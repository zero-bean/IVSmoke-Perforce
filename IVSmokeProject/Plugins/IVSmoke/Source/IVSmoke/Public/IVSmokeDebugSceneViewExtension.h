// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

struct FPostProcessMaterialInputs;
struct FScreenPassTexture;

/**
 * Scene view extension for IVSmoke debug visualization.
 * Renders volume texture slices for debugging purposes.
 */
class IVSMOKE_API FIVSmokeDebugSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FIVSmokeDebugSceneViewExtension(const FAutoRegister& AutoRegister);

	static void Initialize();
	static void Shutdown();

	//~ Begin FSceneViewExtensionBase Interface
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SubscribeToPostProcessingPass(
		EPostProcessingPass Pass,
		const FSceneView& InView,
		FPostProcessingPassDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled) override;
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;
	//~ End FSceneViewExtensionBase Interface

private:
	static TSharedPtr<FIVSmokeDebugSceneViewExtension, ESPMode::ThreadSafe> Instance;

	FScreenPassTexture Render_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);
};
