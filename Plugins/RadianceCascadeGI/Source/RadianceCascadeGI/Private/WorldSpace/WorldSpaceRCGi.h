#pragma once

#include "CoreMinimal.h"
#include "RenderGraphUtils.h"
#include "SceneViewExtension.h"
#include "PostProcess/PostProcessMaterial.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "BlueNoise.h"
#include "ShaderCompilerCore.h"
#include "DeferredShadingRenderer.h"
#include "RayTracingPayloadType.h"
#include "RayTracingShaderBindingLayout.h"
#include "RayTracing/RayTracingLighting.h"
//static constexpr int MAX_CASCADES = 32;

class FWorldSpaceRCGi
{
public:
	FWorldSpaceRCGi();

	void PrepareRayTracing(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders);
	void RenderDiffuseIndirectLight(const FScene& Scene,const FViewInfo& ViewInfo, FRDGBuilder& GraphBuilder, FGlobalIlluminationPluginResources& Resources);

	bool bInitialized = false;
	std::atomic<bool> bResetTable{ false };
	TUniquePtr<FAutoConsoleCommand> ResetCommand;
	TRefCountPtr<FRDGPooledBuffer> CascadeHashTable;
	TRefCountPtr<FRDGPooledBuffer> ProbeRadianceBuffer;
	TRefCountPtr<FRDGPooledBuffer> ProbeWeightBuffer;
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



class FWorldSpaceRCRaygen : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FWorldSpaceRCRaygen)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FWorldSpaceRCRaygen, FGlobalShader)
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepth)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_RDG_BUFFER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FRayTracingLightGrid, RaytracingLightGridData)
		SHADER_PARAMETER(uint32, HashTableSize)
		SHADER_PARAMETER(uint32, DirectionCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint64_t>, HashTable)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint4>, TotalRadiance)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, Weights)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FNaniteRayTracingUniformParameters, NaniteRayTracing)
	END_SHADER_PARAMETER_STRUCT()


	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Params)
	{
		return ShouldCompileRayTracingShadersForProject(Params.Platform);
	}

	// REQUIRED. This is what binds you to the scene's material hit groups.
	static ERayTracingPayloadType GetRayTracingPayloadType(const int32 PermutationId)
	{
		return ERayTracingPayloadType::RayTracingMaterial;
	}

	static const FShaderBindingLayout* GetShaderBindingLayout(const FShaderPermutationParameters& Parameters)
	{
		return RayTracing::GetShaderBindingLayout(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Params,
		FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Params, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("ENABLE_MATERIALS"), 1);
		OutEnvironment.SetDefine(TEXT("ENABLE_TWO_SIDED_GEOMETRY"), 1);
		OutEnvironment.SetDefine(TEXT("SUPPORT_CONTACT_SHADOWS"), 1);
	}
};

class RADIANCECASCADEGI_API FWorldSpaceRCApplyShader : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FWorldSpaceRCApplyShader)

	SHADER_USE_PARAMETER_STRUCT(FWorldSpaceRCApplyShader, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, SceneColorViewport)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint64_t>, HashTable)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, ProbeRadiance)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ProbeWeights)
		SHADER_PARAMETER(uint32, HashTableSize)
		SHADER_PARAMETER(uint32, DirectionCount)

	END_SHADER_PARAMETER_STRUCT()

	// Basic shader initialization
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	// Define environment variables used by compute shader
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADS_X"), 8);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), 8);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), 1);
	}
};


