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

static constexpr uint32 kHashTableSize = 1024 * 16;
static constexpr uint32 kDirectionCount = 4;
static constexpr uint32 kNumCascades = 4;

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
	if (!RC::WorldSpace::CVarEnabled.GetValueOnRenderThread())
	{
		return;
	}

	//Capability checks
	if (!IsRayTracingEnabled() || !ViewInfo.HasRayTracingScene())
	{
		UE_LOG(LogRC, Log, TEXT("No RT available, cannot do GI"));
		return;
	}
	if (ViewInfo.GetFeatureLevel() < ERHIFeatureLevel::SM6)
	{
		UE_LOG(LogRC, Log, TEXT("No SM6 support cannot do RC GI"));
	}
	if (IsForwardShadingEnabled(ViewInfo.GetShaderPlatform()))
	{
		UE_LOG(LogRC, Log, TEXT("RC GI Requires Deferred shading"));
	}
	if (ViewInfo.bIsSceneCapture || ViewInfo.bIsReflectionCapture || ViewInfo.bIsPlanarReflection)
	{
		return;
	}

	// Accesspoint to our Shaders
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ViewInfo.GetFeatureLevel());

	//Initialize pooled resources
	if (!bInitialized)
	{
		//Create Hashmaps for probes
		bInitialized = true;
		FRDGBufferDesc HTDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint64_t), kHashTableSize * kNumCascades);
		AllocatePooledBuffer(HTDesc, HashTableKeys, TEXT("RC Hash table data keys"));

		FRDGBufferDesc HTOccupiedDesc =
			FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32_t), kHashTableSize * kNumCascades);
		AllocatePooledBuffer(HTOccupiedDesc, HashTableLastOccupied, TEXT("RC Hash table last frame occupied indices"));

		//Allocate probes
		FRDGBufferDesc ProbeRadBufferDesc = FRDGBufferDesc::CreateStructuredDesc(
			sizeof(uint32) * 4, kNumCascades * kHashTableSize * (kDirectionCount * 2 * kDirectionCount));
		AllocatePooledBuffer(ProbeRadBufferDesc, ProbeRadianceBuffer,
		                     TEXT("RC Cascade Probes Total Radiance and transmittance"));

		FRDGBufferDesc ProbeDepositCountDesc = FRDGBufferDesc::CreateStructuredDesc(
			sizeof(uint32), kNumCascades * kHashTableSize);
		AllocatePooledBuffer(ProbeDepositCountDesc, ProbeDepositCountBuffer,
		                     TEXT("RC Cascade Probe Ray Deposit Count"));

		FRDGBufferDesc ProbeWeightBufferDesc = FRDGBufferDesc::CreateStructuredDesc(
			sizeof(uint32), kNumCascades * kHashTableSize * (kDirectionCount * 2 * kDirectionCount));
		AllocatePooledBuffer(ProbeWeightBufferDesc, ProbeWeightBuffer,
			TEXT("RC Cascade Probe Weights"));
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

		auto TableKeys = GraphBuilder.RegisterExternalBuffer(HashTableKeys);
		auto TableKeysUAV = GraphBuilder.CreateUAV(TableKeys);

		auto HashTableOccupied = GraphBuilder.RegisterExternalBuffer(HashTableLastOccupied);
		auto HashTableOccupiedUAV = GraphBuilder.CreateUAV(HashTableOccupied);

		auto RadianceBuffer = GraphBuilder.RegisterExternalBuffer(ProbeRadianceBuffer);
		auto RadianceUAV = GraphBuilder.CreateUAV(RadianceBuffer);
		auto RadianceSRV = GraphBuilder.CreateSRV(RadianceBuffer);

		auto DepositCountBuffer = GraphBuilder.RegisterExternalBuffer(ProbeDepositCountBuffer);
		auto DepositCountBufferUAV = GraphBuilder.CreateUAV(DepositCountBuffer);
		auto DepositCountBufferSRV = GraphBuilder.CreateSRV(DepositCountBuffer);

		auto WeightBuffer = GraphBuilder.RegisterExternalBuffer(ProbeWeightBuffer);
		auto WeightUAV = GraphBuilder.CreateUAV(WeightBuffer);
		auto WeightSRV = GraphBuilder.CreateSRV(WeightBuffer);

		//Create RDG buffers

		// - HashTable
		FRDGBufferDesc TableFreeListDesc = FRDGBufferDesc::CreateStructuredDesc(
			sizeof(uint32), kNumCascades * kHashTableSize);
		auto TableFreeList = GraphBuilder.CreateBuffer(TableFreeListDesc, TEXT("RC Table free list"));
		auto TableFreeListUAV = GraphBuilder.CreateUAV(TableFreeList);
		FRDGBufferDesc TableFreeListIndexDesc = FRDGBufferDesc::CreateStructuredDesc(
			sizeof(uint32), kNumCascades);
		auto TableFreeListIndex = GraphBuilder.CreateBuffer(TableFreeListIndexDesc, TEXT("RC Table free list index"));
		auto TableFreeListIndexUAV = GraphBuilder.CreateUAV(TableFreeListIndex);
		//Clear Freelist counter to tablestart
		{
			FWorldSpaceRCClearFreeList::FParameters* PassParameters = GraphBuilder.AllocParameters<
				FWorldSpaceRCClearFreeList::FParameters>();

			PassParameters->TableFreeListIndex = TableFreeListIndexUAV;
			PassParameters->HashTableSize = kHashTableSize;

			TShaderMapRef<FWorldSpaceRCClearFreeList> Shader(GlobalShaderMap);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("RC Clear Freelist to table starts"),
				Shader,
				PassParameters,
				FIntVector(1, 1, 1));
		}
		FRDGBufferDesc TableEntryKeysDesc = FRDGBufferDesc::CreateStructuredDesc(
			sizeof(uint64_t), kNumCascades * kHashTableSize);
		auto TableEntryKeys = GraphBuilder.CreateBuffer(TableEntryKeysDesc, TEXT("RC Table Entries > Key"));
		auto TableEntryKeysUAV = GraphBuilder.CreateUAV(TableEntryKeys);
		AddClearUAVPass(GraphBuilder, TableEntryKeysUAV, 0);
		FRDGBufferDesc TableEntryIndexDesc = FRDGBufferDesc::CreateStructuredDesc(
			sizeof(uint32), kNumCascades * kHashTableSize);
		auto TableEntryIndex = GraphBuilder.CreateBuffer(TableEntryIndexDesc, TEXT("RC Table Entries > Index"));
		auto TableEntryIndexUAV = GraphBuilder.CreateUAV(TableEntryIndex);




		FRDGBufferDesc ProbeBinCountDesc = FRDGBufferDesc::CreateStructuredDesc(
			sizeof(uint32), kHashTableSize);
		auto BinCountBuffer = GraphBuilder.CreateBuffer(ProbeBinCountDesc, TEXT("RC Probe R2 bin count"));
		auto BinCountBufferUAV = GraphBuilder.CreateUAV(BinCountBuffer);
		auto BinCountBufferSRV = GraphBuilder.CreateSRV(BinCountBuffer);
		AddClearUAVPass(GraphBuilder, BinCountBufferUAV, 0);


		FIntPoint PassViewSize = SceneColor.ViewRect.Size();
		FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(PassViewSize,
		                                                           FComputeShaderUtils::kGolden2DGroupSize);
		//Register active probes
		FRDGBufferRef RDGActiveProbes;
		FRDGBufferUAVRef ActiveProbeUAV;
		FRDGBufferSRVRef ActiveProbeSRV;
		FRDGBufferRef RDGActiveProbeCount;
		FRDGBufferUAVRef ActiveProbeCountUAV;
		FRDGBufferSRVRef ActiveProbeCountSRV;
		FRDGBufferDesc ActiveProbesDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), kHashTableSize*kNumCascades);
		FRDGBufferDesc ActiveProbesCounterDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), kNumCascades);
		
		RDGActiveProbes = GraphBuilder.CreateBuffer(ActiveProbesDesc, TEXT("RC Active probes"));
		ActiveProbeUAV = GraphBuilder.CreateUAV(RDGActiveProbes);
		ActiveProbeSRV = GraphBuilder.CreateSRV(RDGActiveProbes);

		RDGActiveProbeCount = GraphBuilder.CreateBuffer(ActiveProbesCounterDesc,
		                                                    TEXT("RC Active probes counter"));
		ActiveProbeCountUAV = GraphBuilder.CreateUAV(RDGActiveProbeCount);
		ActiveProbeCountSRV = GraphBuilder.CreateSRV(RDGActiveProbeCount);
		AddClearUAVPass(GraphBuilder, ActiveProbeCountUAV, 0);

		float HistoryConservation = RC::WorldSpace::CVarHistoryConservation.GetValueOnRenderThread();

		//Build freelist
		{
			FWorldSpaceRCBuildFreeList::FParameters* PassParameters = GraphBuilder.AllocParameters<
				FWorldSpaceRCBuildFreeList::FParameters>();

			PassParameters->View = ViewInfo.ViewUniformBuffer;
			PassParameters->TableKeys = TableKeysUAV;
			PassParameters->TableFreeList = TableFreeListUAV;
			PassParameters->TableFreeListIndex = TableFreeListIndexUAV;
			PassParameters->TableEntryKeys = TableEntryKeysUAV;
			PassParameters->TableEntryIndex = TableEntryIndexUAV;
			PassParameters->TableLastOccupiedFrame = HashTableOccupiedUAV;
			PassParameters->HashTableSize = kHashTableSize;
			PassParameters->NumCascades = kNumCascades;
			PassParameters->ActiveCounter = ActiveProbeCountUAV;
			PassParameters->ActiveProbes = ActiveProbeUAV;
			PassParameters->BaseDirections = kDirectionCount;
			PassParameters->TotalRadiance = RadianceUAV;
			PassParameters->ProbeWeights = WeightUAV;
			PassParameters->DepositCounts = DepositCountBufferUAV;
			PassParameters->BinCounts = BinCountBufferUAV;
			PassParameters->HistoryConservation = HistoryConservation;

			TShaderMapRef<FWorldSpaceRCBuildFreeList> Shader(GlobalShaderMap);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("RC Build Freelist"),
				Shader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(kHashTableSize * kNumCascades, 64));
		}
		//Clear Freelist counter to tablestart
		{
			FWorldSpaceRCClearFreeList::FParameters* PassParameters = GraphBuilder.AllocParameters<
				FWorldSpaceRCClearFreeList::FParameters>();

			PassParameters->TableFreeListIndex = TableFreeListIndexUAV;
			PassParameters->HashTableSize = kHashTableSize;

			TShaderMapRef<FWorldSpaceRCClearFreeList> Shader(GlobalShaderMap);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("RC Clear Freelist to table starts"),
				Shader,
				PassParameters,
				FIntVector(1, 1, 1));
		}
		//Gather pass, Fill lowest cascade HashTable from the screen
		{
			FWorldSpaceRCGather::FParameters* PassParameters = GraphBuilder.AllocParameters<
				FWorldSpaceRCGather::FParameters>();

			PassParameters->View = ViewInfo.ViewUniformBuffer;
			PassParameters->SceneTextures = SceneTextureParams;
			PassParameters->TableKeys = TableKeysUAV;
			PassParameters->TableFreeList = TableFreeListUAV;
			PassParameters->TableFreeListIndex = TableFreeListIndexUAV;
			PassParameters->TableEntryKeys = TableEntryKeysUAV;
			PassParameters->TableEntryIndex = TableEntryIndexUAV;
			PassParameters->HashTableSize = kHashTableSize;
			PassParameters->BaseVoxelSize = BaseVoxelSize;
			PassParameters->LevelBaseDist = LevelBaseDist;
			PassParameters->TableLastOccupiedFrame = HashTableOccupiedUAV;

			PassParameters->Output = Output;
			PassParameters->ActiveProbes = ActiveProbeUAV;
			PassParameters->ActiveCounter = ActiveProbeCountUAV;
			PassParameters->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);

			TShaderMapRef<FWorldSpaceRCGather> GatherShader(GlobalShaderMap);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("RC Gather pass %dx%d", PassViewSize.X, PassViewSize.Y),
				GatherShader,
				PassParameters,
				GroupCount);
		}
		//Fill further cascades using the filled probes from the lowest one, bottom to top
		{
			for (int i = 1; i < kNumCascades; ++i)
			{
				FWorldSpaceRCFillCascade::FParameters* PassParameters = GraphBuilder.AllocParameters<
					FWorldSpaceRCFillCascade::FParameters>();

				PassParameters->View = ViewInfo.ViewUniformBuffer;
				PassParameters->SceneTextures = SceneTextureParams;
				PassParameters->Cascade = i;
				PassParameters->TableKeys = TableKeysUAV;
				PassParameters->TableFreeList = TableFreeListUAV;
				PassParameters->TableFreeListIndex = TableFreeListIndexUAV;
				PassParameters->TableEntryKeys = TableEntryKeysUAV;
				PassParameters->TableEntryIndex = TableEntryIndexUAV;
				PassParameters->HashTableSize = kHashTableSize;
				PassParameters->BaseVoxelSize = BaseVoxelSize;
				PassParameters->LevelBaseDist = LevelBaseDist;


				PassParameters->TableLastOccupiedFrame = HashTableOccupiedUAV;

				PassParameters->ActiveProbes = ActiveProbeUAV;
				PassParameters->ActiveCounter = ActiveProbeCountUAV;

				PassParameters->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);

				TShaderMapRef<FWorldSpaceRCFillCascade> GatherShader(GlobalShaderMap);

				auto IndirectBuffer = FComputeShaderUtils::AddIndirectArgsSetupCsPass1D(
					GraphBuilder, ViewInfo.GetFeatureLevel(), RDGActiveProbeCount, TEXT("RC Indirect Args"),
					FWorldSpaceRCFillCascade::GroupCount, i-1);
				PassParameters->IndirectArgsBuffer = IndirectBuffer;

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("RC Fill Cascade %d", i),
					GatherShader,
					PassParameters,
					IndirectBuffer, 0);
			}
		}
		//Trace the scene per pixel and split into probes
		{
			TShaderMapRef<FWorldSpaceRCRayBin> RayGenShader(GlobalShaderMap);
			FWorldSpaceRCRayBin::FParameters* PassParameters = GraphBuilder.AllocParameters<
				FWorldSpaceRCRayBin::FParameters>();
			PassParameters->Output = Output;
			PassParameters->TableKeys = TableKeysUAV;
			PassParameters->TableFreeList = TableFreeListUAV;
			PassParameters->TableFreeListIndex = TableFreeListIndexUAV;
			PassParameters->TableEntryKeys = TableEntryKeysUAV;
			PassParameters->TableEntryIndex = TableEntryIndexUAV;
			PassParameters->HashTableSize = kHashTableSize;
			PassParameters->TotalRadiance = RadianceUAV;
			PassParameters->Weights = WeightUAV;
			PassParameters->BinCounts = BinCountBufferUAV;
			PassParameters->DepositCounts = DepositCountBufferUAV;
			PassParameters->TLAS = ViewInfo.GetRayTracingSceneLayerViewChecked(ERayTracingSceneLayer::Base);
			PassParameters->BaseDirections = kDirectionCount;
			PassParameters->NormalOffset = RC::WorldSpace::CVarNormalOffset.GetValueOnRenderThread();
			PassParameters->BaseVoxelSize = BaseVoxelSize;
			PassParameters->LevelBaseDist = LevelBaseDist;
			const FSceneTextures& SceneTex = ViewInfo.GetSceneTextures();
			PassParameters->SceneTextures.SceneDepthTexture = SceneTex.Depth.Resolve;
			PassParameters->SceneTextures.GBufferATexture = SceneTex.GBufferA;
			PassParameters->SceneTextures.GBufferBTexture = SceneTex.GBufferB;
			PassParameters->SceneTextures.GBufferCTexture = SceneTex.GBufferC;
			PassParameters->SceneTextures.GBufferDTexture = SceneTex.GBufferD;
			PassParameters->SceneTextures.GBufferETexture = SceneTex.GBufferE;
			//RGParams->SceneTextures.GBufferFTexture = SceneTex.GBufferF;
			//RGParams->SceneTextures.GBufferSGGXTexture = SceneTex.GBufferSGGX;

			PassParameters->View = ViewInfo.ViewUniformBuffer;
			PassParameters->RaytracingLightGridData = ViewInfo.RayTracingLightGridUniformBuffer;
			PassParameters->SceneDepth = ViewInfo.GetSceneTextures().Depth.Resolve;
			auto SceneUniformBuffer = GetSceneUniformBufferRef(GraphBuilder, ViewInfo);
			TRDGUniformBufferRef<FNaniteRayTracingUniformParameters> NaniteRayTracingUniformBuffer =
				Nanite::GetPublicGlobalRayTracingUniformBuffer();

			PassParameters->Scene = SceneUniformBuffer;
			PassParameters->NaniteRayTracing = NaniteRayTracingUniformBuffer;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("RC TraceRays %dx%d", PassViewSize.X, PassViewSize.Y),
				PassParameters,
				ERDGPassFlags::Compute,
				[PassParameters, RayGenShader, &ViewInfo, SceneUniformBuffer, NaniteRayTracingUniformBuffer, PassViewSize]
			(FRDGAsyncTask, FRHICommandList& RHICmdList)
				{
					FRHIBatchedShaderParameters& GlobalResources = RHICmdList.GetScratchShaderParameters();
					SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);

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
			sizeof(float) * 4, kNumCascades * kHashTableSize * (kDirectionCount * 2 * kDirectionCount));
		auto MergedRadianceBuffer = GraphBuilder.CreateBuffer(ProbeMergedRadBufferDesc,
		                                                      TEXT("RC Probe Merged radiance+weight"));
		auto MergedRadianceUAV = GraphBuilder.CreateUAV(MergedRadianceBuffer);
		AddClearUAVPass(GraphBuilder, MergedRadianceUAV, 0);

		{
			for (int i = kNumCascades - 1; i >= 0; --i)
			{
				FWorldSpaceRCMerging::FParameters* PassParameters = GraphBuilder.AllocParameters<
					FWorldSpaceRCMerging::FParameters>();

				PassParameters->TableKeys = TableKeysUAV;
				PassParameters->TableFreeList = TableFreeListUAV;
				PassParameters->TableFreeListIndex = TableFreeListIndexUAV;
				PassParameters->TableEntryKeys = TableEntryKeysUAV;
				PassParameters->TableEntryIndex = TableEntryIndexUAV;
				PassParameters->HashTableSize = kHashTableSize;
				PassParameters->Cascade = i;
				PassParameters->HistoryConservation = HistoryConservation;
				PassParameters->NumCascades = kNumCascades;
				PassParameters->HashTableSize = kHashTableSize;
				PassParameters->BaseVoxelSize = BaseVoxelSize;
				PassParameters->LevelBaseDist = LevelBaseDist;
				PassParameters->ActiveProbes = ActiveProbeSRV;
				PassParameters->ActiveCounter = ActiveProbeCountSRV;
				PassParameters->BaseDirections = kDirectionCount;
				PassParameters->ProbeRadiance = RadianceUAV;
				PassParameters->ProbeWeights = WeightUAV;
				PassParameters->ProbeMergedRadiance = MergedRadianceUAV;
				PassParameters->ProbeDepositCount = DepositCountBufferSRV;

				TShaderMapRef<FWorldSpaceRCMerging> MergeShader(GlobalShaderMap);

				auto IndirectBuffer = FComputeShaderUtils::AddIndirectArgsSetupCsPass1D(
					GraphBuilder, ViewInfo.GetFeatureLevel(), RDGActiveProbeCount, TEXT("RC Indirect Args"),
					FWorldSpaceRCMerging::GroupCount, i);
				PassParameters->IndirectArgsBuffer = IndirectBuffer;

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("RC Merge Cascade %d into %d", i+1, i),
					MergeShader,
					PassParameters,
					IndirectBuffer, 0);
			}
		}


		//Output
		// Set the shader parameters
		FWorldSpaceRCApply::FParameters* PassParameters = GraphBuilder.AllocParameters<
			FWorldSpaceRCApply::FParameters>();

		// Input is the SceneColor from PostProcess Material Inputs
		PassParameters->View = ViewInfo.ViewUniformBuffer;
		PassParameters->SceneTextures = SceneTextureParams;
		PassParameters->TableKeys = TableKeysUAV;
		PassParameters->TableFreeList = TableFreeListUAV;
		PassParameters->TableFreeListIndex = TableFreeListIndexUAV;
		PassParameters->TableEntryKeys = TableEntryKeysUAV;
		PassParameters->TableEntryIndex = TableEntryIndexUAV;
		PassParameters->HashTableSize = kHashTableSize;
		PassParameters->BaseVoxelSize = BaseVoxelSize;
		PassParameters->LevelBaseDist = LevelBaseDist;
		PassParameters->Intensity = RC::WorldSpace::CVarIntensity.GetValueOnRenderThread();
		PassParameters->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);
		PassParameters->BaseDirections = kDirectionCount;
		PassParameters->ProbeRadiance = GraphBuilder.CreateSRV(RadianceBuffer);
		PassParameters->ProbeWeights = GraphBuilder.CreateSRV(WeightBuffer);
		PassParameters->Output = Output;
		PassParameters->DisplayCascade = RC::WorldSpace::CVarDisplayCascade.GetValueOnRenderThread();
		PassParameters->ProbeMergedRadiance = GraphBuilder.CreateSRV(MergedRadianceBuffer);
		PassParameters->ProbeDepositCount = DepositCountBufferSRV;
		PassParameters->DebugOutputCells = RC::WorldSpace::CVarDisplayProbes.GetValueOnRenderThread();
		TShaderMapRef<FWorldSpaceRCApply> ApplyCS(GlobalShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("RC Applying Output pass %dx%d", PassViewSize.X, PassViewSize.Y),
			ApplyCS,
			PassParameters,
			GroupCount);
	}
}
