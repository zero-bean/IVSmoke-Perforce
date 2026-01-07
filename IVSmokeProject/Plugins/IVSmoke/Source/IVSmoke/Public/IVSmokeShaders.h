// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "SceneTexturesConfig.h"

// ============================================================================
// GPU Data Structures for Multi-Volume Rendering
// ============================================================================

/**
 * GPU-side volume metadata for single-pass multi-volume ray marching.
 * Each volume has its own transform, bounds, and rendering parameters.
 * This struct is uploaded to a StructuredBuffer for GPU access.
 *
 * Memory layout: 256 bytes (aligned to 16-byte boundary)
 */
struct FIVSmokeVolumeGPUData
{
	/** World-to-local transform for this volume. */
	FMatrix44f WorldToLocal;        // 64 bytes

	/** Local-to-world transform for this volume. */
	FMatrix44f LocalToWorld;        // 64 bytes

	/** Local-space AABB minimum corner. */
	FVector3f AABBMin;              // 12 bytes
	/** World-space size of each voxel. */
	float VoxelSize;                // 4 bytes

	/** Local-space AABB maximum corner. */
	FVector3f AABBMax;              // 12 bytes
	/** Offset into packed voxel buffer. */
	uint32 VoxelBufferOffset;       // 4 bytes

	/** Grid resolution (voxel count per axis). */
	FIntVector3 GridResolution;     // 12 bytes
	/** Total voxel count for this volume. */
	uint32 VoxelCount;              // 4 bytes

	/** Smoke color for this volume. */
	FVector3f SmokeColor;           // 12 bytes
	/** Absorption coefficient. */
	float Absorption;               // 4 bytes

	/** Center offset for grid-to-local coordinate conversion. */
	FVector3f CenterOffset;         // 12 bytes
	/** Per-volume density multiplier (default 1.0). */
	float DensityScale;             // 4 bytes

	/** World-space AABB minimum (for fast ray-box intersection). */
	FVector3f WorldAABBMin;         // 12 bytes
	float Pad1;                     // 4 bytes (padding)

	/** World-space AABB maximum (for fast ray-box intersection). */
	FVector3f WorldAABBMax;         // 12 bytes
	float Pad2;                     // 4 bytes (padding)

	// Total: 240 bytes, padded to 256 bytes for GPU alignment
	float Reserved[4];              // 16 bytes (future use / alignment)
};

// Ensure structure is 256 bytes for efficient GPU access
static_assert(sizeof(FIVSmokeVolumeGPUData) == 256, "FIVSmokeVolumeGPUData must be 256 bytes");

/**
 * Multi-Volume Ray Marching compute shader.
 * Processes all smoke volumes in a single pass with correct Beer-Lambert integration.
 */
class IVSMOKE_API FIVSmokeMultiVolumeRayMarchCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FIVSmokeMultiVolumeRayMarchCS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeMultiVolumeRayMarchCS, FGlobalShader);

	static constexpr uint32 ThreadGroupSizeX = 8;
	static constexpr uint32 ThreadGroupSizeY = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Output (Dual Render Target)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SmokeAlbedoTex)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, SmokeMaskTex)

		// Input Textures
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, NoiseVolume)

		// Samplers
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearRepeat_Sampler)

		// Time
		SHADER_PARAMETER(float, ElapsedTime)

		// Viewport
		SHADER_PARAMETER(FIntPoint, TexSize)
		SHADER_PARAMETER(FVector2f, ViewportSize)
		SHADER_PARAMETER(FVector2f, ViewRectMin)

		// Camera (Ray reconstruction using vectors)
		SHADER_PARAMETER(FVector3f, CameraPosition)
		SHADER_PARAMETER(FVector3f, CameraForward)
		SHADER_PARAMETER(FVector3f, CameraRight)
		SHADER_PARAMETER(FVector3f, CameraUp)
		SHADER_PARAMETER(float, TanHalfFOV)
		SHADER_PARAMETER(float, AspectRatio)

		// Ray Marching Setup
		SHADER_PARAMETER(int32, MaxSteps)

		// Multi-Volume Data
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FIVSmokeVolumeGPUData>, VolumeDataBuffer)
		SHADER_PARAMETER(uint32, NumActiveVolumes)

		// Packed Voxel Data (all volumes concatenated)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, PackedVoxelBuffer)

		// Scene Textures
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER(FVector4f, InvDeviceZToWorldZTransform)

		// Global Smoke Parameters
		SHADER_PARAMETER(float, GlobalAbsorption)
		SHADER_PARAMETER(float, SmokeSize)
		SHADER_PARAMETER(float, SmokeDensityFalloff)
		SHADER_PARAMETER(FVector3f, WindDirection)

		// Rayleigh Scattering
		SHADER_PARAMETER(FVector3f, LightDirection)
		SHADER_PARAMETER(FVector3f, LightColor)
		SHADER_PARAMETER(float, ScatterScale)

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
		OutEnvironment.SetDefine(TEXT("MULTI_VOLUME_RAY_MARCH"), 1);
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
//class IVSMOKE_API FIVSmokeCompositePS : public FGlobalShader
//{
//	DECLARE_GLOBAL_SHADER(FIVSmokeCompositePS);
//	SHADER_USE_PARAMETER_STRUCT(FIVSmokeCompositePS, FGlobalShader);
//
//	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
//		SHADER_PARAMETER(FVector2f, ViewportSize)
//		SHADER_PARAMETER(FVector2f, ViewRectMin)
//		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SmokeTexture)
//		SHADER_PARAMETER_SAMPLER(SamplerState, TextureSampler)
//		RENDER_TARGET_BINDING_SLOTS()
//	END_SHADER_PARAMETER_STRUCT()
//
//	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
//	{
//		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
//	}
//};
class IVSMOKE_API FIVSmokeSharpenCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FIVSmokeSharpenCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeSharpenCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SmokeAlbedoTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SmokeMaskTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearRepeat_Sampler)
		SHADER_PARAMETER(float, Sharpness)
		SHADER_PARAMETER(FVector2f, ViewportSize)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
class IVSMOKE_API FIVSmokeCopyPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FIVSmokeCopyPS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeCopyPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MainTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearRepeat_Sampler)
		SHADER_PARAMETER(FVector2f, ViewportSize)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
