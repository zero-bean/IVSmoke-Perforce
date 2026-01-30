// Copyright (c) 2026, Team SDB. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ScreenPass.h"
#include "IVSmokeShaders.h"
#include "IVSmokeSettings.h"
#include "IVSmokeVisualMaterialPreset.h"

class AIVSmokeVoxelVolume;
class FRDGBuilder;
class FSceneView;
class UIVSmokeSmokePreset;
class UTextureRenderTargetVolume;
struct FPostProcessMaterialInputs;
class FIVSmokeCSMRenderer;
class FIVSmokeVSMProcessor;
struct FIVSmokeOccupancyResources;

//~==============================================================================
// Internal Noise Generation Constants

/**
 * Internal noise generation configuration.
 * These values are tuned for optimal smoke appearance and should not be exposed to users.
 */
struct FIVSmokeNoiseConfig
{
	static constexpr int32 Seed = 0;
	static constexpr int32 TexSize = 128;
	static constexpr int32 Octaves = 6;
	static constexpr float Wrap = 0.76f;
	static constexpr float Amplitude = 0.62f;
	static constexpr int32 AxisCellCount = 4;
	static constexpr int32 CellSize = 32;

	/** UV multiplier for noise sampling. Fixed at 1.0; use SmokeSize to control detail frequency. */
	static constexpr float NoiseUVMul = 1.0f;
};

//~==============================================================================
// Render Data Structures (Thread-Safe Data Transfer)

/**
 * Packed render data for all smoke volumes.
 * Created on Game Thread, consumed on Render Thread.
 * Contains all data needed for rendering without accessing Volume actors.
 */
struct IVSMOKE_API FIVSmokePackedRenderData
{
	/** Packed voxel birth times for all volumes (flattened by VoxelBufferOffset). */
	TArray<float> PackedVoxelBirthTimes;

	/** Packed voxel death times for all volumes (flattened by VoxelBufferOffset). */
	TArray<float> PackedVoxelDeathTimes;

	/** Per-volume GPU metadata */
	TArray<FIVSmokeVolumeGPUData> VolumeDataArray;

	/** Hole Texture references (RHI resources are thread-safe) */
	TArray<FTextureRHIRef> HoleTextures;
	TArray<FIntVector> HoleTextureSizes;

	/** Common resolution info */
	FIntVector VoxelResolution = FIntVector::ZeroValue;
	FIntVector HoleResolution = FIntVector::ZeroValue;
	int32 VolumeCount = 0;

	/** Preset parameters (copied from default preset) */
	float Sharpness = 0.0f;
	int32 MaxSteps = 128;
	float GlobalAbsorption = 0.1f;
	float SmokeSize = 128.0f;
	float SmokeDensityFalloff = 0.2f;
	FVector WindDirection = FVector(0.01f, 0.02f, 0.1f);
	float VolumeRangeOffset = 0.1f;
	float VolumeEdgeNoiseFadeOffset = 0.04f;
	float VolumeEdgeFadeSharpness = 3.5f;

	/** Scattering parameters */
	bool bEnableScattering = true;
	float ScatterScale = 0.5f;
	float ScatteringAnisotropy = 0.5f;
	FVector LightDirection = FVector(0.2f, 0.1f, 0.9f);
	FLinearColor LightColor = FLinearColor::White;
	float LightIntensity = 1.0f;

	/** Self-shadowing parameters */
	bool bEnableSelfShadowing = true;
	int32 LightMarchingSteps = 6;
	float LightMarchingDistance = 0.0f;
	float LightMarchingExpFactor = 2.0f;
	float ShadowAmbient = 0.2f;

	/** External shadowing parameters (CSM - Cascaded Shadow Maps) */
	/** Note: CSM is always used when external shadowing is enabled */
	int32 NumCascades = 0;
	TArray<FTextureRHIRef> CSMDepthTextures;
	TArray<FTextureRHIRef> CSMVSMTextures;
	TArray<FMatrix> CSMViewProjectionMatrices;
	TArray<float> CSMSplitDistances;
	TArray<FVector> CSMLightCameraPositions;
	TArray<FVector> CSMLightCameraForwards;
	float CascadeBlendRange = 0.1f;
	float ShadowDepthBias = 1.0f;
	float ExternalShadowAmbient = 0.3f;

	/** VSM parameters */
	bool bEnableVSM = true;
	float VSMMinVariance = 0.0001f;
	float VSMLightBleedingReduction = 0.2f;

	/** Main camera position for CSM (must match what CSMRenderer used) */
	FVector CSMMainCameraPosition = FVector::ZeroVector;

	/** Validity flag */
	bool bIsValid = false;

	/** Game World Time */
	float GameTime = 0.0f;

	/** Rendering Info */
	UMaterialInterface* SmokeVisualMaterial = nullptr;
	EIVSmokeVisualAlphaType VisualAlphaType = EIVSmokeVisualAlphaType::Alpha;
	float AlphaThreshold = 0.0f;
	float LowOpacityRemapThreshold = 0.02f;

	/** Reset to invalid state */
	void Reset()
	{
		PackedVoxelBirthTimes.Empty();
		PackedVoxelDeathTimes.Empty();
		VolumeDataArray.Empty();
		HoleTextures.Empty();
		HoleTextureSizes.Empty();
		VolumeCount = 0;
		bIsValid = false;

		// CSM
		NumCascades = 0;
		CSMDepthTextures.Empty();
		CSMVSMTextures.Empty();
		CSMViewProjectionMatrices.Empty();
		CSMSplitDistances.Empty();
		CSMLightCameraPositions.Empty();
		CSMLightCameraForwards.Empty();

		SmokeVisualMaterial = nullptr;
	}
};

/**
 * Manages registered smoke volumes and handles rendering.
 * Owns shared rendering resources (noise volume) and reads settings from UIVSmokeSettings.
 */
class IVSMOKE_API FIVSmokeRenderer
{
public:
	static FIVSmokeRenderer& Get();

	//~==============================================================================
	// Lifecycle

	/** Initialize renderer resources. Called on first use or settings change. */
	void Initialize();

	/** Release renderer resources. */
	void Shutdown();

	/** Check if renderer is initialized with valid resources. */
	bool IsInitialized() const { return NoiseVolume != nullptr; }

	/** Check if server time offset was set. */
	bool bIsServerTimeSynced() const { return bServerTimeSynced; }

	/** SetServerTimeOffset for smoke wind animation. */
	void SetServerTimeOffset(const float InServerTimeOffset) { bServerTimeSynced = true;  ServerTimeOffset = InServerTimeOffset; }

	//~==============================================================================
	// Thread-Safe Render Data (Game Thread Render Thread)

	/** Maximum number of volumes supported for rendering. */
	static constexpr int32 MaxSupportedVolumes = 128;

	/**
	 * Prepare render data from all registered volumes.
	 * Must be called on Game Thread.
	 * Copies and packs all volume data for safe Render Thread access.
	 * If volume count exceeds MaxSupportedVolumes (128), filters by distance from camera.
	 *
	 * @param InVolumes Array of volumes to process
	 * @param CameraPosition Camera world position for distance-based filtering
	 * @return Packed render data ready for Render Thread
	 */
	FIVSmokePackedRenderData PrepareRenderData(const TArray<AIVSmokeVoxelVolume*>& InVolumes, const FVector& CameraPosition);

	/**
	 * Set cached render data for next frame.
	 * Called from Render Thread via ENQUEUE_RENDER_COMMAND.
	 */
	void SetCachedRenderData(FIVSmokePackedRenderData&& InRenderData)
	{
		FScopeLock Lock(&RenderDataMutex);
		CachedRenderData = MoveTemp(InRenderData);
	}

	//~==============================================================================
	// Rendering

	/**
	 * Main render entry point called from SceneViewExtension.
	 *
	 * @param GraphBuilder    RDG builder
	 * @param View            Current scene view
	 * @param Inputs          Post-process material inputs
	 * @return Output texture after smoke rendering
	 */
	FScreenPassTexture Render(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs
	);

private:
	FIVSmokeRenderer();   // Defined in cpp for TUniquePtr with forward-declared types
	~FIVSmokeRenderer();

	FIntVector GetAtlasTexCount(const FIntVector& TexSize, const int32 TexCount, const int32 TexturePackInterval, const int32 TexturePackMaxSize) const;
	//~==============================================================================
	// Resource Management

	/** Create noise volume texture using settings from UIVSmokeSettings. */
	void CreateNoiseVolume();

	/** Get the effective preset for a volume (override or default). */
	const UIVSmokeSmokePreset* GetEffectivePreset(const AIVSmokeVoxelVolume* Volume) const;

	//~==============================================================================
	// Pass Functions

	/**
	 * Multi-Volume Ray Marching with Occupancy Optimization (Three-Pass Pipeline).
	 * Uses tile-based occupancy grid for efficient empty space skipping.
	 * Processes all volumes with correct Beer-Lambert integration.
	 * Outputs to Dual Render Targets (Albedo + Mask) at reduced resolution.
	 *
	 * Pipeline:
	 *   Pass 0: Tile Setup - Compute per-tile depth range and quick volume mask
	 *   Pass 1: Occupancy Build - Build View and Light occupancy 3D textures
	 *   Pass 2: Ray March - Use occupancy for efficient skipping
	 *
	 * @param GraphBuilder       RDG builder
	 * @param View               Current scene view
	 * @param RenderData         Pre-packed render data (created on Game Thread)
	 * @param SmokeAlbedoTex     UAV texture for smoke color output
	 * @param SmokeMaskTex       UAV texture for smoke opacity mask
	 * @param TexSize            Size of output textures (may be reduced resolution)
	 * @param ViewportSize       Size of the full viewport for depth sampling
	 * @param ViewRectMin        Offset into full scene texture for depth sampling
	 */
	void AddMultiVolumeRayMarchPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FIVSmokePackedRenderData& RenderData,
		FRDGTextureRef SmokeAlbedoTex,
		FRDGTextureRef SmokeLocalPosAlpha,
		FRDGTextureRef SmokeWorldPosDepth,
		const FIntPoint& TexSize,
		const FIntPoint& ViewportSize,
		const FIntPoint& ViewRectMin
	);

	/**
	 * Copy/Resize Pass using bilinear sampling.
	 * Used for upscaling (1/2 resolution to Full) to improve quality.
	 *
	 * @param GraphBuilder       RDG builder
	 * @param View               Current scene view
	 * @param SourceTex          Source texture to copy from
	 * @param DestSize           Destination texture size (resizes via bilinear)
	 * @param TexName            Debug name for the created texture
	 * @return                   New texture at DestSize with copied content
	 */
	FRDGTextureRef AddCopyPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef SourceTex,
		const FIntPoint& DestSize,
		const TCHAR* TexName
	);

	/**
	 * Copy Pass to existing texture.
	 * Copies SourceTex to DestTex using bilinear sampling.
	 *
	 * @param GraphBuilder       RDG builder
	 * @param View               Current scene view
	 * @param SourceTex          Source texture to copy from
	 * @param DestTex            Destination texture to copy to
	 */
	void AddCopyPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef SourceTex,
		FRDGTextureRef DestTex
	);

	/**
	 * Filtering pass after upsampling.
	 *
	 * @param GraphBuilder			RDG builder
	 * @param RenderData			RenderData ref
	 * @param View					Current scene view
	 * @param SceneTex				SceneColor texture
	 * @param SmokeAlbedo			SmokeAlbedo texture from ray marching
	 * @param SmokeLocalPosAlpha	Smoke (local position, alpha) texture from ray marching
	 * @param TexSize				Output texture size
	 */
	FRDGTextureRef AddUpsampleFilterPass(FRDGBuilder& GraphBuilder, const FIVSmokePackedRenderData& RenderData, const FSceneView& View,
		FRDGTextureRef SceneTex, FRDGTextureRef SmokeAlbedo, FRDGTextureRef SmokeLocalPosAlpha, const FIntPoint& TexSize);

	/**
	 * Visual pass after Upsample Filtering.
	 *
	 * @param GraphBuilder				RDG builder
	 * @param RenderData				RenderData ref
	 * @param View						Current scene view
	 * @param SmokeTex					Smoke texture after SmokeVisual pass
	 * @param SmokeLocalPosAlphaTex		Smoke (local position, alpha) texture from ray marching
	 * @param SmokeWorldPosDepthTex		Smoke (world position, linear depth) texture from ray marching
	 * @param SceneTex,					Scene texture
	 * @param TexSize					Output texture size
	 */
	FRDGTextureRef AddSmokeVisualPass(FRDGBuilder& GraphBuilder, const FIVSmokePackedRenderData& RenderData, const FSceneView& View, FRDGTextureRef SmokeTex, FRDGTextureRef SmokeLocalPosAlphaTex, FRDGTextureRef SmokeWorldPosDepthTex, FRDGTextureRef SceneTex, const FIntPoint& TexSize);

	/**
	 * Composite PS Pass.
	 * Blends ray marching result (Dual RT) with scene color and applies sharpening/blurring.
	 *
	 * @param GraphBuilder				RDG builder
	 * @param RenderData				RenderData ref
	 * @param View						Current scene view
	 * @param SceneTex					Scene color texture
	 * @param SmokeVisualTex			Smoke texture after smoke visual pass
	 * @param SmokeLocalPosAlphaTex		Smoke (local position, alpha) texture from ray marching
	 * @param Output					Final render target
	 * @param ViewportSize				Size of the viewport for UV calculation
	 */
	void AddCompositePass(
		FRDGBuilder& GraphBuilder,
		const FIVSmokePackedRenderData& RenderData,
		const FSceneView& View,
		FRDGTextureRef SceneTex,
		FRDGTextureRef SmokeVisualTex,
		FRDGTextureRef SmokeLocalPosAlphaTex,
		const FScreenPassRenderTarget& Output,
		const FIntPoint& ViewportSize);

	/**
	 * Translucency Composite PS Pass.
	 * Composites smoke OVER particles for TranslucencyAfterDOF mode.
	 * Engine will composite result with SceneColor using alpha as transmittance.
	 *
	 * @param GraphBuilder				RDG builder
	 * @param RenderData				RenderData ref
	 * @param View						Current scene view
	 * @param SmokeVisualTex			Smoke texture after smoke visual pass
	 * @param SmokeLocalPosAlphaTex		Smoke (local position, alpha) texture from ray marching
	 * @param ParticlesTex				SeparateTranslucency texture (particles)
	 * @param Output					Final render target
	 * @param ParticlesTexExtent		Particles texture extent
	 * @param ViewportSize				Size of the viewport for UV calculation
	 */
	void AddTranslucencyCompositePass(
		FRDGBuilder& GraphBuilder,
		const FIVSmokePackedRenderData& RenderData,
		const FSceneView& View,
		FRDGTextureRef SmokeVisualTex,
		FRDGTextureRef SmokeLocalPosAlphaTex,
		FRDGTextureRef ParticlesTex,
		const FScreenPassRenderTarget& Output,
		const FIntPoint& ParticlesTexExtent,
		const FIntPoint& ViewportSize);

	/**
	 * Depth-Sorted Composite PS Pass.
	 * Compares Z values to determine front/back ordering, then applies standard over blending.
	 * Accesses CustomDepth and SceneDepth via SceneTexturesStruct uniform buffer.
	 *
	 * @param GraphBuilder				RDG builder
	 * @param RenderData				RenderData ref
	 * @param View						Current scene view
	 * @param SmokeVisualTex			Smoke texture after smoke visual pass
	 * @param SmokeLocalPosAlphaTex		Smoke (local position, alpha) texture from ray marching
	 * @param SmokeWorldPosDepthTex		Smoke (world position, linear depth) texture from ray marching
	 * @param SeparateTranslucencyTex	Particle layer from SeparateTranslucency
	 * @param Output					Final render target
	 * @param ViewportSize				Size of the viewport for UV calculation
	 */
	void AddDepthSortedCompositePass(
		FRDGBuilder& GraphBuilder,
		const FIVSmokePackedRenderData& RenderData,
		const FSceneView& View,
		FRDGTextureRef SmokeVisualTex,
		FRDGTextureRef SmokeLocalPosAlphaTex,
		FRDGTextureRef SmokeWorldPosDepthTex,
		FRDGTextureRef SeparateTranslucencyTex,
		const FScreenPassRenderTarget& Output,
		const FIntPoint& ViewportSize);

	//~==============================================================================
	// State

	/** Shared noise volume texture for all smoke rendering. Prevent GC via AddToRoot. */
	UTextureRenderTargetVolume* NoiseVolume = nullptr;

	/** True if server time offset has been initialized (one-time sync completed). */
	bool bServerTimeSynced = false;

	/** ServerTime offset for animation. (ServerTime = LocalTime + Offset) */
	float ServerTimeOffset = 0.0f;

	//~==============================================================================
	// External Shadowing (CSM - Cascaded Shadow Maps)

	/** Last world used for rendering. Used to detect world changes (Editor ↔ PIE). */
	TWeakObjectPtr<UWorld> LastRenderedWorld;

	/** CSM renderer (manages all cascade captures). */
	TUniquePtr<FIVSmokeCSMRenderer> CSMRenderer;

	/** VSM processor (depth to variance conversion and blur). */
	TUniquePtr<FIVSmokeVSMProcessor> VSMProcessor;

	/** Last frame number when CSM was updated (prevents multiple updates per frame). */
	uint32 LastCSMUpdateFrameNumber = 0;

	/** Last frame number when VSM was processed (prevents duplicate processing per view). */
	uint32 LastVSMProcessFrameNumber = 0;

	/** Re-entry guard to prevent infinite recursion during shadow capture. */
	bool bIsCapturingShadow = false;

	/** Initialize CSM renderer if needed. */
	void InitializeCSM(UWorld* World);

	/** Clean up CSM resources. */
	void CleanupCSM();

	/**
	 * Find the main directional light (Atmosphere Sun Light) in the world.
	 * Uses the same logic as the engine: bAtmosphereSunLight + AtmosphereSunLightIndex.
	 *
	 * @param World          World to search in
	 * @param OutDirection   Direction TOWARD the light source (opposite of light travel direction)
	 * @param OutColor       Light color
	 * @param OutIntensity   Light intensity
	 * @return true if a directional light was found
	 */
	bool GetMainDirectionalLight(UWorld* World, FVector& OutDirection, FLinearColor& OutColor, float& OutIntensity);

	//~==============================================================================
	// Thread-Safe Render Data Cache

	/** Cached render data prepared on Game Thread, consumed on Render Thread. */
	FIVSmokePackedRenderData CachedRenderData;

	/** Mutex for thread-safe access to CachedRenderData. */
	mutable FCriticalSection RenderDataMutex;

	//~==============================================================================
	// Stats Tracking

	/** Last time stats were updated (for 1-second interval). */
	double LastStatUpdateTime = 0.0;

	/** Cached memory sizes for stat reporting. */
	int64 CachedNoiseVolumeSize = 0;
	int64 CachedCSMSize = 0;
	int64 CachedPerFrameSize = 0;

	/** Update stats if 1 second has passed since last update. */
	void UpdateStatsIfNeeded(const FIVSmokePackedRenderData& RenderData, const FIntPoint& ViewportSize);

	/** Calculate per-frame texture memory usage. */
	int64 CalculatePerFrameTextureSize(
		const FIntPoint& ViewportSize,
		int32 VolumeCount,
		const FIntVector& VoxelResolution,
		const FIntVector& HoleResolution
	) const;

	/** Update all stat values. */
	void UpdateAllStats();
};
