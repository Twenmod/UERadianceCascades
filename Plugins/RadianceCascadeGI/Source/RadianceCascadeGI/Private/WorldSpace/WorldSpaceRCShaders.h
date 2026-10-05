#pragma once

#include "CoreMinimal.h"
#include "RenderGraphUtils.h"
#include "PostProcess/PostProcessMaterial.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "RayTracingPayloadType.h"
#include "RayTracingShaderBindingLayout.h"
#include "RayTracing/RayTracingLighting.h"

class RADIANCECASCADEGI_API FWorldSpaceRCGather : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FWorldSpaceRCGather)

	SHADER_USE_PARAMETER_STRUCT(FWorldSpaceRCGather, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, SceneColorViewport)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint64_t>, RWHashTable)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, ActiveProbes)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, ActiveCounter)
		SHADER_PARAMETER(uint32, HashTableSize)
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

class RADIANCECASCADEGI_API FWorldSpaceRCFillCascade : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FWorldSpaceRCFillCascade)

	SHADER_USE_PARAMETER_STRUCT(FWorldSpaceRCFillCascade, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, SceneColorViewport)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint64_t>, RWHashTable)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, PrevActiveProbes)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, PrevActiveCounter)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, ActiveProbes)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint32>, ActiveCounter)
		RDG_BUFFER_ACCESS(IndirectArgsBuffer, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER(uint32, HashTableSize)
		SHADER_PARAMETER(uint32, Cascade)
	END_SHADER_PARAMETER_STRUCT()

	// Basic shader initialization
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	// Define environment variables used by compute shader
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADS_X"), GroupCount);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), 1);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), 1);
	}
	static constexpr uint32 GroupCount = 64;
};


class FWorldSpaceRCRaygen : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FWorldSpaceRCRaygen)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FWorldSpaceRCRaygen, FGlobalShader)
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepth)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_RDG_BUFFER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FRayTracingLightGrid, RaytracingLightGridData)
		SHADER_PARAMETER(uint32, HashTableSize)
		SHADER_PARAMETER(uint32, BaseDirections)
		SHADER_PARAMETER(float, NormalOffset)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint64_t>, HashTable)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint4>, TotalRadiance)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, Weights)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, BinCounts)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, DepositCounts)
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

class RADIANCECASCADEGI_API FWorldSpaceRCMerging : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FWorldSpaceRCMerging)

	SHADER_USE_PARAMETER_STRUCT(FWorldSpaceRCMerging, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint64_t>, HashTable)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, ActiveProbes)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint32>, ActiveCounter)
		RDG_BUFFER_ACCESS(IndirectArgsBuffer, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER(uint32, HashTableSize)
		SHADER_PARAMETER(uint32, Cascade)
		SHADER_PARAMETER(uint32, BaseDirections)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, ProbeRadiance)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ProbeWeights)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ProbeDepositCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, ProbeMergedRadiance)
	END_SHADER_PARAMETER_STRUCT()

	// Basic shader initialization
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	// Define environment variables used by compute shader
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADS_X"), GroupCount);
		OutEnvironment.SetDefine(TEXT("THREADS_Y"), 1);
		OutEnvironment.SetDefine(TEXT("THREADS_Z"), 1);
	}
	static constexpr uint32 GroupCount = 64;
};


class RADIANCECASCADEGI_API FWorldSpaceRCApply : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FWorldSpaceRCApply)

	SHADER_USE_PARAMETER_STRUCT(FWorldSpaceRCApply, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, SceneColorViewport)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint64_t>, HashTable)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint4>, ProbeRadiance)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ProbeWeights)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ProbeDepositCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, ProbeMergedRadiance)
		SHADER_PARAMETER(uint32, HashTableSize)
		SHADER_PARAMETER(uint32, BaseDirections)
		SHADER_PARAMETER(uint32, DisplayCascade)
		SHADER_PARAMETER(uint32, DebugOutputCells)

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


