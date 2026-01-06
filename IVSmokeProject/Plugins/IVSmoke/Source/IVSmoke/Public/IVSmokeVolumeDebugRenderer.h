// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ScreenPass.h"

class UIVSmokeCollisionComponent;
class FRDGBuilder;
class FSceneView;
struct FPostProcessMaterialInputs;

/**
 * Cached debug render data (thread-safe, copied from game thread)
 */
struct FIVSmokeDebugRenderData
{
	FTextureRHIRef VolumeTextureRHI;
	int32 Resolution = 0;
	int32 SliceIndex = 0;
	int32 DebugMode = 0;
	float CurrentTime = 0.0f;
	float HoleLifeTime = 0.0f;
	bool bIsValid = false;
};

/**
 * Debug renderer for volume texture visualization.
 * Renders Z-slices of registered collision components' volume textures.
 */
class IVSMOKE_API FIVSmokeVolumeDebugRenderer
{
public:
	static FIVSmokeVolumeDebugRenderer& Get();

	// ============================================================================
	// Component Registration
	// ============================================================================

	void Register(UIVSmokeCollisionComponent* Component);
	void Unregister(UIVSmokeCollisionComponent* Component);

	/** Check if any debug components are registered (thread-safe, no game object access) */
	bool HasAnyComponents() const;

	/** Update cached render data from game thread (call from Tick) */
	void UpdateRenderData(UIVSmokeCollisionComponent* Component);

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
	 * Render debug slice using cached data.
	 */
	void RenderDebugSlice(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FScreenPassRenderTarget& Output,
		const FIVSmokeDebugRenderData& RenderData
	);

	TArray<TWeakObjectPtr<UIVSmokeCollisionComponent>> DebugComponents;
	TMap<uint32, FIVSmokeDebugRenderData> CachedRenderData;  // Key: Component ID
	mutable FCriticalSection ComponentsMutex;
	mutable FCriticalSection RenderDataMutex;
};
