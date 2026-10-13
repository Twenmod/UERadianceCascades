#pragma once

#include "CoreMinimal.h"
#include "RenderGraphUtils.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "DeferredShadingRenderer.h"

class FWorldSpaceRCGi
{
public:
	FWorldSpaceRCGi();

	void PrepareRayTracing(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	void RenderDiffuseIndirectLight(const FScene& Scene,const FViewInfo& ViewInfo, FRDGBuilder& GraphBuilder, FGlobalIlluminationPluginResources& Resources);

	bool bInitialized = false;
	TRefCountPtr<FRDGPooledBuffer> HashTableKeys;
	TRefCountPtr<FRDGPooledBuffer> HashTableLastOccupied;
	TRefCountPtr<FRDGPooledBuffer> ProbeRadianceBuffer;
	TRefCountPtr<FRDGPooledBuffer> ProbeWeightBuffer;
	TRefCountPtr<IPooledRenderTarget> FinalIrradianceAtlas;
	TRefCountPtr<FRDGPooledBuffer> BinAgeBuffer;
	TRefCountPtr<FRDGPooledBuffer> ProbeDepositCountBuffer;
	TRefCountPtr<FRDGPooledBuffer> SecondaryHitPositionBuffer;
	TRefCountPtr<FRDGPooledBuffer> SecondaryHitNormalBuffer;
};
