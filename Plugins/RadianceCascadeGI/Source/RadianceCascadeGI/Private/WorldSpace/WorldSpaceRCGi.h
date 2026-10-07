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
	std::atomic<bool> bResetTable{ false };
	TUniquePtr<FAutoConsoleCommand> ResetCommand;
	TRefCountPtr<FRDGPooledBuffer> HashTableKeys;
	TRefCountPtr<FRDGPooledBuffer> HashTableLastOccupied;
	TRefCountPtr<FRDGPooledBuffer> ProbeRadianceBuffer;
	TRefCountPtr<FRDGPooledBuffer> ProbeWeightBuffer;
	TRefCountPtr<FRDGPooledBuffer> ProbeDepositCountBuffer;
};
