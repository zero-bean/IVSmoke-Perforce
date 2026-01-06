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
	 * Ray Marching CS Pass.
	 * Calculates volumetric smoke and outputs to intermediate texture.
	 * Scene depth is accessed via SceneTexturesStruct uniform buffer.
	 *
	 * @param GraphBuilder       RDG builder
	 * @param View               Current scene view
	 * @param OutputTexture      UAV texture to write ray marching result
	 * @param ViewportSize       Size of the viewport for dispatch and UV calculation
	 * @param ViewRectMin        Offset into full scene texture for depth sampling
	 */
	void AddRayMarchPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef OutputTexture,
		const FIntPoint& ViewportSize,
		const FIntPoint& ViewRectMin
	);

	/**
	 * Composite PS Pass.
	 * Blends ray marching result with scene color.
	 *
	 * @param GraphBuilder    RDG builder
	 * @param View            Current scene view
	 * @param SmokeTexture    Ray marching result texture
	 * @param Output          Final render target
	 * @param ViewportSize    Size of the viewport for UV calculation
	 */
	void AddCompositePass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef SmokeTexture,
		const FScreenPassRenderTarget& Output,
		const FIntPoint& ViewportSize
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
