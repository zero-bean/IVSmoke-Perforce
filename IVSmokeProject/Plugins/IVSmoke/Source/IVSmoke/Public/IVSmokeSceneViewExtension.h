// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

/**
 * Hooks into the rendering pipeline to trigger smoke rendering at the appropriate time.
 * Delegates actual rendering to FIVSmokeRenderer.
 */
class IVSMOKE_API FIVSmokeSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FIVSmokeSceneViewExtension(const FAutoRegister& AutoRegister);

	static void Initialize();
	static void Shutdown();

	// FSceneViewExtensionBase interface
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs) override;
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

private:
	static TSharedPtr<FIVSmokeSceneViewExtension, ESPMode::ThreadSafe> Instance;
};
