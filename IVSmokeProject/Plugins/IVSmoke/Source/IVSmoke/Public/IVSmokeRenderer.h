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

	TArray<TWeakObjectPtr<UIVSmokeVolumeComponent>> Volumes;
	mutable FCriticalSection VolumesMutex;
};
