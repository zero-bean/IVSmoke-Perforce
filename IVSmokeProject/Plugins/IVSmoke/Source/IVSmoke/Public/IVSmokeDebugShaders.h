// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

// ============================================================================
// Volume Debug Visualization Shaders (Ray Marching Cube)
// ============================================================================

/**
 * Shared parameters for volume cube ray marching visualization.
 */
BEGIN_SHADER_PARAMETER_STRUCT(FIVSmokeVolumeSliceParameters, )
	SHADER_PARAMETER_TEXTURE(Texture3D, VolumeTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, VolumeSampler)
	SHADER_PARAMETER(FMatrix44f, LocalToWorld)
	SHADER_PARAMETER(FMatrix44f, WorldToLocal)
	SHADER_PARAMETER(FMatrix44f, WorldToClip)
	SHADER_PARAMETER(FVector3f, VolumeExtent)
	SHADER_PARAMETER(FVector3f, CameraWorldPos)
	SHADER_PARAMETER(int32, NumSteps)
	SHADER_PARAMETER(float, StepOpacity)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

/**
 * Vertex shader for volume cube debug visualization.
 */
class IVSMOKE_API FIVSmokeVolumeSliceDebugVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FIVSmokeVolumeSliceDebugVS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeVolumeSliceDebugVS, FGlobalShader);

	using FParameters = FIVSmokeVolumeSliceParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/**
 * Pixel shader for volume cube debug visualization with ray marching.
 */
class IVSMOKE_API FIVSmokeVolumeSliceDebugPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FIVSmokeVolumeSliceDebugPS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeVolumeSliceDebugPS, FGlobalShader);

	using FParameters = FIVSmokeVolumeSliceParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
