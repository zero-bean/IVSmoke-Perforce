// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"

// ============================================================================
// GPU Data Structures
// ============================================================================

/**
 * @brief GPU-friendly trajectory metadata.
 *        References control points in separate buffer for flexibility.
 */
struct FIVSmokeHoleTrajectoryGPU
{
	int32 ControlPointStartIndex;  // Index into ControlPointBuffer
	int32 NumControlPoints;        // 2=linear, 3=quadratic, N=spline
	float StartRadius;             // Radius at trajectory start
	float EndRadius;               // Radius at trajectory end

	float CreationTime;            // Creation timestamp
	int32 ShapeType;               // Primitive type (0=Sphere, 1=Box)
	float EdgeSoftness;            // Hole edge falloff (0=hard, 1=soft)
	float DensityMultiplier;       // Density reduction (0.5=half, 1=full hole)
};  // Total: 32 bytes

/** @brief Single control point for trajectory path. */
struct FIVSmokeControlPointGPU
{
	FVector3f Position;  // World position
	float Radius;        // Radius at this point
};  // Total: 16 bytes

/** @brief Primitive shape types for SDF calculation. */
enum class EIVSmokeHoleShape : int32
{
	Sphere = 0,
	Box = 1
};

// ============================================================================
// Compute Shader
// ============================================================================

/**
 * @brief Compute shader that carves trajectories into 3D volume texture.
 *
 * @architecture
 *   - TrajectoryBuffer: Metadata for each bullet hole (start/end radius, timing)
 *   - ControlPointBuffer: Shared pool of control points for all trajectories
 *   - Each voxel thread evaluates all trajectories and computes final density
 */
class IVSMOKE_API FIVSmokeHoleCarveCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FIVSmokeHoleCarveCS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeHoleCarveCS, FGlobalShader);

public:
	static constexpr uint32 ThreadGroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Output: 3D Volume Texture (Read and Write)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, VolumeTexture)

		// Input: Trajectory metadata buffer
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FIVSmokeHoleTrajectoryGPU>, TrajectoryBuffer)

		// Input: Control points buffer (shared pool)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FIVSmokeControlPointGPU>, ControlPointBuffer)

		// Volume bounds (world space)
		SHADER_PARAMETER(FVector3f, VolumeMin)
		SHADER_PARAMETER(FVector3f, VolumeMax)

		// Volume resolution
		SHADER_PARAMETER(FIntVector, Resolution)

		// Trajectory parameters
		SHADER_PARAMETER(int32, NumTrajectories)
		SHADER_PARAMETER(int32, NumSpheresPerTrajectory)
		SHADER_PARAMETER(float, CurrentTime)
		SHADER_PARAMETER(float, HoleLifetime)
	END_SHADER_PARAMETER_STRUCT()

	/** @todo implement to check version into OnPostEngineInit() [IVSmoke.cpp] */
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
