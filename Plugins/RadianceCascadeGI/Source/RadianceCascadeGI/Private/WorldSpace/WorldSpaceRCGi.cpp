#include "WorldSpace/WorldSpaceRCGi.h"

#include "IContentBrowserSingleton.h"
#include "RenderTargetPool.h"
#include "SceneRendering.h"
#include "SceneTextureParameters.h"
#include "Core/RCLog.h"

#include "Core/RCVars.h"

IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCShader, "/Plugins/RadianceCascadeGI/WorldSpace/WorldSpaceRC.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCRaygen, "/Plugins/RadianceCascadeGI/WorldSpace/RayGen.usf", "MainRG", SF_RayGen);



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
	if (RC::CVarScreenspaceEnabled.GetValueOnRenderThread() == 0)
	{
		return;
	}

	// Accesspoint to our Shaders
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ViewInfo.GetFeatureLevel());

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	if (!bInitialized)
	{
		//Create Hashmaps for probes
		bInitialized = true;
		FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint64_t), HashTableSize);
		AllocatePooledBuffer(Desc, CascadeHashTable, TEXT("RC Cascade Hashmap"));
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
		FIntPoint PassViewSize = SceneColor.ViewRect.Size();

		//// Set the shader parameters
		//FWorldSpaceRCShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FWorldSpaceRCShader::FParameters>();

		//// Input is the SceneColor from PostProcess Material Inputs
		//PassParameters->View = ViewInfo.ViewUniformBuffer;
		//PassParameters->SceneTextures = SceneTextureParams;
		//
		//PassParameters->HashTable = GraphBuilder.CreateSRV(HashTable);
		//PassParameters->RWHashTable = HashTableUAV;
		//PassParameters->TLAS = ViewInfo.GetRayTracingSceneLayerViewChecked(ERayTracingSceneLayer::Base);
		//PassParameters->HashTableSize = HashTableSize;

		//// Use ScreenPassTextureViewportParameters so we don't need to calculate these ourselves
		//PassParameters->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);


		//// Create UAV from Target Texture
		//PassParameters->Output =


		//// Set Compute Shader and execute
		//FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(PassViewSize, FComputeShaderUtils::kGolden2DGroupSize);

		//TShaderMapRef<FWorldSpaceRCShader> ComputeShader(GlobalShaderMap);

		//FComputeShaderUtils::AddPass(
		//	GraphBuilder,
		//	RDG_EVENT_NAME("World Space RC Output pass %dx%d", PassViewSize.X, PassViewSize.Y),
		//	ComputeShader,
		//	PassParameters,
		//	GroupCount);


		//Trace the scene
		TShaderMapRef<FWorldSpaceRCRaygen> RayGenShader(GlobalShaderMap);
		FWorldSpaceRCRaygen::FParameters* RGParams = GraphBuilder.AllocParameters<FWorldSpaceRCRaygen::FParameters>();
		RGParams->Output = Output;
		//RGParams->HashTableSize = HashTableSize;
		//RGParams->RWHashTable = HashTableUAV;
		RGParams->TLAS = ViewInfo.GetRayTracingSceneLayerViewChecked(ERayTracingSceneLayer::Base);
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
}
