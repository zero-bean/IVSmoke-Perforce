// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ScreenPass.h"

class UIVSmokeVolumeComponent;
class FRDGBuilder;
class FSceneView;
struct FPostProcessMaterialInputs;

/**
 * Manages registered smoke volumes and handles rendering.
 */
class IVSMOKE_API FIVSmokeRenderer
{
public:
	static FIVSmokeRenderer& Get();

	// ============================================================================
	// Volume Management
	// ============================================================================

	void AddVolume(UIVSmokeVolumeComponent* Volume);
	void RemoveVolume(UIVSmokeVolumeComponent* Volume);

	bool HasVolumes() const;
	TArray<FBox> GatherVolumeBounds() const;

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
	// Pass Functions
	// ============================================================================

	/**
	 * Ray Marching CS Pass.
	 * Calculates volumetric smoke and outputs to intermediate texture.
	 *
	 * @param GraphBuilder    RDG builder
	 * @param View            Current scene view
	 * @param OutputTexture   UAV texture to write ray marching result
	 * @param ViewportSize    Size of the viewport for dispatch and UV calculation
	 */
	void AddRayMarchPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef OutputTexture,
		const FIntPoint& ViewportSize
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

	TArray<TWeakObjectPtr<UIVSmokeVolumeComponent>> Volumes;
	mutable FCriticalSection VolumesMutex;
};
