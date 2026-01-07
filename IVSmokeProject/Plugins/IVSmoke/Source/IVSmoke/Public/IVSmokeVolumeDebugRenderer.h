// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ScreenPass.h"

class UIVSmokeHoleGeneratorComponent;
class FRDGBuilder;
class FSceneView;

/**
 * Cached debug render data (thread-safe, copied from game thread)
 */
struct FIVSmokeDebugRenderData
{
	FTextureRHIRef VolumeTextureRHI;
	FIntVector Resolution = {};
	bool bIsValid = false;

	// World-space rendering data
	FMatrix LocalToWorld = FMatrix::Identity;
	FMatrix WorldToLocal = FMatrix::Identity;
	FVector VolumeExtent = FVector::ZeroVector;
	int32 NumSteps = 64;
	float StepOpacity = 1.0f;
};

/**
 * Debug renderer for volume texture visualization.
 * Renders volume as a cube with ray marching.
 */
class IVSMOKE_API FIVSmokeVolumeDebugRenderer
{
public:
	static FIVSmokeVolumeDebugRenderer& Get();

	// ============================================================================
	// Component Registration
	// ============================================================================

	void Register(UIVSmokeHoleGeneratorComponent* Component);
	void Unregister(UIVSmokeHoleGeneratorComponent* Component);

	/** Check if any debug components are registered (thread-safe, no game object access) */
	bool HasAnyComponents() const;

	/** Update cached render data from game thread (call from Tick) */
	void UpdateRenderData(UIVSmokeHoleGeneratorComponent* Component);

	// ============================================================================
	// Rendering
	// ============================================================================

	/**
	 * Render debug visualization for all registered components.
	 * Called from SceneViewExtension.
	 *
	 * @param GraphBuilder    RDG builder
	 * @param View            Current scene view
	 * @param SceneColor      Scene color texture to render on top of
	 * @return Output texture after debug rendering
	 */
	FScreenPassTexture Render(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassTexture& SceneColor
	);

private:
	FIVSmokeVolumeDebugRenderer() = default;

	/**
	 * Render volume cube with ray marching.
	 */
	void RenderVolumeCube(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FScreenPassRenderTarget& Output,
		const FIVSmokeDebugRenderData& RenderData
	);

	TArray<TWeakObjectPtr<UIVSmokeHoleGeneratorComponent>> DebugComponents;
	TMap<uint32, FIVSmokeDebugRenderData> CachedRenderData;  // Key: Component ID
	mutable FCriticalSection ComponentsMutex;
	mutable FCriticalSection RenderDataMutex;
};
