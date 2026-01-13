// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IVSmokeHoleData.generated.h"

// ============================================================================
// Hole Type Constants
// ============================================================================

namespace IVSmokeHoleType
{
	constexpr int32 Penetration = 0;
	constexpr int32 Explosion = 1;
}

// ============================================================================
// Request Structures (Public API - User Input)
// ============================================================================

/**
 * @struct FIVSmokePenetrationRequest
 * @brief Request data for creating a penetration hole
 */
USTRUCT(BlueprintType)
struct IVSMOKE_API FIVSmokePenetrationRequest
{
	GENERATED_BODY()

	/** Origin position of the shot (world space) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke")
	FVector Origin = FVector::ZeroVector;

	/** Direction of the shot (normalized) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke")
	FVector Direction = FVector::ForwardVector;

	/** Radius at entry point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke",
		meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float StartRadius = 50.0f;

	/** Radius at exit point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke",
		meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float EndRadius = 25.0f;

	/** Lifetime of the hole in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke",
		meta = (ClampMin = "0.1", ClampMax = "30.0"))
	float LifeTime = 3.0f;
};

/**
 * @struct FIVSmokeExplosionRequest
 * @brief Request data for creating an explosion hole
 */
USTRUCT(BlueprintType)
struct IVSMOKE_API FIVSmokeExplosionRequest
{
	GENERATED_BODY()

	/** Center position of the explosion (world space) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke")
	FVector Origin = FVector::ZeroVector;

	/** Radius of the explosion */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke",
		meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float Radius = 100.0f;

	/** Lifetime of the hole in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke",
		meta = (ClampMin = "0.1", ClampMax = "30.0"))
	float LifeTime = 3.0f;
};

// ============================================================================
// Internal Data Structure
// ============================================================================

/**
 * @struct FIVSmokeHoleData
 * @brief Internal data structure for a smoke hole created by processing Request structures
 */
USTRUCT(BlueprintType)
struct IVSMOKE_API FIVSmokeHoleData
{
	GENERATED_BODY()

	FIVSmokeHoleData() = default;

	// ============================================================================
	// Common Properties
	// ============================================================================

	/** Type of hole (0=Penetration/Cone, 1=Explosion/Sphere, 2+=Reserved) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke")
	int32 HoleType = IVSmokeHoleType::Penetration;

	/** World position where the hole starts (Penetration) or center (Explosion) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke")
	FVector Position = FVector::ZeroVector;

	/** Radius at start point (Penetration) or explosion radius (Explosion) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke",
		meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float Radius = 50.0f;

	// ============================================================================
	// Penetration-Only Properties
	// ============================================================================

	/** World position where the penetration exits the smoke volume */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Penetration")
	FVector EndPosition = FVector::ZeroVector;

	/** Radius at exit point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Penetration",
		meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float EndRadius = 25.0f;

	// ============================================================================
	// Lifetime Properties (Server Time Based)
	// ============================================================================

	/** Total lifetime of the hole in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Lifetime",
		meta = (ClampMin = "0.1", ClampMax = "30.0"))
	float InitialLifetime = 3.0f;

	/** Expiration time in server time (set by Authority) */
	UPROPERTY()
	float ExpirationServerTime = 0.0f;

	// ============================================================================
	// Helper Methods
	// ============================================================================

	/** Calculate normalized age (0.0 = just created, 1.0 = about to expire) */
	float GetNormalizedAge(const float CurrentServerTime) const
	{
		const float RemainingTime = ExpirationServerTime - CurrentServerTime;
		const float ElapsedTime = InitialLifetime - RemainingTime;
		return FMath::Clamp(ElapsedTime / InitialLifetime, 0.0f, 1.0f);
	}

	/** Check if this hole has expired (Authority only) */
	bool IsExpired(const float CurrentServerTime) const
	{
		return CurrentServerTime >= ExpirationServerTime;
	}

	/** Calculate AABB for this hole in local space */
	FBox CalculateLocalAABB(const FTransform& VolumeTransform) const
	{
		if (HoleType == IVSmokeHoleType::Explosion)
		{
			const FVector LocalCenter = VolumeTransform.InverseTransformPosition(Position);
			const FVector Extent(Radius);
			return FBox(LocalCenter - Extent, LocalCenter + Extent);
		}
		else // Penetration (Cone)
		{
			const FVector LocalStart = VolumeTransform.InverseTransformPosition(Position);
			const FVector LocalEnd = VolumeTransform.InverseTransformPosition(EndPosition);
			const float MaxRadius = FMath::Max(Radius, EndRadius);

			FBox PenetrationBox(ForceInit);
			PenetrationBox += LocalStart - FVector(MaxRadius);
			PenetrationBox += LocalStart + FVector(MaxRadius);
			PenetrationBox += LocalEnd - FVector(MaxRadius);
			PenetrationBox += LocalEnd + FVector(MaxRadius);
			return PenetrationBox;
		}
	}
};

// ============================================================================
// GPU Data Structure
// ============================================================================

/**
 * @struct FIVSmokeHoleGPU
 * @brief GPU-friendly structure for hole data.
 *        EdgeSoftness and DensityMultiplier are provided by HoleGeneratorComponent.
 */
struct FIVSmokeHoleGPU
{
	// Common (16 bytes)
	FVector3f Position;
	float Radius;

	// Penetration-only, ignored for Explosion (16 bytes)
	FVector3f EndPosition;
	float EndRadius;

	// Parameters (16 bytes)
	float EdgeSoftness;
	float DensityMultiplier;
	float NormalizedAge;      // 0.0 = just created, 1.0 = about to expire
	int32 HoleType;           // 0 = Penetration, 1 = Explosion

};  // Total: 48 bytes
