
#include "ScreenSpaceRCSceneExtension.h"

#include "IContentBrowserSingleton.h"
#include "RenderTargetPool.h"
#include "SceneRendering.h"
#include "SceneTextureParameters.h"

#include "Core/RCVars.h"

IMPLEMENT_GLOBAL_SHADER(FScreenSpaceRCOutputShader, "/Plugins/SceneViewExtensionTemplate/PostProcessCS.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FScreenSpaceRCMarchShader, "/Plugins/SceneViewExtensionTemplate/ScreenSpaceMarch.usf", "MainCS", SF_Compute);



FScreenSpaceRCSceneExtension::FScreenSpaceRCSceneExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister)
{
	UE_LOG(LogTemp, Log, TEXT("SceneViewExtensionTemplate: Custom SceneViewExtension registered"));
}

void FScreenSpaceRCSceneExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	// Define to what Post Processing stage to hook the SceneViewExtension into. See SceneViewExtension.h and PostProcessing.cpp for more info
	if (PassId == EPostProcessingPass::BeforeDOF)
	{
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FScreenSpaceRCSceneExtension::CustomPostProcessing));
	}
}


FScreenPassTexture FScreenSpaceRCSceneExtension::CustomPostProcessing(FRDGBuilder& GraphBuilder, const FSceneView& SceneView, const FPostProcessMaterialInputs& Inputs)
{

	// SceneViewExtension gives SceneView, not ViewInfo so we need to setup some basics
	const FSceneViewFamily& ViewFamily = *SceneView.Family;


	const FScreenPassTexture& SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

	if (!SceneColor.IsValid() || RC::CVarScreenspaceEnabled.GetValueOnRenderThread() == 0)
	{
		return SceneColor;
	}

	const FScreenPassTextureViewport SceneColorViewport(SceneColor);
	auto SceneDesc = SceneColor.Texture->Desc;

	uint32 TileSize = static_cast<uint32>(RC::CVarTileSize->GetInt());

	//Get viewports
	const FIntPoint SceneColorExtent = SceneColor.Texture->Desc.Extent;
	const FIntPoint ViewSize = SceneColor.ViewRect.Size();
	const FIntPoint CascadeExtent = FIntPoint::DivideAndRoundUp(SceneColorExtent, (int32)TileSize);
	const FIntPoint CascadeViewSize = FIntPoint::DivideAndRoundUp(ViewSize, (int32)TileSize);

	//Initialize resources if not there
	if (!bInitialized || CurrentResolution != CascadeExtent || CurrentTileSize != TileSize)
	{
		CurrentResolution = CascadeExtent;
		CurrentTileSize = TileSize;
		RDG_EVENT_SCOPE(GraphBuilder, "Screen Space RC Initialization");
		{
			//Initialize Cascade textures
				//Lower res
			FIntPoint Resolution = CurrentResolution;
			FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
				Resolution,
				PF_FloatRGBA,
				FClearValueBinding::Black,
				TexCreate_None,
				TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable,
				false);
			FPooledRenderTargetDesc MaskDesc = FPooledRenderTargetDesc::Create2DDesc(
				Resolution,
				PF_R32_UINT,
				FClearValueBinding::Black,
				TexCreate_None,
				TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable,
				false);
			ProbeCascadeArray.SetNum(MAX_CASCADES);
			ProbeCascadeSliceMasks.SetNum(MAX_CASCADES);
			for (int i = 0; i < MAX_CASCADES; ++i)
			{
				//Probe color ray data
				GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, ProbeCascadeArray[i], TEXT("RC Cascades"));
				//Probe Depth slice Occlusion bits
				GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, MaskDesc, ProbeCascadeSliceMasks[i], TEXT("RC Cascades Slice Mask"));
			}

			//Lut buffer
			static TArray<float> LutData;
			LutData.SetNumZeroed(1024);
			for (uint32 Slice = 0; Slice < 4; ++Slice)
			{
				for (uint32 Byte = 0; Byte < 256; ++Byte)
				{
					float W = 0.0f;
					for (uint32 Bit = 0; Bit < 8; ++Bit)
					{
						if (Byte & (1u << Bit))
						{
							const uint32 j = Slice * 8 + Bit;
							W += FMath::Sin((float(j) + 0.5f) * (UE_PI / 32.0f));
						}
					}
					LutData[Slice * 256 + Byte] = W;
				}
			}
			const FRDGBufferDesc LutDesc =
				FRDGBufferDesc::CreateBufferDesc(sizeof(float), LutData.Num());

			BitWeightLUTBuffer = AllocatePooledBuffer(LutDesc, TEXT("RC BitWeightLUT"));


			FRDGBufferRef LutRDG = GraphBuilder.RegisterExternalBuffer(BitWeightLUTBuffer);
			GraphBuilder.QueueBufferUpload(
				LutRDG,
				LutData.GetData(),
				LutData.Num() * sizeof(float),
				ERDGInitialDataFlags::NoCopy);
		}
		bInitialized = true;
	}
	// Accesspoint to our Shaders
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ViewFamily.GetFeatureLevel());

	check(SceneView.bIsViewInfo);
	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(SceneView);

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	RDG_EVENT_SCOPE(GraphBuilder, "Screen Space RC");
	{

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


		FIntPoint MarchPassViewSize = SceneDesc.Extent / TileSize;


		//Marching pass Fills Probe

		FRDGBufferRef BitWeightLut = GraphBuilder.RegisterExternalBuffer(BitWeightLUTBuffer);
		auto BitWeightLUTSRV = GraphBuilder.CreateSRV(BitWeightLut, PF_R32_FLOAT);
		int BaseRayCount = RC::CVarRayCount->GetInt();

		const FScreenPassTextureViewport CascadeViewport(
			CascadeExtent, FIntRect(FIntPoint::ZeroValue, CascadeViewSize));

		const float Diagonal = FMath::Sqrt(float(CascadeViewSize.X) * CascadeViewSize.X +
			float(CascadeViewSize.Y) * CascadeViewSize.Y);
		int CascadeCount = FMath::CeilToInt(FMath::Loge(Diagonal) / FMath::Loge((float)BaseRayCount)) + 1;
		CascadeCount = FMath::Clamp(CascadeCount, 1, MAX_CASCADES);

		FIntVector MarchGroupCount = FComputeShaderUtils::GetGroupCount(
			CascadeViewSize, FComputeShaderUtils::kGolden2DGroupSize);

		TShaderMapRef<FScreenSpaceRCMarchShader> MarchShader(GlobalShaderMap);
		auto DepthTexture = ViewInfo.GetSceneTextures().Depth.Resolve;
		auto SceneTextureParams = CreateSceneTextureShaderParameters(GraphBuilder, &ViewInfo.GetSceneTextures(), ERHIFeatureLevel::SM5);
		FScreenPassTextureViewport TraceViewport(DepthTexture, ViewInfo.ViewRect);
		FRDGTextureRef PreviousTexture = SystemTextures.Black;
		FRDGTextureRef PreviousMaskTexture = SystemTextures.Black;
		int Final = RC::CVarDisplayCascade->GetInt();

		for (int i = CascadeCount - 1; i >= Final; --i)
		{
			FRDGTextureRef ProbeCascadeTexture = GraphBuilder.RegisterExternalTexture(ProbeCascadeArray[i], ERDGTextureFlags::None);
			FRDGTextureRef ProbeCascadeMaskTexture = GraphBuilder.RegisterExternalTexture(ProbeCascadeSliceMasks[i], ERDGTextureFlags::None);

			auto ProbeUAV = GraphBuilder.CreateUAV(ProbeCascadeTexture);
			auto ProbeMaskUAV = GraphBuilder.CreateUAV(ProbeCascadeMaskTexture);

			//Clear probe
			AddClearRenderTargetPass(GraphBuilder, ProbeCascadeTexture);
			AddClearRenderTargetPass(GraphBuilder, ProbeCascadeMaskTexture);

			FScreenSpaceRCMarchShader::FParameters* MarchParametersCascade = GraphBuilder.AllocParameters<FScreenSpaceRCMarchShader::FParameters>();
			MarchParametersCascade->View = ViewInfo.ViewUniformBuffer;
			MarchParametersCascade->ProbeCascade = ProbeUAV;
			MarchParametersCascade->PreviousProbeCascade = PreviousTexture;
			MarchParametersCascade->ProbeCascadeMask = ProbeMaskUAV;
			MarchParametersCascade->PreviousProbeCascadeMask = PreviousMaskTexture;
			MarchParametersCascade->OriginalSceneColor = SceneColor.Texture;
			MarchParametersCascade->SceneDepth = DepthTexture;
			MarchParametersCascade->SceneColorViewport = GetScreenPassTextureViewportParameters(SceneColorViewport);
			MarchParametersCascade->TraceViewport = GetScreenPassTextureViewportParameters(TraceViewport);
			MarchParametersCascade->CascadeViewport = GetScreenPassTextureViewportParameters(CascadeViewport);
			MarchParametersCascade->BaseRayCount = BaseRayCount;
			MarchParametersCascade->Cascade = i;
			MarchParametersCascade->CascadeCount = CascadeCount;
			MarchParametersCascade->SceneTextures = SceneTextureParams;
			MarchParametersCascade->IntervalMult = RC::CVarIntervalMult->GetFloat();
			MarchParametersCascade->TileSize = TileSize;
			MarchParametersCascade->WallThickness = RC::CVarWallThickness->GetFloat();
			MarchParametersCascade->BitWeightLUT = BitWeightLUTSRV;
			MarchParametersCascade->BlueNoise = CreateUniformBufferImmediate(
				GetBlueNoiseGlobalParameters(),
				EUniformBufferUsage::UniformBuffer_SingleFrame);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Screen Space RC Pass Cascade %d", i),
				MarchShader,
				MarchParametersCascade,
				MarchGroupCount);

			PreviousTexture = ProbeCascadeTexture;
			PreviousMaskTexture = ProbeCascadeMaskTexture;
		}

		// Create target texture
		FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("Screen space RC Output Texture"));

		// Set the shader parameters
		FScreenSpaceRCOutputShader::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenSpaceRCOutputShader::FParameters>();

		// Input is the SceneColor from PostProcess Material Inputs
		PassParameters->OriginalSceneColor = SceneColor.Texture;
		PassParameters->View = ViewInfo.ViewUniformBuffer;
		PassParameters->SceneTextures = SceneTextureParams;
		PassParameters->ProbeCascade = PreviousTexture;
		PassParameters->SceneDepth = DepthTexture;
		PassParameters->TileSize = TileSize;
		PassParameters->Intensity = RC::CVarIntensity->GetFloat();


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
