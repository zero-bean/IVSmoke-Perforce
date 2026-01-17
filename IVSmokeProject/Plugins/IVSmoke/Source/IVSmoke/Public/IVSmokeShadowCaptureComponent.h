// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IVSmokeShadowCaptureComponent.generated.h"

/**
 * Scene capture component for volumetric smoke external shadow maps.
 * Captures orthographic depth from directional light's perspective.
 *
 * Managed globally by FIVSmokeRenderer as a singleton.
 * Covers combined AABB of all visible smoke volumes.
 */
UCLASS(ClassGroup = (IVSmoke), meta = (BlueprintSpawnableComponent))
class IVSMOKE_API UIVSmokeShadowCaptureComponent : public USceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	UIVSmokeShadowCaptureComponent();

	// ============================================================================
	// Public Interface
	// ============================================================================

	/**
	 * Initialize or resize render target for shadow depth capture.
	 * @param Resolution Shadow map resolution (width and height).
	 */
	void InitializeRenderTarget(int32 Resolution);

	/**
	 * Update shadow capture for combined volume bounds and light direction.
	 * Positions camera opposite to light direction, covering the entire AABB.
	 *
	 * @param LightDirection Normalized direction TOWARD the light source (opposite of light travel direction).
	 * @param CombinedAABB   World-space bounding box of all visible volumes.
	 */
	void UpdateCapture(const FVector& LightDirection, const FBox& CombinedAABB);

	/**
	 * Get shadow depth texture (thread-safe RHI reference).
	 * Safe to pass to Render Thread.
	 */
	FTextureRHIRef GetShadowDepthTexture() const;

	/** Get light view-projection matrix for shader coordinate transform. */
	FORCEINLINE const FMatrix& GetLightViewProjectionMatrix() const { return LightViewProjectionMatrix; }

	/** Get shadow camera position (for linear depth calculation). Returns value matching current texture. */
	FORCEINLINE const FVector& GetShadowCameraPosition() const { return ActiveCameraPosition; }

	/** Get shadow camera forward direction (light travel direction, FROM light). Returns value matching current texture. */
	FORCEINLINE const FVector& GetShadowCameraForward() const { return ActiveCameraForward; }

	/** Check if render target is valid and ready for use. */
	FORCEINLINE bool IsReady() const { return ShadowDepthRT != nullptr && ShadowDepthRT->GetResource() != nullptr; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// ============================================================================
	// Internal Methods
	// ============================================================================

	/**
	 * Calculate orthographic view-projection matrix from light direction and bounds.
	 * Ensures the entire AABB is covered by the orthographic frustum.
	 */
	void CalculateLightViewProjection(const FVector& LightDir, const FBox& InBounds);

	/**
	 * Configure scene capture settings for depth-only rendering.
	 * Disables unnecessary rendering features for performance.
	 */
	void ConfigureCaptureSettings();

	// ============================================================================
	// Member Variables
	// ============================================================================

	/** Render target for shadow depth capture. */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ShadowDepthRT;

	/** Combined view-projection matrix for world-to-light-clip transform. */
	FMatrix LightViewProjectionMatrix;

	/** Current render target resolution. */
	int32 CurrentResolution = 0;

	// ============================================================================
	// Double-buffered values to match texture timing
	// "Active" values = match current texture (returned by getters)
	// "Pending" values = for next frame's capture
	// ============================================================================

	/** Active camera forward direction (light travel direction, FROM light source).
	 *  Used in shader for linear depth calculation. Returned by GetShadowCameraForward(). */
	FVector ActiveCameraForward = FVector(0.0f, 0.0f, -1.0f);

	/** Active camera position (matches current texture). Returned by GetShadowCameraPosition(). */
	FVector ActiveCameraPosition = FVector::ZeroVector;

	/** Active OrthoWidth (matches current texture). Used for matrix calculation. */
	float ActiveOrthoWidth = 1000.0f;

	/** Active near/far planes (matches current texture). */
	float ActiveNearClipPlane = 0.0f;
	float ActiveFarClipPlane = 10000.0f;

	/** Pending values for next frame's capture. */
	FVector PendingCameraForward = FVector(0.0f, 0.0f, -1.0f);
	FVector PendingCameraPosition = FVector::ZeroVector;
	FBox PendingAABB;
	float PendingOrthoWidth = 1000.0f;
	float PendingNearClipPlane = 0.0f;
	float PendingFarClipPlane = 10000.0f;
};
