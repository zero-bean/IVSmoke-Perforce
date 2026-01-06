// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "SceneView.h"

/**
 * Volume texture debug visualization pixel shader.
 * Displays a Z-slice of the 3D volume texture for debugging.
 */
class IVSMOKE_API FIVSmokeVolumeTextureDebugPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FIVSmokeVolumeTextureDebugPS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeVolumeTextureDebugPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_TEXTURE(Texture3D, SmokeVolumeTexture3D)
		SHADER_PARAMETER_SAMPLER(SamplerState, SmokeVolumeSampler)
		SHADER_PARAMETER(int32, Resolution)
		SHADER_PARAMETER(int32, SliceIndex)
		SHADER_PARAMETER(int32, DebugMode)
		SHADER_PARAMETER(float, CurrentTime)
		SHADER_PARAMETER(float, HoleLifeTime)
		SHADER_PARAMETER(FVector2f, DisplayOffset)
		SHADER_PARAMETER(FVector2f, DisplaySize)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
