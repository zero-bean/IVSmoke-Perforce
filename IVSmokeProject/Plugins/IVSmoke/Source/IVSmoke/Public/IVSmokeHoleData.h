// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
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

	FIVSmokeHoleData(const FVector& InPosition, const FVector& InDirection, 
		const double InRadius, const double InCreationTime)
		: Position(InPosition)
		, Direction(InDirection.GetSafeNormal())
		, Radius(InRadius)
		, CreationTime(InCreationTime) {}

	/** World position where the hole was created */
	UPROPERTY(BlueprintReadOnly, Category = "IVSmoke")
	FVector Position = FVector::ZeroVector;

	/** NORMALIZED Direction of penetration */
	UPROPERTY(BlueprintReadOnly, Category = "IVSmoke")
	FVector Direction = FVector::ForwardVector;

	/** Radius of the hole */
	UPROPERTY(BlueprintReadOnly, Category = "IVSmoke")
	double Radius = 0.0f;

	/** Time when this hole was created. */
	UPROPERTY(BlueprintReadOnly, Category = "IVSmoke")
	double CreationTime = 0.0f;
};
