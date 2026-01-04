// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

/**
 * Ray marching pixel shader for volumetric smoke.
 */
class FIVSmokeRayMarchPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FIVSmokeRayMarchPS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeRayMarchPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector3f, BoundsMin)
		SHADER_PARAMETER(FVector3f, BoundsMax)
		// TODO: Add parameters (camera, time, noise texture, etc.)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
