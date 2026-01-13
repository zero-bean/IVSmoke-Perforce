// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ScreenPass.h"

class AIVSmokeVoxelVolume;
class FRDGBuilder;
class FSceneView;
class UIVSmokeSmokePreset;
class UTextureRenderTargetVolume;
struct FPostProcessMaterialInputs;

/**
 * Manages registered smoke volumes and handles rendering.
 * Owns shared rendering resources (noise volume) and reads settings from UIVSmokeSettings.
 */
class IVSMOKE_API FIVSmokeRenderer
{
public:
	static FIVSmokeRenderer& Get();

	// ============================================================================
	// Lifecycle
	// ============================================================================

	/** Initialize renderer resources. Called on first use or settings change. */
	void Initialize();

	/** Release renderer resources. */
	void Shutdown();

	/** Check if renderer is initialized with valid resources. */
	bool IsInitialized() const { return NoiseVolume != nullptr; }

	// ============================================================================
	// Volume Management
	// ============================================================================

	void AddVolume(AIVSmokeVoxelVolume* Volume);
	void RemoveVolume(AIVSmokeVoxelVolume* Volume);

	bool HasVolumes() const;

	// ============================================================================
	// Rendering
	// ============================================================================

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
	FIVSmokeRenderer() = default;

	// ============================================================================
	// Resource Management
	// ============================================================================

	/** Create noise volume texture using settings from UIVSmokeSettings. */
	void CreateNoiseVolume();

	/** Get the effective preset for a volume (override or default). */
	const UIVSmokeSmokePreset* GetEffectivePreset(const AIVSmokeVoxelVolume* Volume) const;

	// ============================================================================
	// Pass Functions
	// ============================================================================

	/**
	 * Multi-Volume Ray Marching CS Pass (Single-Pass).
	 * Processes all volumes in a single pass with correct Beer-Lambert integration.
	 * Outputs to Dual Render Targets (Albedo + Mask) at reduced resolution.
	 *
	 * @param GraphBuilder       RDG builder
	 * @param View               Current scene view
	 * @param SortedVolumes      Sorted array of volumes to render
	 * @param SmokeAlbedoTex     UAV texture for smoke color output
	 * @param SmokeMaskTex       UAV texture for smoke opacity mask
	 * @param TexSize            Size of output textures (may be reduced resolution)
	 * @param ViewportSize       Size of the full viewport for depth sampling
	 * @param ViewRectMin        Offset into full scene texture for depth sampling
	 */
	void AddMultiVolumeRayMarchPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const TArray<AIVSmokeVoxelVolume*>& SortedVolumes,
		FRDGTextureRef SmokeAlbedoTex,
		FRDGTextureRef SmokeMaskTex,
		const FIntPoint& TexSize,
		const FIntPoint& ViewportSize,
		const FIntPoint& ViewRectMin
	);

	/**
	 * Sharpen Composite PS Pass.
	 * Blends ray marching result (Dual RT) with scene color and applies sharpening/blurring.
	 *
	 * @param GraphBuilder       RDG builder
	 * @param View               Current scene view
	 * @param SceneTex           Scene color texture
	 * @param SmokeAlbedoTex     Smoke color texture from ray marching
	 * @param SmokeMaskTex       Smoke opacity mask from ray marching
	 * @param Output             Final render target
	 * @param ViewportSize       Size of the viewport for UV calculation
	 * @param Sharpness          Sharpen/blur amount (-1 to 1, 0 = no filter)
	 */
	void AddSharpenCompositePass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef SceneTex,
		FRDGTextureRef SmokeAlbedoTex,
		FRDGTextureRef SmokeMaskTex,
		const FScreenPassRenderTarget& Output,
		const FIntPoint& ViewportSize,
		float Sharpness
	);

	/**
	 * Copy/Resize Pass using bilinear sampling.
	 * Used for progressive upscaling (1/4 → 1/2 → Full) to improve quality.
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
	 * Translucency Composite PS Pass.
	 * Composites smoke OVER particles for TranslucencyAfterDOF mode.
	 * Engine will composite result with SceneColor using alpha as transmittance.
	 *
	 * @param GraphBuilder       RDG builder
	 * @param View               Current scene view
	 * @param SmokeAlbedoTex     Smoke color texture from ray marching
	 * @param SmokeMaskTex       Smoke opacity mask from ray marching
	 * @param ParticlesTex       SeparateTranslucency texture (particles)
	 * @param Output             Final render target
	 * @param Sharpness          Sharpen/blur amount (-1 to 1, 0 = no filter)
	 */
	void AddTranslucencyCompositePass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef SmokeAlbedoTex,
		FRDGTextureRef SmokeMaskTex,
		FRDGTextureRef ParticlesTex,
		const FScreenPassRenderTarget& Output,
		float Sharpness
	);

	/**
	 * Depth-Sorted Composite PS Pass.
	 * Compares Z values to determine front/back ordering, then applies standard over blending.
	 * Accesses CustomDepth and SceneDepth via SceneTexturesStruct uniform buffer.
	 *
	 * @param GraphBuilder            RDG builder
	 * @param View                    Current scene view
	 * @param SceneTex                Scene color texture (background)
	 * @param SmokeAlbedoTex          Smoke color texture from ray marching
	 * @param SmokeMaskTex            Smoke opacity mask from ray marching
	 * @param SeparateTranslucencyTex Particle layer from SeparateTranslucency
	 * @param Output                  Final render target
	 */
	void AddDepthSortedCompositePass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef SceneTex,
		FRDGTextureRef SmokeAlbedoTex,
		FRDGTextureRef SmokeMaskTex,
		FRDGTextureRef SeparateTranslucencyTex,
		const FScreenPassRenderTarget& Output
	);

	// ============================================================================
	// State
	// ============================================================================

	TArray<TWeakObjectPtr<AIVSmokeVoxelVolume>> Volumes;
	mutable FCriticalSection VolumesMutex;

	/** Shared noise volume texture for all smoke rendering. Prevent GC via AddToRoot. */
	UTextureRenderTargetVolume* NoiseVolume = nullptr;

	/** Elapsed time for animation. */
	float ElapsedTime = 0.0f;
};
