#pragma once

#include "CoreMinimal.h"
#include "RenderGraphUtils.h"
#include "SceneViewExtension.h"
#include "PostProcess/PostProcessMaterial.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "BlueNoise.h"

static constexpr int MAX_CASCADES = 32;

class FWorldSpaceRCSceneExtension : public FSceneViewExtensionBase
{
public:
	FWorldSpaceRCSceneExtension(const FAutoRegister& AutoRegister);

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {};
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {};
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {};

	// See SceneViewExtension.h for hooks to different stages of rendering
	// f.ex. PrePostProcessPass_RenderThread happens just when rendering is finished but PostProcessing hasn't started yet

	// This is the method to hook into PostProcessing pass
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass PassId, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled);

	// This is our actual processing function
	FScreenPassTexture CustomPostProcessing(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);

	bool bInitialized = false;

	TArray<TRefCountPtr<IPooledRenderTarget>> ProbeCascadeArray;
	TArray<TRefCountPtr<IPooledRenderTarget >> ProbeCascadeSliceMasks;
	TRefCountPtr<FRDGPooledBuffer> BitWeightLUTBuffer;

	FInt32Point CurrentResolution = FInt32Point(0, 0);
	uint32 CurrentTileSize = 0;

};



