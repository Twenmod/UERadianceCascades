#pragma once

#include "CoreMinimal.h"
#include "RenderGraphUtils.h"
#include "SceneViewExtension.h"
#include "PostProcess/PostProcessMaterial.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "BlueNoise.h"
#include "ShaderCompilerCore.h"
#include "DeferredShadingRenderer.h"

//static constexpr int MAX_CASCADES = 32;

class FWorldSpaceRCGi
{
public:
	FWorldSpaceRCGi();

	void AnyRayTracingPassEnabled(bool& bOutAnyRayTracingPassEnabled) {};
	void PrepareRayTracing(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders) {};
	void RenderDiffuseIndirectLight(const FScene& Scene,const FViewInfo& ViewInfo, FRDGBuilder& GraphBuilder, FGlobalIlluminationPluginResources& Resources);

	bool bInitialized = false;
	std::atomic<bool> bResetTable{ false };
	TUniquePtr<FAutoConsoleCommand> ResetCommand;
	TRefCountPtr<FRDGPooledBuffer> CascadeHashTable;
	//TArray<TRefCountPtr<IPooledRenderTarget >> ProbeCascadeSliceMasks;
	//TRefCountPtr<FRDGPooledBuffer> BitWeightLUTBuffer;

	//FInt32Point CurrentResolution = FInt32Point(0, 0);
	//uint32 CurrentTileSize = 0;

};

class RADIANCECASCADEGI_API FWorldSpaceRCShader : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FWorldSpaceRCShader)

	SHADER_USE_PARAMETER_STRUCT(FWorldSpaceRCShader, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, SceneColorViewport)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint64_t>, HashTable)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint64_t>, RWHashTable)
		SHADER_PARAMETER_RDG_BUFFER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER(uint32, HashTableSize)
	END_SHADER_PARAMETER_STRUCT()

	// Basic shader initialization
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && ShouldCompileRayTracingShadersForProject(Parameters.Platform)
			&& FDataDrivenShaderPlatformInfo::GetSupportsInlineRayTracing(Parameters.Platform);
	}

	// Define environment variables used by compute shader
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADS_X"), 8);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), 8);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_InlineRayTracing);
	}
};

