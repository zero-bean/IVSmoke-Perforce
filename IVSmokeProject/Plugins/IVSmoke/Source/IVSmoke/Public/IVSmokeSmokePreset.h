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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|RayMarching", meta = (ClampMin = "16", ClampMax = "256"))
	int32 MaxSteps = 64;

	// ============================================================================
	// UPrimaryDataAsset Interface
	// ============================================================================

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("IVSmokeSmokePreset"), GetFName());
	}
};
