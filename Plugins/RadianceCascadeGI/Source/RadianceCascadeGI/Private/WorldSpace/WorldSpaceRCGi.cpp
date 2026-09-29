#include "WorldSpace/WorldSpaceRCGi.h"

#include "IContentBrowserSingleton.h"
#include "RenderTargetPool.h"
#include "SceneRendering.h"
#include "SceneTextureParameters.h"
#include "Core/RCLog.h"

#include "Core/RCVars.h"
#include "DataWrappers/ChaosVDCollisionDataWrappers.h"

IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCGather, "/Plugins/RadianceCascadeGI/WorldSpace/WorldSpaceProbeGather.usf", "MainCS",
                        SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCFillCascade, "/Plugins/RadianceCascadeGI/WorldSpace/WorldSpaceFillCascade.usf", "MainCS",
	SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCRaygen, "/Plugins/RadianceCascadeGI/WorldSpace/RayGen.usf", "MainRG", SF_RayGen);
IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCApply, "/Plugins/RadianceCascadeGI/WorldSpace/WorldSpaceApply.usf", "MainCS",
                        SF_Compute);


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

static constexpr uint32 HashTableSize = 4096;
static constexpr uint32 DirectionCount = 32;
static constexpr uint32 Cascades = 4;

void FWorldSpaceRCGi::PrepareRayTracing(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (!IsRayTracingEnabled(View.GetShaderPlatform()))
	{
		UE_LOG(LogRC, Warning, TEXT("RT not Available, no RC GI"))
		return;
	}

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FWorldSpaceRCRaygen> RayGenShader(ShaderMap);
	OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
}

void FWorldSpaceRCGi::RenderDiffuseIndirectLight(const FScene& Scene, const FViewInfo& ViewInfo,
                                                 FRDGBuilder& GraphBuilder,
                                                 FGlobalIlluminationPluginResources& Resources)
{
	// Accesspoint to our Shaders
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ViewInfo.GetFeatureLevel());

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	if (!bInitialized)
	{
		//Create Hashmaps for probes
		bInitialized = true;
		FRDGBufferDesc HTDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint64_t), HashTableSize*Cascades);
		AllocatePooledBuffer(HTDesc, HashTableCascade, TEXT("RC Cascade Hashmap"));

		FRDGBufferDesc ActiveProbesDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), HashTableSize);

		FRDGBufferDesc ActiveProbesCounterDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1);
		ActiveProbes.SetNum(Cascades);
		ActiveProbeCounters.SetNum(Cascades);
		for (int i = 0; i < Cascades; ++i)
		{
			AllocatePooledBuffer(ActiveProbesDesc, ActiveProbes[i], TEXT("RC Active Probes"));
			AllocatePooledBuffer(ActiveProbesCounterDesc, ActiveProbeCounters[i], TEXT("RC Active Probes"));
		}

		FRDGBufferDesc ProbeRadBufferDesc = FRDGBufferDesc::CreateStructuredDesc(
			sizeof(uint32) * 4, Cascades*HashTableSize * (DirectionCount * 2 * DirectionCount));
		AllocatePooledBuffer(ProbeRadBufferDesc, ProbeRadianceBuffer,
		                     TEXT("RC Cascade Probes Total Radiance and transmittance"));
		FRDGBufferDesc ProbeWeightBufferDesc = FRDGBufferDesc::CreateStructuredDesc(
			sizeof(uint32), Cascades*HashTableSize * (DirectionCount * 2 * DirectionCount));
		AllocatePooledBuffer(ProbeWeightBufferDesc, ProbeWeightBuffer, TEXT("RC Cascade Probes Weight"));
	}

	if (!IsRayTracingEnabled() || !ViewInfo.HasRayTracingScene())
	{
		UE_LOG(LogRC, Log, TEXT("No RT available, cannot do GI"));
		return;
	}

	const FScreenPassTexture SceneColor(Resources.SceneColor, ViewInfo.ViewRect);
	const FScreenPassTextureViewport SceneColorViewport(SceneColor);

	RDG_EVENT_SCOPE(GraphBuilder, "Screen Space RC");
	{
		auto SceneTextureParams = CreateSceneTextureShaderParameters(GraphBuilder, &ViewInfo.GetSceneTextures(),
			ViewInfo.GetFeatureLevel());
		auto Output = GraphBuilder.CreateUAV(Resources.SceneColor);
		//Clear hash
		auto HashTable = GraphBuilder.RegisterExternalBuffer(HashTableCascade);
		auto HashTableUAV = GraphBuilder.CreateUAV(HashTable);
		auto HashTableSRV = GraphBuilder.CreateSRV(HashTable);
		AddClearUAVPass(GraphBuilder, HashTableUAV, 0);

		//Clear Radiance
		auto RadianceBuffer = GraphBuilder.RegisterExternalBuffer(ProbeRadianceBuffer);
		auto RadianceUAV = GraphBuilder.CreateUAV(RadianceBuffer);
		AddClearUAVPass(GraphBuilder, RadianceUAV, 0);
		//And weights
		auto WeightBuffer = GraphBuilder.RegisterExternalBuffer(ProbeWeightBuffer);
		auto WeightUAV = GraphBuilder.CreateUAV(WeightBuffer);
		AddClearUAVPass(GraphBuilder, WeightUAV, 0);
		//Clear Active (cascade 0)
		auto ActiveProbeCount = GraphBuilder.RegisterExternalBuffer(ActiveProbeCounters[0]);
		auto ActiveProbeCountUAV = GraphBuilder.CreateUAV(ActiveProbeCount);
		AddClearUAVPass(GraphBuilder, ActiveProbeCountUAV, 0);
		auto ActiveProbe = GraphBuilder.RegisterExternalBuffer(ActiveProbes[0]);

		FIntPoint PassViewSize = SceneColor.ViewRect.Size();
		FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(PassViewSize,
			FComputeShaderUtils::kGolden2DGroupSize);

		//Gather pass, Fill lowest cascade HashTable from the screen
		{
			FWorldSpaceRCGather::FParameters* GatherPassParameters = GraphBuilder.AllocParameters<
				FWorldSpaceRCGather::FParameters>();

			GatherPassParameters->View = ViewInfo.ViewUniformBuffer;
			GatherPassParameters->SceneTextures = SceneTextureParams;
			GatherPassParameters->RWHashTable = HashTableUAV;
			GatherPassParameters->HashTableSize = HashTableSize;
			GatherPassParameters->ActiveProbes = GraphBuilder.CreateUAV(ActiveProbe);
			GatherPassParameters->ActiveCounter = ActiveProbeCountUAV;
			GatherPassParameters->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);
			GatherPassParameters->Output = Output;

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
			auto PreviousActiveProbes = ActiveProbe; // Init to one from cascade 0
			auto PreviousActiveProbeCount = ActiveProbeCount;
			for (int i = 1; i < Cascades; ++i)
			{
				FWorldSpaceRCFillCascade::FParameters* FillCascadeParams = GraphBuilder.AllocParameters<
					FWorldSpaceRCFillCascade::FParameters>();

				FillCascadeParams->View = ViewInfo.ViewUniformBuffer;
				FillCascadeParams->SceneTextures = SceneTextureParams;
				FillCascadeParams->RWHashTable = HashTableUAV;
				FillCascadeParams->Cascade = i;
				FillCascadeParams->HashTableSize = HashTableSize;
				FillCascadeParams->PrevActiveProbes = GraphBuilder.CreateSRV(PreviousActiveProbes);
				FillCascadeParams->PrevActiveCounter = GraphBuilder.CreateSRV(PreviousActiveProbeCount);

				auto NextProbes = GraphBuilder.RegisterExternalBuffer(ActiveProbes[i]);
				auto NextProbeCount = GraphBuilder.RegisterExternalBuffer(ActiveProbeCounters[i]);
				auto CounterUAV = GraphBuilder.CreateUAV(NextProbeCount);
				AddClearUAVPass(GraphBuilder, CounterUAV, 0);

				FillCascadeParams->ActiveProbes = GraphBuilder.CreateUAV(NextProbes);
				FillCascadeParams->ActiveCounter = CounterUAV;

				FillCascadeParams->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);

				TShaderMapRef<FWorldSpaceRCFillCascade> GatherShader(GlobalShaderMap);

				auto IndirectBuffer =FComputeShaderUtils::AddIndirectArgsSetupCsPass1D(GraphBuilder, ViewInfo.GetFeatureLevel(), PreviousActiveProbeCount, TEXT("RC Indirect Args"), FWorldSpaceRCFillCascade::GroupCount);
				FillCascadeParams->IndirectArgsBuffer = IndirectBuffer;

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("RC Fill Cascade %d", i),
					GatherShader,
					FillCascadeParams,
					IndirectBuffer,0);

				PreviousActiveProbes = NextProbes;
				PreviousActiveProbeCount = NextProbeCount;
			}
		}
		//Trace the scene per pixel and split into probes
		{
			

		TShaderMapRef<FWorldSpaceRCRaygen> RayGenShader(GlobalShaderMap);
		FWorldSpaceRCRaygen::FParameters* RGParams = GraphBuilder.AllocParameters<FWorldSpaceRCRaygen::FParameters>();
		RGParams->Output = Output;
		RGParams->HashTableSize = HashTableSize;
		RGParams->HashTable = HashTableSRV;
		RGParams->TotalRadiance = RadianceUAV;
		RGParams->Weights = WeightUAV;
		RGParams->TLAS = ViewInfo.GetRayTracingSceneLayerViewChecked(ERayTracingSceneLayer::Base);
		RGParams->BaseDirections = DirectionCount;
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
		{
			
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
		ApplyPassParameters->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);
		ApplyPassParameters->BaseDirections = DirectionCount;
		ApplyPassParameters->ProbeRadiance = GraphBuilder.CreateSRV(RadianceBuffer);
		ApplyPassParameters->ProbeWeights = GraphBuilder.CreateSRV(WeightBuffer);
		ApplyPassParameters->Output = Output;
		ApplyPassParameters->DisplayCascade = RC::CVarDisplayCascade.GetValueOnAnyThread();

		TShaderMapRef<FWorldSpaceRCApply> ApplyCS(GlobalShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("RC Applying Output pass %dx%d", PassViewSize.X, PassViewSize.Y),
			ApplyCS,
			ApplyPassParameters,
			GroupCount);
	}
}
