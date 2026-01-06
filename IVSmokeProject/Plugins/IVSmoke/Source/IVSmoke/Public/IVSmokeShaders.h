// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"

/**
 * Ray Marching compute shader.
 * Calculates volumetric smoke and outputs to intermediate texture.
 */
class IVSMOKE_API FIVSmokeRayMarchCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FIVSmokeRayMarchCS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeRayMarchCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSizeX = 8;
	static constexpr uint32 ThreadGroupSizeY = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Output
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)

		// Viewport
		SHADER_PARAMETER(FVector2f, ViewportSize)

		// Camera (Ray reconstruction using vectors)
		SHADER_PARAMETER(FVector3f, CameraPosition)
		SHADER_PARAMETER(FVector3f, CameraForward)
		SHADER_PARAMETER(FVector3f, CameraRight)
		SHADER_PARAMETER(FVector3f, CameraUp)
		SHADER_PARAMETER(float, TanHalfFOV)
		SHADER_PARAMETER(float, AspectRatio)

		// Ray Marching Setup
		SHADER_PARAMETER(int32, MaxSteps)

		// Volume Data
		SHADER_PARAMETER(FVector3f, VolumeMin)
		SHADER_PARAMETER(FVector3f, VolumeMax)
		SHADER_PARAMETER(float, VolumeDensity)
		SHADER_PARAMETER(FVector3f, SmokeColor)
		SHADER_PARAMETER(float, SmokeAbsorption)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Y"), ThreadGroupSizeY);
	}
};
class IVSMOKE_API FIVSmokeNoiseGeneratorGlobalCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FIVSmokeNoiseGeneratorGlobalCS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeNoiseGeneratorGlobalCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<half>, RWNoiseTex)
		SHADER_PARAMETER(FUintVector3, TexSize)
		SHADER_PARAMETER(int32, Octaves)
		SHADER_PARAMETER(float, Wrap)
		SHADER_PARAMETER(int32, AxisCellCount)
		SHADER_PARAMETER(float, Amplitude)
		SHADER_PARAMETER(int32, CellSize)
		SHADER_PARAMETER(int32, Seed)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/**
 * Composite pixel shader.
 * Blends ray marching result with scene color.
 */
class IVSMOKE_API FIVSmokeCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FIVSmokeCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector2f, ViewportSize)
		SHADER_PARAMETER(FVector2f, ViewRectMin)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SmokeTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TextureSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
class IVSMOKE_API FIVSmokeBicubicFilteringPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FIVSmokeBicubicFilteringPS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeBicubicFilteringPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, MainTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, MainSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class IVSMOKE_API FIVSmokeSharpenCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FIVSmokeSharpenCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeSharpenCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, MainTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SmokeTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SmokeMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, DepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, MainSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, PointClampSampler)
		SHADER_PARAMETER(float, Sharpness)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
