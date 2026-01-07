// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"

class UIVSmokeHoleGeneratorComponent;
struct FPostProcessMaterialInputs;

// ============================================================================
// Shader Parameters
// ============================================================================

/**
 * Shared parameters for volume cube ray marching visualization.
 */
BEGIN_SHADER_PARAMETER_STRUCT(FIVSmokeVolumeSliceParameters, )
	SHADER_PARAMETER_TEXTURE(Texture3D, VolumeTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, VolumeSampler)
	SHADER_PARAMETER(FMatrix44f, LocalToWorld)
	SHADER_PARAMETER(FMatrix44f, WorldToLocal)
	SHADER_PARAMETER(FMatrix44f, WorldToClip)
	SHADER_PARAMETER(FVector3f, VolumeExtent)
	SHADER_PARAMETER(FVector3f, CameraWorldPos)
	SHADER_PARAMETER(int32, NumSteps)
	SHADER_PARAMETER(float, StepOpacity)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

// ============================================================================
// Shader Classes
// ============================================================================

/**
 * Vertex shader for volume cube debug visualization.
 */
class IVSMOKE_API FIVSmokeVolumeSliceDebugVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FIVSmokeVolumeSliceDebugVS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeVolumeSliceDebugVS, FGlobalShader);

	using FParameters = FIVSmokeVolumeSliceParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/**
 * Pixel shader for volume cube debug visualization with ray marching.
 */
class IVSMOKE_API FIVSmokeVolumeSliceDebugPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FIVSmokeVolumeSliceDebugPS);
	SHADER_USE_PARAMETER_STRUCT(FIVSmokeVolumeSliceDebugPS, FGlobalShader);

	using FParameters = FIVSmokeVolumeSliceParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// ============================================================================
// Render Data
// ============================================================================

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

// ============================================================================
// Debug Renderer (SceneViewExtension + Rendering)
// ============================================================================

/**
 * @class FIVSmokeDebugRenderer
 * @brief Unified debug renderer for IVSmoke volume visualization.
 *        Combines SceneViewExtension and volume rendering logic.
 *
 * Usage:
 *   - FIVSmokeDebugRenderer::Get().Register(Component);
 *   - FIVSmokeDebugRenderer::Get().UpdateRenderData(Component);  // in Tick
 *   - FIVSmokeDebugRenderer::Get().Unregister(Component);
 */
class IVSMOKE_API FIVSmokeDebugRenderer : public FSceneViewExtensionBase
{
public:
	static FIVSmokeDebugRenderer& Get();

	// ============================================================================
	// Component Registration
	// ============================================================================

	/** Register a component for debug rendering. Initializes extension if needed. */
	void Register(UIVSmokeHoleGeneratorComponent* Component);

	/** Unregister a component from debug rendering. */
	void Unregister(UIVSmokeHoleGeneratorComponent* Component);

	/** Check if any debug components are registered (thread-safe) */
	bool HasAnyComponents() const;

	/** Update cached render data from game thread (call from Tick) */
	void UpdateRenderData(UIVSmokeHoleGeneratorComponent* Component);

	// ============================================================================
	// FSceneViewExtensionBase Interface
	// ============================================================================

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SubscribeToPostProcessingPass(
		EPostProcessingPass Pass,
		const FSceneView& InView,
		FPostProcessingPassDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled
	) override;
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

	FIVSmokeDebugRenderer(const FAutoRegister& AutoRegister);

private:
	static TSharedPtr<FIVSmokeDebugRenderer, ESPMode::ThreadSafe> Instance;

	// ============================================================================
	// Rendering
	// ============================================================================

	FScreenPassTexture Render_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs
	);

	void RenderVolumeCube(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FScreenPassRenderTarget& Output,
		const FIVSmokeDebugRenderData& RenderData
	);

	// ============================================================================
	// Data
	// ============================================================================

	TArray<TWeakObjectPtr<UIVSmokeHoleGeneratorComponent>> DebugComponents;
	TMap<uint32, FIVSmokeDebugRenderData> CachedRenderData;
	mutable FCriticalSection DataMutex;
};
