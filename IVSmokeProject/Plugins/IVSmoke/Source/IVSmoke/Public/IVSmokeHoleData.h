// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IVSmokeHoleCarveCS.h"
#include "IVSmokeHoleData.generated.h"

/**
 * @struct FIVSmokeHoleData
 * @brief Data structure for a smoke hole created by bullet penetration.
 *        Used for both network replication and GPU buffer transmission.
 * @todo  In prototype, uses local time (GetWorld()->GetTimeSeconds()) 
 * @todo  but production Should be replaced with server time. (GetWorld()->GetGameState()->GetServerWorldTimeSeconds())
 */
USTRUCT(BlueprintType)
struct IVSMOKE_API FIVSmokeHoleData
{
	GENERATED_BODY()

	FIVSmokeHoleData() = default;

	/** World position where the hole was created */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke")
	FVector Position = FVector::ZeroVector;

	/** NORMALIZED Direction of penetration */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke")
	FVector Direction = FVector::ForwardVector;

	/** Radius of the hole at entry point */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke",
		meta = (ClampMin = "1.0", ClampMax = "200.0"))
	float Radius = 50.0f;

	/** Ratio of exit radius to entry radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EndRadiusRatio = 0.5f;

	/** Shape type for hole carving (Sphere or Box SDF) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke")
	EIVSmokeHoleShape ShapeType = EIVSmokeHoleShape::Sphere;

	/** Edge softness for hole boundaries (0=hard edge, 1=soft gradient) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EdgeSoftness = 0.3f;

	/** Density multiplier for holes (0=transparent, 1=full hole) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DensityMultiplier = 1.0f;

	/** Time when this hole was created (auto-filled by component) */
	UPROPERTY(BlueprintReadOnly, Category = "IVSmoke")
	double CreationTime = 0.0f;
};
