#include "WorldSpace/WorldSpaceRCGi.h"

#include "IContentBrowserSingleton.h"
#include "RenderTargetPool.h"
#include "SceneRendering.h"
#include "SceneTextureParameters.h"
#include "Core/RCLog.h"

#include "Core/RCVars.h"
#include "DataWrappers/ChaosVDCollisionDataWrappers.h"
#include "WorldSpaceRCShaders.h"


FWorldSpaceRCGi::FWorldSpaceRCGi()
{
	ResetCommand = MakeUnique<FAutoConsoleCommand>(
		TEXT("r.RC.ResetTable"),
		TEXT("Reset hashtable."),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			bResetTable.store(true, std::memory_order_release);
		}));
}

static constexpr uint32 HashTableSize = 1024*16;
static constexpr uint32 DirectionCount = 4;
static constexpr uint32 Cascades = 4;

void FWorldSpaceRCGi::PrepareRayTracing(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (!IsRayTracingEnabled(View.GetShaderPlatform()))
	{
		UE_LOG(LogRC, Warning, TEXT("RT not Available, no RC GI"))
		return;
	}

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FWorldSpaceRCRayBin> RayGenShader(ShaderMap);
	OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
}

void FWorldSpaceRCGi::RenderDiffuseIndirectLight(const FScene& Scene, const FViewInfo& ViewInfo,
                                                 FRDGBuilder& GraphBuilder,
                                                 FGlobalIlluminationPluginResources& Resources)
{
	if (!RC::WorldSpace::CVarEnabled.GetValueOnRenderThread()) return;

	//Capability checks
	if (!IsRayTracingEnabled() || !ViewInfo.HasRayTracingScene())
	{
		UE_LOG(LogRC, Log, TEXT("No RT available, cannot do GI"));
		return;
	}
	if (ViewInfo.GetFeatureLevel() < ERHIFeatureLevel::SM6) {
		UE_LOG(LogRC, Log, TEXT("No SM6 support cannot do RC GI"));
	}
	if (IsForwardShadingEnabled(ViewInfo.GetShaderPlatform()))
	{
		UE_LOG(LogRC, Log, TEXT("RC GI Requires Deferred shading"));
	}
	if (ViewInfo.bIsSceneCapture || ViewInfo.bIsReflectionCapture || ViewInfo.bIsPlanarReflection) return;

	// Accesspoint to our Shaders
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ViewInfo.GetFeatureLevel());

	//Initialize pooled resources
	if (!bInitialized)
	{
		//Create Hashmaps for probes
		bInitialized = true;
		FRDGBufferDesc HTDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint64_t), HashTableSize * Cascades);
		AllocatePooledBuffer(HTDesc, HashTableCascade, TEXT("RC Cascade Hashmap"));

		//Allocate probes
		FRDGBufferDesc ProbeRadBufferDesc = FRDGBufferDesc::CreateStructuredDesc(
			sizeof(uint32) * 4, Cascades * HashTableSize * (DirectionCount * 2 * DirectionCount));
		AllocatePooledBuffer(ProbeRadBufferDesc, ProbeRadianceBuffer,
		                     TEXT("RC Cascade Probes Total Radiance and transmittance"));

		FRDGBufferDesc ProbeDepositCountDesc = FRDGBufferDesc::CreateStructuredDesc(
			sizeof(uint32), Cascades * HashTableSize);
		AllocatePooledBuffer(ProbeDepositCountDesc, ProbeDepositCountBuffer, TEXT("RC Cascade Probe Ray Deposit Count"));
	}



	const FScreenPassTexture SceneColor(Resources.SceneColor, ViewInfo.ViewRect);
	const FScreenPassTextureViewport SceneColorViewport(SceneColor);

	RDG_EVENT_SCOPE(GraphBuilder, "Screen Space RC");
	{
		float BaseVoxelSize = RC::WorldSpace::CVarProbeSize.GetValueOnRenderThread();
		float LevelBaseDist = RC::WorldSpace::CVarProbeHalfDistance.GetValueOnRenderThread();

		auto SceneTextureParams = CreateSceneTextureShaderParameters(GraphBuilder, &ViewInfo.GetSceneTextures(),
		                                                             ViewInfo.GetFeatureLevel());
		auto Output = GraphBuilder.CreateUAV(Resources.SceneColor);

		//Import pooled buffers
		//Clear hash
		auto HashTable = GraphBuilder.RegisterExternalBuffer(HashTableCascade);
		auto HashTableUAV = GraphBuilder.CreateUAV(HashTable);
		auto HashTableSRV = GraphBuilder.CreateSRV(HashTable);
		AddClearUAVPass(GraphBuilder, HashTableUAV, 0);

		//Clear Radiance
		auto RadianceBuffer = GraphBuilder.RegisterExternalBuffer(ProbeRadianceBuffer);
		auto RadianceUAV = GraphBuilder.CreateUAV(RadianceBuffer);
		auto RadianceSRV = GraphBuilder.CreateSRV(RadianceBuffer);
		AddClearUAVPass(GraphBuilder, RadianceUAV, 0);

		//Clear deposit counter
		auto DepositCountBuffer = GraphBuilder.RegisterExternalBuffer(ProbeDepositCountBuffer);
		auto DepositCountBufferUAV = GraphBuilder.CreateUAV(DepositCountBuffer);
		auto DepositCountBufferSRV = GraphBuilder.CreateSRV(DepositCountBuffer);
		AddClearUAVPass(GraphBuilder, DepositCountBufferUAV, 0);

		//Create RDG buffers

		FRDGBufferDesc ProbeWeightBufferDesc = FRDGBufferDesc::CreateStructuredDesc(
			sizeof(uint32), Cascades * HashTableSize * (DirectionCount * 2 * DirectionCount));
		auto WeightBuffer = GraphBuilder.CreateBuffer(ProbeWeightBufferDesc, TEXT("RC Cascade Probe weights"));
		auto WeightUAV = GraphBuilder.CreateUAV(WeightBuffer);
		auto WeightSRV = GraphBuilder.CreateSRV(WeightBuffer);
		AddClearUAVPass(GraphBuilder, WeightUAV, 0);

		FRDGBufferDesc ProbeBinCountDesc = FRDGBufferDesc::CreateStructuredDesc(
			sizeof(uint32), HashTableSize);
		auto BinCountBuffer = GraphBuilder.CreateBuffer(ProbeBinCountDesc, TEXT("RC Probe R2 bin count"));
		auto BinCountBufferUAV = GraphBuilder.CreateUAV(BinCountBuffer);
		auto BinCountBufferSRV = GraphBuilder.CreateSRV(BinCountBuffer);
		AddClearUAVPass(GraphBuilder, BinCountBufferUAV, 0);


		FIntPoint PassViewSize = SceneColor.ViewRect.Size();
		FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(PassViewSize,
		                                                           FComputeShaderUtils::kGolden2DGroupSize);
		//Register active probes
		TArray<FRDGBufferRef> RDGActiveProbes;
		TArray<FRDGBufferUAVRef> ActiveProbeUAVs;
		TArray<FRDGBufferSRVRef> ActiveProbeSRVs;
		RDGActiveProbes.SetNum(Cascades);
		ActiveProbeUAVs.SetNum(Cascades);
		ActiveProbeSRVs.SetNum(Cascades);
		TArray<FRDGBufferRef> RDGActiveProbeCounts;
		TArray<FRDGBufferUAVRef> ActiveProbeCountUAVs;
		TArray<FRDGBufferSRVRef> ActiveProbeCountSRVs;
		RDGActiveProbeCounts.SetNum(Cascades);
		ActiveProbeCountUAVs.SetNum(Cascades);
		ActiveProbeCountSRVs.SetNum(Cascades);

		FRDGBufferDesc ActiveProbesDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), HashTableSize);
		FRDGBufferDesc ActiveProbesCounterDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1);
		for (int i = 0; i < Cascades; ++i)
		{
			RDGActiveProbes[i] = GraphBuilder.CreateBuffer(ActiveProbesDesc, TEXT("RC Active probes"));
			ActiveProbeUAVs[i] = GraphBuilder.CreateUAV(RDGActiveProbes[i]);
			ActiveProbeSRVs[i] = GraphBuilder.CreateSRV(RDGActiveProbes[i]);
			RDGActiveProbeCounts[i] = GraphBuilder.CreateBuffer(ActiveProbesCounterDesc, TEXT("RC Active probes counter"));
			ActiveProbeCountUAVs[i] = GraphBuilder.CreateUAV(RDGActiveProbeCounts[i]);
			ActiveProbeCountSRVs[i] = GraphBuilder.CreateSRV(RDGActiveProbeCounts[i]);
		}
		AddClearUAVPass(GraphBuilder, ActiveProbeCountUAVs[0], 0);


		//Gather pass, Fill lowest cascade HashTable from the screen
		{
			FWorldSpaceRCGather::FParameters* GatherPassParameters = GraphBuilder.AllocParameters<
				FWorldSpaceRCGather::FParameters>();

			GatherPassParameters->View = ViewInfo.ViewUniformBuffer;
			GatherPassParameters->SceneTextures = SceneTextureParams;
			GatherPassParameters->RWHashTable = HashTableUAV;
			GatherPassParameters->HashTableSize = HashTableSize;
			GatherPassParameters->BaseVoxelSize = BaseVoxelSize;
			GatherPassParameters->LevelBaseDist = LevelBaseDist;

				GatherPassParameters->Output = Output;
			GatherPassParameters->ActiveProbes = ActiveProbeUAVs[0];
			GatherPassParameters->ActiveCounter = ActiveProbeCountUAVs[0];
			GatherPassParameters->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);

			TShaderMapRef<FWorldSpaceRCGather> GatherShader(GlobalShaderMap);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("RC Gather pass %dx%d", PassViewSize.X, PassViewSize.Y),
				GatherShader,
				GatherPassParameters,
				GroupCount);
		}
		//Fill further cascades using the filled probes from the lowest one, bottom to top
		{
			for (int i = 1; i < Cascades; ++i)
			{
				FWorldSpaceRCFillCascade::FParameters* FillCascadeParams = GraphBuilder.AllocParameters<
					FWorldSpaceRCFillCascade::FParameters>();

				FillCascadeParams->View = ViewInfo.ViewUniformBuffer;
				FillCascadeParams->SceneTextures = SceneTextureParams;
				FillCascadeParams->RWHashTable = HashTableUAV;
				FillCascadeParams->Cascade = i;
				FillCascadeParams->HashTableSize = HashTableSize;
				FillCascadeParams->PrevActiveProbes = ActiveProbeSRVs[i - 1];
				FillCascadeParams->PrevActiveCounter = ActiveProbeCountSRVs[i - 1];
				FillCascadeParams->BaseVoxelSize = BaseVoxelSize;
				FillCascadeParams->LevelBaseDist = LevelBaseDist;
				AddClearUAVPass(GraphBuilder, ActiveProbeCountUAVs[i], 0);

				FillCascadeParams->ActiveProbes = ActiveProbeUAVs[i];
				FillCascadeParams->ActiveCounter = ActiveProbeCountUAVs[i];

				FillCascadeParams->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);

				TShaderMapRef<FWorldSpaceRCFillCascade> GatherShader(GlobalShaderMap);

				auto IndirectBuffer = FComputeShaderUtils::AddIndirectArgsSetupCsPass1D(
					GraphBuilder, ViewInfo.GetFeatureLevel(), RDGActiveProbeCounts[i - 1], TEXT("RC Indirect Args"),
					FWorldSpaceRCFillCascade::GroupCount);
				FillCascadeParams->IndirectArgsBuffer = IndirectBuffer;

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("RC Fill Cascade %d", i),
					GatherShader,
					FillCascadeParams,
					IndirectBuffer, 0);
			}
		}
		//Trace the scene per pixel and split into probes
		{
			TShaderMapRef<FWorldSpaceRCRayBin> RayGenShader(GlobalShaderMap);
			FWorldSpaceRCRayBin::FParameters* RGParams = GraphBuilder.AllocParameters<
				FWorldSpaceRCRayBin::FParameters>();
			RGParams->Output = Output;
			RGParams->HashTableSize = HashTableSize;
			RGParams->HashTable = HashTableSRV;
			RGParams->TotalRadiance = RadianceUAV;
			RGParams->Weights = WeightUAV;
			RGParams->BinCounts = BinCountBufferUAV;
			RGParams->DepositCounts = DepositCountBufferUAV;
			RGParams->TLAS = ViewInfo.GetRayTracingSceneLayerViewChecked(ERayTracingSceneLayer::Base);
			RGParams->BaseDirections = DirectionCount;
			RGParams->NormalOffset = RC::WorldSpace::CVarNormalOffset.GetValueOnRenderThread();
			RGParams->BaseVoxelSize = BaseVoxelSize;
			RGParams->LevelBaseDist = LevelBaseDist;
			const FSceneTextures& SceneTex = ViewInfo.GetSceneTextures();
			RGParams->SceneTextures.SceneDepthTexture = SceneTex.Depth.Resolve;
			RGParams->SceneTextures.GBufferATexture = SceneTex.GBufferA;
			RGParams->SceneTextures.GBufferBTexture = SceneTex.GBufferB;
			RGParams->SceneTextures.GBufferCTexture = SceneTex.GBufferC;
			RGParams->SceneTextures.GBufferDTexture = SceneTex.GBufferD;
			RGParams->SceneTextures.GBufferETexture = SceneTex.GBufferE;
			//RGParams->SceneTextures.GBufferFTexture = SceneTex.GBufferF;
			//RGParams->SceneTextures.GBufferSGGXTexture = SceneTex.GBufferSGGX;

			RGParams->View = ViewInfo.ViewUniformBuffer;
			RGParams->RaytracingLightGridData = ViewInfo.RayTracingLightGridUniformBuffer;
			RGParams->SceneDepth = ViewInfo.GetSceneTextures().Depth.Resolve;
			auto SceneUniformBuffer = GetSceneUniformBufferRef(GraphBuilder, ViewInfo);
			TRDGUniformBufferRef<FNaniteRayTracingUniformParameters> NaniteRayTracingUniformBuffer =
				Nanite::GetPublicGlobalRayTracingUniformBuffer();

			RGParams->Scene = SceneUniformBuffer;
			RGParams->NaniteRayTracing = NaniteRayTracingUniformBuffer;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("RC TraceRays %dx%d", PassViewSize.X, PassViewSize.Y),
				RGParams,
				ERDGPassFlags::Compute,
				[RGParams, RayGenShader, &ViewInfo, SceneUniformBuffer, NaniteRayTracingUniformBuffer, PassViewSize]
			(FRDGAsyncTask, FRHICommandList& RHICmdList)
				{
					FRHIBatchedShaderParameters& GlobalResources = RHICmdList.GetScratchShaderParameters();
					SetShaderParameters(GlobalResources, RayGenShader, *RGParams);

					TOptional<FScopedUniformBufferStaticBindings> StaticUniformBufferScope =
						RayTracing::BindStaticUniformBufferBindings(
							ViewInfo,
							SceneUniformBuffer->GetRHI(),
							NaniteRayTracingUniformBuffer->GetRHI(),
							RHICmdList);

					FRayTracingPipelineState* Pipeline = ViewInfo.MaterialRayTracingData.PipelineState;
					FRHIShaderBindingTable* SBT = ViewInfo.MaterialRayTracingData.ShaderBindingTable;

					RHICmdList.RayTraceDispatch(Pipeline, RayGenShader.GetRayTracingShader(), SBT,
					                            GlobalResources, PassViewSize.X, PassViewSize.Y);
				});
		}

		//Merge probes down


		FRDGBufferDesc ProbeMergedRadBufferDesc = FRDGBufferDesc::CreateStructuredDesc(
			sizeof(float) * 4, Cascades * HashTableSize * (DirectionCount * 2 * DirectionCount));
		auto MergedRadianceBuffer = GraphBuilder.CreateBuffer(ProbeMergedRadBufferDesc, TEXT("RC Probe Merged radiance+weight"));
		auto MergedRadianceUAV = GraphBuilder.CreateUAV(MergedRadianceBuffer);
		AddClearUAVPass(GraphBuilder, MergedRadianceUAV, 0);

		{
			for (int i = Cascades - 1; i >= 0; --i)
			{
				FWorldSpaceRCMerging::FParameters* MergeCascadeParams = GraphBuilder.AllocParameters<
					FWorldSpaceRCMerging::FParameters>();

				MergeCascadeParams->HashTable = HashTableSRV;
				MergeCascadeParams->Cascade = i;
				MergeCascadeParams->HashTableSize = HashTableSize;
				MergeCascadeParams->BaseVoxelSize = BaseVoxelSize;
				MergeCascadeParams->LevelBaseDist = LevelBaseDist;
				MergeCascadeParams->ActiveProbes = ActiveProbeSRVs[i];
				MergeCascadeParams->ActiveCounter = ActiveProbeCountSRVs[i];
				MergeCascadeParams->BaseDirections = DirectionCount;
				MergeCascadeParams->ProbeRadiance = RadianceSRV;
				MergeCascadeParams->ProbeWeights = WeightSRV;
				MergeCascadeParams->ProbeMergedRadiance = MergedRadianceUAV;
				MergeCascadeParams->ProbeDepositCount = DepositCountBufferSRV;

				TShaderMapRef<FWorldSpaceRCMerging> MergeShader(GlobalShaderMap);

				auto IndirectBuffer = FComputeShaderUtils::AddIndirectArgsSetupCsPass1D(
					GraphBuilder, ViewInfo.GetFeatureLevel(), RDGActiveProbeCounts[i], TEXT("RC Indirect Args"),
					FWorldSpaceRCMerging::GroupCount);
				MergeCascadeParams->IndirectArgsBuffer = IndirectBuffer;

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("RC Merge Cascade %d into %d", i+1, i),
					MergeShader,
					MergeCascadeParams,
					IndirectBuffer, 0);
			}
		}


		//Output
		// Set the shader parameters
		FWorldSpaceRCApply::FParameters* ApplyPassParameters = GraphBuilder.AllocParameters<
			FWorldSpaceRCApply::FParameters>();

		// Input is the SceneColor from PostProcess Material Inputs
		ApplyPassParameters->View = ViewInfo.ViewUniformBuffer;
		ApplyPassParameters->SceneTextures = SceneTextureParams;
		ApplyPassParameters->HashTable = HashTableSRV;
		ApplyPassParameters->HashTableSize = HashTableSize;
		ApplyPassParameters->BaseVoxelSize = BaseVoxelSize;
		ApplyPassParameters->LevelBaseDist = LevelBaseDist;
		ApplyPassParameters->Intensity = RC::WorldSpace::CVarIntensity.GetValueOnRenderThread();
		ApplyPassParameters->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);
		ApplyPassParameters->BaseDirections = DirectionCount;
		ApplyPassParameters->ProbeRadiance = GraphBuilder.CreateSRV(RadianceBuffer);
		ApplyPassParameters->ProbeWeights = GraphBuilder.CreateSRV(WeightBuffer);
		ApplyPassParameters->Output = Output;
		ApplyPassParameters->DisplayCascade = RC::WorldSpace::CVarDisplayCascade.GetValueOnRenderThread();
		ApplyPassParameters->ProbeMergedRadiance = GraphBuilder.CreateSRV(MergedRadianceBuffer);
		ApplyPassParameters->ProbeDepositCount = DepositCountBufferSRV;
		ApplyPassParameters->DebugOutputCells = RC::WorldSpace::CVarDisplayProbes.GetValueOnRenderThread();
		TShaderMapRef<FWorldSpaceRCApply> ApplyCS(GlobalShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("RC Applying Output pass %dx%d", PassViewSize.X, PassViewSize.Y),
			ApplyCS,
			ApplyPassParameters,
			GroupCount);
	}
}
