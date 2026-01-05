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
