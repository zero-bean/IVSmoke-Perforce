// Copyright (c) 2026, Team SDB. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

// ============================================================================
// Compute Shader
// ============================================================================

/**
 * @brief Compute shader that carves holes into 3D volume texture.
 */
class IVSMOKE_API FIVSmokeHoleCarveCS : public FGlobalShader
{
public:
	static constexpr uint32 CurveSampleCount = 16;
	static constexpr uint32 ThreadGroupSizeX = 8;
	static constexpr uint32 ThreadGroupSizeY = 8;
	static constexpr uint32 ThreadGroupSizeZ = 8;
	static constexpr const TCHAR* EventName = TEXT("IVSmokeHoleCarveCS");
	DECLARE_GLOBAL_SHADER(FIVSmokeHoleCarveCS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeHoleCarveCS, FGlobalShader);

public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Output: 3D Volume Texture (Read and Write) - R16G16B16A16_UNORM channel
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, VolumeTexture)

		// Input: Hole data buffer (unified structure)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FIVSmokeHoleGPU>, HoleBuffer)

		// Volume bounds (local space)
		SHADER_PARAMETER(FVector3f, VolumeMin)
		SHADER_PARAMETER(FVector3f, VolumeMax)

		// Volume resolution
		SHADER_PARAMETER(FIntVector, Resolution)

		// Hole parameters
		SHADER_PARAMETER(int32, NumHoles)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(
		const FGlobalShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment
	)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), ThreadGroupSizeY);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEZ"), ThreadGroupSizeZ);
		OutEnvironment.SetDefine(TEXT("CURVE_SAMPLE_COUNT"), CurveSampleCount);
	}
};
