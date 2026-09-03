// Custom SceneViewExtension Template for Unreal Engine
// Copyright 2023 - 2025 Ossi Luoto
// 
// Custom SceneViewExtension implementation

#include "ScreenSpaceRCSceneExtension.h"

#include "RenderTargetPool.h"

IMPLEMENT_GLOBAL_SHADER(FScreenSpaceRCOutputShader, "/Plugins/SceneViewExtensionTemplate/PostProcessCS.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FScreenSpaceRCMarchShader, "/Plugins/SceneViewExtensionTemplate/ScreenSpaceMarch.usf", "MainCS", SF_Compute);

namespace
{
	TAutoConsoleVariable<int32> CVarShaderOn(
		TEXT("r.ScreenSpaceRC"),
		1,
		TEXT("Enable Screen Space RC \n")
		TEXT(" 0: OFF;")
		TEXT(" 1: ON."),
		ECVF_RenderThreadSafe);
}


FScreenSpaceRCSceneExtension::FScreenSpaceRCSceneExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister)
{
	UE_LOG(LogTemp, Log, TEXT("SceneViewExtensionTemplate: Custom SceneViewExtension registered"));
}

void FScreenSpaceRCSceneExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	// Define to what Post Processing stage to hook the SceneViewExtension into. See SceneViewExtension.h and PostProcessing.cpp for more info
	if (PassId == EPostProcessingPass::MotionBlur)
	{
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FScreenSpaceRCSceneExtension::CustomPostProcessing));
	}
}


FScreenPassTexture FScreenSpaceRCSceneExtension::CustomPostProcessing(FRDGBuilder& GraphBuilder, const FSceneView& SceneView, const FPostProcessMaterialInputs& Inputs)
{



	// SceneViewExtension gives SceneView, not ViewInfo so we need to setup some basics
	const FSceneViewFamily& ViewFamily = *SceneView.Family;

	const FScreenPassTexture& SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid() || CVarShaderOn.GetValueOnRenderThread() == 0)
	{
		return SceneColor;
	}

	const FScreenPassTextureViewport SceneColorViewport(SceneColor);

	//Initialize resources if not there
	if (!bInitialized)
	{
		auto SceneDesc = SceneColor.Texture->Desc;

		//Initialize Cascade textures
		for (int i = 0; i < CascadeCount; ++i)
		{
			//Lower res
			FIntPoint Resolution = SceneDesc.Extent / 2;

			FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DArrayDesc(
				Resolution,
				PF_FloatRGBA,
				FClearValueBinding::Green,
				TexCreate_None,
				TexCreate_ShaderResource | TexCreate_UAV,
				false,
				CascadeCount);
			GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, ProbeCascadesTexArray, TEXT("RC Cascade " + i));
		}
		bInitialized = true;
	}

	// Here starts the RDG stuff
	RDG_EVENT_SCOPE(GraphBuilder, "Screen Space RC");
	{

		// Accesspoint to our Shaders
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ViewFamily.GetFeatureLevel());

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

		FRDGTextureRef ProbeCascadeTexture = GraphBuilder.RegisterExternalTexture(ProbeCascadesTexArray, ERDGTextureFlags::None);

		auto ProbeUAV = GraphBuilder.CreateUAV(ProbeCascadeTexture);
		//TODO: correct size
		FIntPoint MarchPassViewSize = SceneColor.ViewRect.Size() / 2;

		FScreenSpaceRCMarchShader::FParameters* MarchParameters = GraphBuilder.AllocParameters<FScreenSpaceRCMarchShader::FParameters>();
		MarchParameters->ProbeCascades = ProbeUAV;
		MarchParameters->OriginalSceneColor = SceneColor.Texture;
		MarchParameters->Resolution = FUint32Vector2(MarchPassViewSize.X, MarchPassViewSize.Y);

		TShaderMapRef<FScreenSpaceRCMarchShader> MarchShader(GlobalShaderMap);
		FIntVector MarchGroupCount = FComputeShaderUtils::GetGroupCount(MarchPassViewSize, FComputeShaderUtils::kGolden2DGroupSize);

		//Marching pass Fills Probe
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Screen Space RC Marching pass %dx%d", MarchPassViewSize.X, MarchPassViewSize.Y),
			MarchShader,
			MarchParameters,
			MarchGroupCount);


		// Create target texture
		FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("Screen space RC Output Texture"));

		// Set the shader parameters
		FScreenSpaceRCOutputShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenSpaceRCOutputShader::FParameters>();

		// Input is the SceneColor from PostProcess Material Inputs
		PassParameters->OriginalSceneColor = SceneColor.Texture;
		PassParameters->ProbeCascades = GraphBuilder.RegisterExternalTexture(ProbeCascadesTexArray, ERDGTextureFlags::None);

		// Use ScreenPassTextureViewportParameters so we don't need to calculate these ourselves
		PassParameters->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);

		FIntPoint PassViewSize = SceneColor.ViewRect.Size();

		// Create UAV from Target Texture
		PassParameters->Output = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputTexture));


		// Set Compute Shader and execute
		FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(PassViewSize, FComputeShaderUtils::kGolden2DGroupSize);

		TShaderMapRef<FScreenSpaceRCOutputShader> ComputeShader(GlobalShaderMap);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Screen Space RC Output pass %dx%d", PassViewSize.X, PassViewSize.Y),
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
