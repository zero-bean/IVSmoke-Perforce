// Copyright SDB. All Rights Reserved.

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
	DECLARE_GLOBAL_SHADER(FIVSmokeHoleCarveCS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeHoleCarveCS, FGlobalShader);

public:
	static constexpr uint32 ThreadGroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Output: 3D Volume Texture (Read and Write) - R16F single channel
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, VolumeTexture)

		// Input: Hole data buffer (unified structure)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FIVSmokeHoleGPU>, HoleBuffer)

		// Volume bounds (local space)
		SHADER_PARAMETER(FVector3f, VolumeMin)
		SHADER_PARAMETER(FVector3f, VolumeMax)

		// Volume resolution
		SHADER_PARAMETER(FIntVector, Resolution)

		// Update region for partial dispatch (voxel coordinates)
		SHADER_PARAMETER(FIntVector, UpdateRegionMin)
		SHADER_PARAMETER(FIntVector, UpdateRegionMax)

		// Hole parameters
		SHADER_PARAMETER(int32, NumHoles)

		// Flags (1 = clear then apply, 0 = apply only)
		SHADER_PARAMETER(int32, bIsFullRebuild)
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
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
	}
};
