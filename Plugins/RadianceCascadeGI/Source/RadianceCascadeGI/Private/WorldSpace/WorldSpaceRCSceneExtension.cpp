#include "WorldSpace/WorldSpaceRCSceneExtension.h"

#include "IContentBrowserSingleton.h"
#include "RenderTargetPool.h"
#include "SceneRendering.h"
#include "SceneTextureParameters.h"

#include "Core/RCVars.h"

IMPLEMENT_GLOBAL_SHADER(FWorldSpaceRCShader, "/Plugins/RadianceCascadeGI/WorldSpace/WorldSpaceRC.usf", "MainCS", SF_Compute);

FWorldSpaceRCSceneExtension::FWorldSpaceRCSceneExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister)
{
	UE_LOG(LogTemp, Log, TEXT("SceneViewExtensionTemplate: Custom SceneViewExtension registered"));
	ResetCommand = MakeUnique<FAutoConsoleCommand>(
		TEXT("r.RC.ResetTable"),
		TEXT("Reset hashtable."),
		FConsoleCommandDelegate::CreateLambda([this]()
			{
				bResetTable.store(true, std::memory_order_release);
			}));
}

void FWorldSpaceRCSceneExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	// Define to what Post Processing stage to hook the SceneViewExtension into. See SceneViewExtension.h and PostProcessing.cpp for more info
	if (PassId == EPostProcessingPass::BeforeDOF)
	{
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FWorldSpaceRCSceneExtension::CustomPostProcessing));
	}


}

static constexpr uint32 HashTableSize = 4096;

FScreenPassTexture FWorldSpaceRCSceneExtension::CustomPostProcessing(FRDGBuilder& GraphBuilder, const FSceneView& SceneView, const FPostProcessMaterialInputs& Inputs)
{

	// SceneViewExtension gives SceneView, not ViewInfo so we need to setup some basics
	const FSceneViewFamily& ViewFamily = *SceneView.Family;

	const FScreenPassTexture& SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid() || RC::CVarScreenspaceEnabled.GetValueOnRenderThread() == 0)
	{
		return SceneColor;
	}
	
	auto SceneDesc = SceneColor.Texture->Desc;
	const FScreenPassTextureViewport SceneColorViewport(SceneColor);

	// Accesspoint to our Shaders
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ViewFamily.GetFeatureLevel());

	check(SceneView.bIsViewInfo);
	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(SceneView);

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	if (!bInitialized)
	{
		//Create Hashmaps for probes
		bInitialized = true;
		FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint64_t), HashTableSize);
		AllocatePooledBuffer(Desc, CascadeHashTable, TEXT("RC Cascade Hashmap"));
	}
		
	RDG_EVENT_SCOPE(GraphBuilder, "Screen Space RC");
	{
		auto SceneTextureParams = CreateSceneTextureShaderParameters(GraphBuilder, &ViewInfo.GetSceneTextures(), ERHIFeatureLevel::SM5);


		// Setup all the descriptors to create a target texture
		FRDGTextureDesc OutputDesc;
		{
			OutputDesc = SceneColor.Texture->Desc;

			OutputDesc.Reset();
			OutputDesc.Flags |= TexCreate_UAV;
			OutputDesc.Flags &= ~(TexCreate_RenderTargetable | TexCreate_FastVRAM);

			FLinearColor ClearColor(0., 0., 0., 0.);
			OutputDesc.ClearValue = FClearValueBinding(ClearColor);
		}
		//Clear hash
		auto HashTable = GraphBuilder.RegisterExternalBuffer(CascadeHashTable);
		auto HashTableUAV = GraphBuilder.CreateUAV(HashTable);
		AddClearUAVPass(GraphBuilder, HashTableUAV, 0);

		// Create target texture
		FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("World space RC Output Texture"));

		// Set the shader parameters
		FWorldSpaceRCShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FWorldSpaceRCShader::FParameters>();

		// Input is the SceneColor from PostProcess Material Inputs
		PassParameters->View = ViewInfo.ViewUniformBuffer;
		PassParameters->SceneTextures = SceneTextureParams;
		
		PassParameters->HashTable = GraphBuilder.CreateSRV(HashTable);
		PassParameters->RWHashTable = HashTableUAV;

		PassParameters->HashTableSize = HashTableSize;

		// Use ScreenPassTextureViewportParameters so we don't need to calculate these ourselves
		PassParameters->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);

		FIntPoint PassViewSize = SceneColor.ViewRect.Size();

		// Create UAV from Target Texture
		PassParameters->Output = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputTexture));


		// Set Compute Shader and execute
		FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(PassViewSize, FComputeShaderUtils::kGolden2DGroupSize);

		TShaderMapRef<FWorldSpaceRCShader> ComputeShader(GlobalShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("World Space RC Output pass %dx%d", PassViewSize.X, PassViewSize.Y),
			ComputeShader,
			PassParameters,
			GroupCount);


		// Copy the output texture back to SceneColor
		// Returning the new texture as ScreenPassTexture doesn't work, so this is pretty fast alternative
		// Also with f.ex 'PrePostProcessPass_RenderThread' you get only input and something similar needs to be implemented then
		AddCopyTexturePass(GraphBuilder, OutputTexture, SceneColor.Texture);
	}

	// The call expects ScreenPassTexture as a return, we return with the same texture as we started with, see AddCopyTexturePass above 
	return SceneColor;
}
