// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "IVSmokeSmokePreset.generated.h"

/**
 * Noise generation settings for volumetric smoke.
 */
USTRUCT(BlueprintType)
struct IVSMOKE_API FIVSmokeNoiseSettings
{
	GENERATED_BODY()

	/** Random seed for noise generation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise")
	int32 Seed = 0;

	/** Texture resolution (TexSize x TexSize x TexSize). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise", meta = (ClampMin = "16", ClampMax = "512"))
	int32 TexSize = 128;

	/** Number of noise octaves for detail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise", meta = (ClampMin = "1", ClampMax = "8"))
	int32 Octaves = 6;

	/** Noise wrap factor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Wrap = 0.76f;

	/** Noise amplitude. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Amplitude = 0.62f;

	/** Number of cells per axis for Worley noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise", meta = (ClampMin = "1", ClampMax = "16"))
	int32 AxisCellCount = 4;

	/** Size of each cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Noise", meta = (ClampMin = "8", ClampMax = "128"))
	int32 CellSize = 32;
};

/**
 * Data asset containing smoke appearance and behavior settings.
 * Create presets for different smoke styles (thick smoke, light fog, etc.)
 */
UCLASS(BlueprintType)
class IVSMOKE_API UIVSmokeSmokePreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ============================================================================
	// Appearance
	// ============================================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Noise", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float NoiseUVMul = 0.39f;

	/** Base color of the smoke. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Appearance")
	FLinearColor SmokeColor = FLinearColor(0.8f, 0.8f, 0.8f, 1.0f);

	/** Light absorption coefficient. Higher = denser smoke. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Appearance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SmokeAbsorption = 0.1f;

	/** Base density multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Appearance", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float VolumeDensity = 1.0f;

	// ============================================================================
	// Edge Falloff
	// ============================================================================

	/** Controls edge softness. Lower = softer edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Falloff", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SmokeDensityFalloff = 0.2f;

	/** Scale for noise sampling. Affects smoke detail size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Falloff", meta = (ClampMin = "1.0", ClampMax = "1000.0"))
	float SmokeSize = 128.0f;

	// ============================================================================
	// Animation
	// ============================================================================

	/** Wind direction and speed for smoke animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Animation")
	FVector WindDirection = FVector(0.01f, 0.02f, 0.1f);

	// ============================================================================
	// Ray Marching
	// ============================================================================

	/** Maximum ray marching steps. Higher = better quality, lower performance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|RayMarching", meta = (ClampMin = "16", ClampMax = "512"))
	int32 MaxSteps = 256;

	// ============================================================================
	// Post Processing
	// ============================================================================

	/**
	 * Controls sharpening/blurring of the smoke composite.
	 * Positive = sharpen (enhances detail but may show grain)
	 * Zero = no filter (default)
	 * Negative = blur (reduces grain but softens edges)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|PostProcessing", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Sharpness = 0.0f;

	// ============================================================================
	// Rayleigh Scattering
	// ============================================================================

	/** Enable Rayleigh scattering for atmospheric light effects. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Scattering")
	bool bEnableScattering = true;

	/** Scattering intensity multiplier. Higher = more light scattered through smoke. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Scattering", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float ScatterScale = 0.5f;

	/** Override light direction instead of using scene directional light. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Scattering")
	bool bOverrideLightDirection = false;

	/** Custom light direction (normalized). Used when bOverrideLightDirection is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Scattering", meta = (EditCondition = "bOverrideLightDirection"))
	FVector LightDirectionOverride = FVector(0.0f, 0.0f, 1.0f);

	/** Override light color instead of using scene directional light. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Scattering")
	bool bOverrideLightColor = false;

	/** Custom light color. Used when bOverrideLightColor is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Scattering", meta = (EditCondition = "bOverrideLightColor"))
	FLinearColor LightColorOverride = FLinearColor::White;

	/** Anisotropy parameter for Henyey-Greenstein phase function.
	 *  0 = isotropic, positive = forward scattering, negative = backward scattering */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Scattering", meta = (ClampMin = "-0.99", ClampMax = "0.99", UIMin = "-0.99", UIMax = "0.99"))
	float ScatteringAnisotropy = 0.5f;

	// ============================================================================
	// Self-Shadowing (Light Marching)
	// ============================================================================

	/** Enable self-shadowing (light marching) for more realistic smoke appearance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|SelfShadowing")
	bool bEnableSelfShadowing = true;

	/** Number of steps for light marching. Higher = better quality, lower performance.
	 *  Recommended: 4-8 for real-time rendering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|SelfShadowing", meta = (ClampMin = "1", ClampMax = "16", UIMin = "1", UIMax = "16", EditCondition = "bEnableSelfShadowing"))
	int32 LightMarchingSteps = 6;

	/** Maximum distance to march toward light source (in world units).
	 *  Limits the light marching range for performance.
	 *  0 = No limit (march to volume boundary). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|SelfShadowing", meta = (ClampMin = "0.0", ClampMax = "500.0", EditCondition = "bEnableSelfShadowing"))
	float LightMarchingDistance = 0.0f;

	/** Exponential distribution factor for light marching steps.
	 *  Higher = more samples near the surface, fewer far away.
	 *  1.0 = uniform distribution, 2.0~3.0 = recommended for natural shadows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|SelfShadowing", meta = (ClampMin = "1.0", ClampMax = "5.0", EditCondition = "bEnableSelfShadowing"))
	float LightMarchingExpFactor = 2.0f;

	/** Minimum brightness in fully shadowed areas.
	 *  0 = completely dark, 1 = no shadow effect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|SelfShadowing", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableSelfShadowing"))
	float ShadowAmbient = 0.2f;


	/** Minimum brightness in fully shadowed areas.
 *  0 = completely dark, 1 = no shadow effect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|FXAA", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float FXAASpanMax = 4.0f;

	/** Minimum brightness in fully shadowed areas.
 *  0 = completely dark, 1 = no shadow effect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|FXAA", meta = (ClampMin = "0.1", ClampMax = "8.0"))
	float FXAARange = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|FXAA", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float FXAASharpness = 1.7f;
	// ============================================================================
	// UPrimaryDataAsset Interface
	// ============================================================================

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("IVSmokeSmokePreset"), GetFName());
	}
};
