// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "IVSmokeVolumeComponent.generated.h"

/**
 * Volume data holder for smoke ray marching.
 * Defines bounding box via two vectors.
 */
UCLASS(ClassGroup=(IVSmoke), meta=(BlueprintSpawnableComponent))
class IVSMOKE_API UIVSmokeVolumeComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UIVSmokeVolumeComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume")
	FVector BoundsMin = FVector(-100.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Volume")
	FVector BoundsMax = FVector(100.f);

	FBox GetWorldBounds() const;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
};
