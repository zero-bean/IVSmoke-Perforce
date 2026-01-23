// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "SceneTexturesConfig.h"
#include "SceneView.h"

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
	FVector3f VolumeWorldAABBMin;         // 12 bytes
	float VoxelSize;                // 4 bytes

	/** World-space AABB maximum (for fast ray-box intersection). */
	FVector3f VolumeWorldAABBMax;         // 12 bytes
	uint32 VoxelBufferOffset;       // 4 bytes

	FVector3f VoxelWorldAABBMin;	// 12 bytes
	float FadeInDuration;			// 4 bytes
	FVector3f VoxelWorldAABBMax;	// 12 bytes
	float FadeOutDuration;			// 4 bytes

	float Reserved[4];              // 16 bytes (future use / alignment)
};

// Ensure structure is 256 bytes for efficient GPU access
static_assert(sizeof(FIVSmokeVolumeGPUData) % 16 == 0, "FIVSmokeVolumeGPUData size error");

// Note: FIVSmokeMultiVolumeRayMarchCS is now defined in IVSmokeOccupancy.h
// (Occupancy-based ray marching has replaced the original implementation)

class IVSMOKE_API FIVSmokeNoiseGeneratorGlobalCS : public FGlobalShader
{
public:
	static constexpr uint32 ThreadGroupSizeX = 8;
	static constexpr uint32 ThreadGroupSizeY = 8;
	static constexpr uint32 ThreadGroupSizeZ = 8;
	static constexpr const TCHAR* EventName = TEXT("IVSmokeNoiseGeneratorGlobalCS");

	DECLARE_GLOBAL_SHADER(FIVSmokeNoiseGeneratorGlobalCS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeNoiseGeneratorGlobalCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, RWNoiseTex)
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
class IVSMOKE_API FIVSmokeStructuredToTextureCS : public FGlobalShader
{
public:
	static constexpr uint32 ThreadGroupSizeX = 8;
	static constexpr uint32 ThreadGroupSizeY = 8;
	static constexpr uint32 ThreadGroupSizeZ = 8;
	static constexpr const TCHAR* EventName = TEXT("IVSmokeStructuredToTextureCS");

	DECLARE_GLOBAL_SHADER(FIVSmokeStructuredToTextureCS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeStructuredToTextureCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, Desti)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, Source)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FIVSmokeVolumeGPUData>, VolumeDataBuffer)

		SHADER_PARAMETER(FIntVector, TexSize)
		SHADER_PARAMETER(FIntVector, VoxelResolution)
		SHADER_PARAMETER(FIntVector, VoxelAtlasCount)
		SHADER_PARAMETER(int32, PackedInterval)
		SHADER_PARAMETER(float, GameTime)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
class IVSMOKE_API FIVSmokeVoxelFXAACS : public FGlobalShader
{
public:
	static constexpr uint32 ThreadGroupSizeX = 8;
	static constexpr uint32 ThreadGroupSizeY = 8;
	static constexpr uint32 ThreadGroupSizeZ = 8;
	static constexpr const TCHAR* EventName = TEXT("IVSmokeVoxelFXAACS");

	DECLARE_GLOBAL_SHADER(FIVSmokeVoxelFXAACS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeVoxelFXAACS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, Desti)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, Source)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearBorder_Sampler)
		SHADER_PARAMETER(FIntVector, TexSize)

		//FXAA
		SHADER_PARAMETER(float, FXAASpanMax)
		SHADER_PARAMETER(float, FXAARange)
		SHADER_PARAMETER(float, FXAASharpness)
	END_SHADER_PARAMETER_STRUCT()
};

class IVSMOKE_API FIVSmokeSharpenCompositePS : public FGlobalShader
{
public:
	static constexpr const TCHAR* EventName = TEXT("IVSmokeSharpenCompositePS");
	static FRHIBlendState* GetBlendState()
	{
		return TStaticBlendState<>::GetRHI();
	}

	DECLARE_GLOBAL_SHADER(FIVSmokeSharpenCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeSharpenCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SmokeAlbedoTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SmokeMaskTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearRepeat_Sampler)
		SHADER_PARAMETER(float, Sharpness)
		SHADER_PARAMETER(FVector2f, ViewportSize)
		SHADER_PARAMETER(FVector2f, ViewRectMin)
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
public:
	static constexpr const TCHAR* EventName = TEXT("IVSmokeCopyPS");
	static FRHIBlendState* GetBlendState()
	{
		return TStaticBlendState<>::GetRHI();
	}

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

/**
 * Translucency Composite pixel shader.
 * Composites smoke OVER SeparateTranslucency (smoke on top of particles).
 * Used for TranslucencyAfterDOF render pass.
 */
class IVSMOKE_API FIVSmokeTranslucencyCompositePS : public FGlobalShader
{
public:
	static constexpr const TCHAR* EventName = TEXT("IVSmokeTranslucencyCompositePS");
	static FRHIBlendState* GetBlendState()
	{
		return TStaticBlendState<>::GetRHI();
	}

	DECLARE_GLOBAL_SHADER(FIVSmokeTranslucencyCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeTranslucencyCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SmokeAlbedoTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SmokeMaskTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ParticlesTex)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER(float, Sharpness)
		SHADER_PARAMETER(FVector2f, SmokeTexExtent)
		SHADER_PARAMETER(FVector2f, ParticlesTexExtent)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/**
 * Depth-Sorted Composite pixel shader.
 * Compares Z values to determine front/back ordering, then applies standard over blending.
 * Properly composites smoke and particles based on their depth relationship.
 */
class IVSMOKE_API FIVSmokeDepthSortedCompositePS : public FGlobalShader
{
public:
	static constexpr const TCHAR* EventName = TEXT("IVSmokeDepthSortedCompositePS");
	static FRHIBlendState* GetBlendState()
	{
		return TStaticBlendState<>::GetRHI();
	}

	DECLARE_GLOBAL_SHADER(FIVSmokeDepthSortedCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeDepthSortedCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Smoke layer (from ray marching CS)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SmokeAlbedoTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SmokeMaskTex)

		// Particle layer (from Separate Translucency)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SeparateTranslucencyTex)

		// Scene Textures (provides CustomDepth and SceneDepth via uniform buffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)

		// Samplers
		SHADER_PARAMETER_SAMPLER(SamplerState, PointClamp_Sampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearClamp_Sampler)

		// Texture Extents for UV calculation (UV = SvPosition / TexExtent)
		SHADER_PARAMETER(FVector2f, SmokeTexExtent)
		SHADER_PARAMETER(float, Sharpness)
		SHADER_PARAMETER(FVector4f, InvDeviceZToWorldZTransform)

		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// ============================================================================
// VSM (Variance Shadow Map) Shaders
// ============================================================================

/**
 * Depth to Variance compute shader.
 * Converts depth texture (R32F) to variance texture (RG32F).
 * Output: (depth, depth²)
 */
class IVSMOKE_API FIVSmokeDepthToVarianceCS : public FGlobalShader
{
public:
	static constexpr uint32 ThreadGroupSizeX = 8;
	static constexpr uint32 ThreadGroupSizeY = 8;
	static constexpr uint32 ThreadGroupSizeZ = 1;
	static constexpr const TCHAR* EventName = TEXT("IVSmokeDepthToVarianceCS");

	DECLARE_GLOBAL_SHADER(FIVSmokeDepthToVarianceCS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeDepthToVarianceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, VarianceTexture)
		SHADER_PARAMETER(FIntPoint, TextureSize)
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
 * VSM Gaussian blur compute shader.
 * Performs separable Gaussian blur on variance texture.
 * Uses horizontal or vertical direction based on BlurDirection parameter.
 */
class IVSMOKE_API FIVSmokeVSMBlurCS : public FGlobalShader
{
public:
	static constexpr uint32 ThreadGroupSizeX = 8;
	static constexpr uint32 ThreadGroupSizeY = 8;
	static constexpr uint32 ThreadGroupSizeZ = 1;
	static constexpr const TCHAR* EventName = TEXT("IVSmokeVSMBlurCS");

	DECLARE_GLOBAL_SHADER(FIVSmokeVSMBlurCS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeVSMBlurCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, DestTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearClampSampler)
		SHADER_PARAMETER(FIntPoint, TextureSize)
		SHADER_PARAMETER(int32, BlurRadius)
		SHADER_PARAMETER(int32, BlurDirection)  // 0 = Horizontal, 1 = Vertical
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
