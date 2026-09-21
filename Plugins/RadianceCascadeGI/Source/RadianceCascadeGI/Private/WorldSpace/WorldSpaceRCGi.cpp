#include "WorldSpace/WorldSpaceRCGi.h"

#include "IContentBrowserSingleton.h"
#include "RenderTargetPool.h"
#include "SceneRendering.h"
#include "SceneTextureParameters.h"
#include "Core/RCLog.h"

#include "Core/RCVars.h"

IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCShader, "/Plugins/RadianceCascadeGI/WorldSpace/WorldSpaceRC.usf", "MainCS", SF_Compute);

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

void FWorldSpaceRCGi::RenderDiffuseIndirectLight(const FScene& Scene, const FViewInfo& ViewInfo, FRDGBuilder& GraphBuilder, FGlobalIlluminationPluginResources& Resources)
{
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

		//Clear hash
		auto HashTable = GraphBuilder.RegisterExternalBuffer(CascadeHashTable);
		auto HashTableUAV = GraphBuilder.CreateUAV(HashTable);
		AddClearUAVPass(GraphBuilder, HashTableUAV, 0);


		// Set the shader parameters
		FWorldSpaceRCShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FWorldSpaceRCShader::FParameters>();

		// Input is the SceneColor from PostProcess Material Inputs
		PassParameters->View = ViewInfo.ViewUniformBuffer;
		PassParameters->SceneTextures = SceneTextureParams;
		
		PassParameters->HashTable = GraphBuilder.CreateSRV(HashTable);
		PassParameters->RWHashTable = HashTableUAV;
		PassParameters->TLAS = ViewInfo.GetRayTracingSceneLayerViewChecked(ERayTracingSceneLayer::Base);
		PassParameters->HashTableSize = HashTableSize;

		// Use ScreenPassTextureViewportParameters so we don't need to calculate these ourselves
		PassParameters->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);

		FIntPoint PassViewSize = SceneColor.ViewRect.Size();

		// Create UAV from Target Texture
		PassParameters->Output = GraphBuilder.CreateUAV(Resources.SceneColor);


		// Set Compute Shader and execute
		FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(PassViewSize, FComputeShaderUtils::kGolden2DGroupSize);

		TShaderMapRef<FWorldSpaceRCShader> ComputeShader(GlobalShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("World Space RC Output pass %dx%d", PassViewSize.X, PassViewSize.Y),
			ComputeShader,
			PassParameters,
			GroupCount);


	}
}
