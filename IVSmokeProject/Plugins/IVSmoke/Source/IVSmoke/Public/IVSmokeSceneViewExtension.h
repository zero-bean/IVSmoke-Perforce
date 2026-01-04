// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "IVSmokePostProcessPass.h"

struct FPostProcessMaterialInputs;
struct FScreenPassTexture;

/**
 * Scene view extension for IVSmoke post-process effects.
 * Uses SubscribeToPostProcessingPass pattern with public API only.
 * Supports both Pixel Shader and Compute Shader rendering paths.
 */
class IVSMOKE_API FIVSmokeSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FIVSmokeSceneViewExtension(const FAutoRegister& AutoRegister);

	/** Initialize the scene view extension singleton. */
	static void Initialize();

	/** Shutdown and release the scene view extension. */
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
	/** Singleton instance. */
	static TSharedPtr<FIVSmokeSceneViewExtension, ESPMode::ThreadSafe> Instance;

	/** Main render callback for post-process pass. */
	FScreenPassTexture Render_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	/** Render using pixel shader path. */
	void RenderWithPixelShader(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FScreenPassRenderTarget& Output);

	/** Render using compute shader path. */
	void RenderWithComputeShader(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef SceneColorTexture,
		FRDGTextureRef OutputTexture);
};
