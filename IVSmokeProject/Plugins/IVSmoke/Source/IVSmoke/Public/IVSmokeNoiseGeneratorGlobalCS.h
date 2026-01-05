// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
/**
 *
 */
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
