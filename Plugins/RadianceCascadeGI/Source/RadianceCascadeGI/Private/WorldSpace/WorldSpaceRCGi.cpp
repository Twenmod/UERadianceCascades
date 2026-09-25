#include "WorldSpace/WorldSpaceRCGi.h"

#include "IContentBrowserSingleton.h"
#include "RenderTargetPool.h"
#include "SceneRendering.h"
#include "SceneTextureParameters.h"
#include "Core/RCLog.h"

#include "Core/RCVars.h"

IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCShader, "/Plugins/RadianceCascadeGI/WorldSpace/WorldSpaceRC.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCRaygen, "/Plugins/RadianceCascadeGI/WorldSpace/RayGen.usf", "MainRG", SF_RayGen);
IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCApplyShader, "/Plugins/RadianceCascadeGI/WorldSpace/WorldSpaceApply.usf", "MainCS", SF_Compute);



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
static constexpr uint32 DirectionCount = 16;

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

void FWorldSpaceRCGi::RenderDiffuseIndirectLight(const FScene& Scene, const FViewInfo& ViewInfo, FRDGBuilder& GraphBuilder, FGlobalIlluminationPluginResources& Resources)
{
	// Accesspoint to our Shaders
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ViewInfo.GetFeatureLevel());

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	if (!bInitialized)
	{
		//Create Hashmaps for probes
		bInitialized = true;
		FRDGBufferDesc HTDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint64_t), HashTableSize);
		AllocatePooledBuffer(HTDesc, CascadeHashTable, TEXT("RC Cascade Hashmap"));
		FRDGBufferDesc ProbeRadBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32)*4, HashTableSize*(DirectionCount*2*DirectionCount));
		AllocatePooledBuffer(ProbeRadBufferDesc, ProbeRadianceBuffer, TEXT("RC Cascade Probes Total Radiance and transmittance"));
		FRDGBufferDesc ProbeWeightBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), HashTableSize* (DirectionCount * 2 * DirectionCount));
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
		auto SceneTextureParams = CreateSceneTextureShaderParameters(GraphBuilder, &ViewInfo.GetSceneTextures(), ERHIFeatureLevel::SM5);
		auto Output = GraphBuilder.CreateUAV(Resources.SceneColor);
		//Clear hash
		auto HashTable = GraphBuilder.RegisterExternalBuffer(CascadeHashTable);
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

		FIntPoint PassViewSize = SceneColor.ViewRect.Size();


		// Set the shader parameters
		FWorldSpaceRCShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FWorldSpaceRCShader::FParameters>();

		// Input is the SceneColor from PostProcess Material Inputs
		PassParameters->View = ViewInfo.ViewUniformBuffer;
		PassParameters->SceneTextures = SceneTextureParams;
		
		PassParameters->HashTable = HashTableSRV;
		PassParameters->RWHashTable = HashTableUAV;
		PassParameters->HashTableSize = HashTableSize;

		// Use ScreenPassTextureViewportParameters so we don't need to calculate these ourselves
		PassParameters->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);

		// Create UAV from Target Texture
		PassParameters->Output = Output;


		// Set Compute Shader and execute
		FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(PassViewSize, FComputeShaderUtils::kGolden2DGroupSize);

		TShaderMapRef<FWorldSpaceRCShader> ComputeShader(GlobalShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("RC Gather pass %dx%d", PassViewSize.X, PassViewSize.Y),
			ComputeShader,
			PassParameters,
			GroupCount);


		//Trace the scene per pixel and split into probes

		TShaderMapRef<FWorldSpaceRCRaygen> RayGenShader(GlobalShaderMap);
		FWorldSpaceRCRaygen::FParameters* RGParams = GraphBuilder.AllocParameters<FWorldSpaceRCRaygen::FParameters>();
		RGParams->Output = Output;
		RGParams->HashTableSize = HashTableSize;
		RGParams->HashTable = HashTableSRV;
		RGParams->TotalRadiance = RadianceUAV;
		RGParams->Weights = WeightUAV;
		RGParams->TLAS = ViewInfo.GetRayTracingSceneLayerViewChecked(ERayTracingSceneLayer::Base);
		RGParams->DirectionCount = DirectionCount;
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


		//Output
				// Set the shader parameters
		FWorldSpaceRCApplyShader::FParameters* ApplyPassParameters = GraphBuilder.AllocParameters<FWorldSpaceRCApplyShader::FParameters>();

		// Input is the SceneColor from PostProcess Material Inputs
		ApplyPassParameters->View = ViewInfo.ViewUniformBuffer;
		ApplyPassParameters->SceneTextures = SceneTextureParams;
		ApplyPassParameters->HashTable = HashTableSRV;
		ApplyPassParameters->HashTableSize = HashTableSize;
		ApplyPassParameters->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);
		ApplyPassParameters->DirectionCount = DirectionCount;
		ApplyPassParameters->ProbeRadiance = GraphBuilder.CreateSRV(RadianceBuffer);
		ApplyPassParameters->ProbeWeights = GraphBuilder.CreateSRV(WeightBuffer);
		ApplyPassParameters->Output = Output;

		TShaderMapRef<FWorldSpaceRCApplyShader> ApplyCS(GlobalShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("RC Applying Output pass %dx%d", PassViewSize.X, PassViewSize.Y),
			ApplyCS,
			ApplyPassParameters,
			GroupCount);

	}
}
