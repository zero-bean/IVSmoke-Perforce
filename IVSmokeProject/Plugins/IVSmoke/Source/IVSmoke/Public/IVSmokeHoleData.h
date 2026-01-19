// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IVSmokeHoleData.generated.h"

/**
 * @struct FIVSmokeHoleData
 * @brief Network-optimized hole data structure.
 */
USTRUCT(BlueprintType)
struct IVSMOKE_API FIVSmokeHoleData
{
	GENERATED_BODY()

	FIVSmokeHoleData() = default;

	// World position where the hole starts
	UPROPERTY()
	FVector Position = FVector::ZeroVector;

	// World position where the penetration exits (Penetration only)
	UPROPERTY()
	FVector EndPosition = FVector::ZeroVector;

	// Hole expiration time (server based)
	UPROPERTY()
	float ExpirationServerTime = 0.0f;

	// Preset ID
	UPROPERTY()
	uint8 PresetID = 0;

	// Check if this hole has expired
	FORCEINLINE bool IsExpired(const float CurrentServerTime) const { return CurrentServerTime >= ExpirationServerTime; }
};

/**
 * @struct FIVSmokeHoleGPU
 * @brief Built from FIVSmokeHoleData + UIVSmokeHolePreset at render time.
 */
struct FIVSmokeHoleGPU
{
	// Position (16 bytes)
	FVector3f Position;
	float Radius;

	// Penetration-only (16 bytes)
	FVector3f EndPosition;
	float EndRadius;

	// Parameters from Preset (16 bytes)
	float EdgeSoftness;
	float DensityMultiplier;
	float NormalizedAge;
	int32 HoleType;

};  // Total: 48 bytes
