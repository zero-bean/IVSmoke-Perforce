// Copyright Team SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IVSmokeSampleGrenade.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class AIVSmokeVoxelVolume;

/**
 * Sample smoke grenade projectile for IVSmoke plugin demonstration.
 * Bounces on surfaces and detonates when stopped.
 */
UCLASS()
class IVSMOKE_API AIVSmokeSampleGrenade : public AActor
{
	GENERATED_BODY()

public:
	AIVSmokeSampleGrenade();

protected:
	virtual void BeginPlay() override;

public:
	/** Collision component for hit detection */
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Grenade")
	TObjectPtr<USphereComponent> CollisionComponent;

	/** Visual mesh representation */
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Grenade")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/** Handles projectile movement and bouncing */
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Grenade")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	/** VoxelVolume class to spawn on detonation */
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Grenade")
	TSubclassOf<AIVSmokeVoxelVolume> SmokeVoxelVolume;

protected:
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Grenade",
		meta = (ClampMin = 1.0, ClampMax = 100.0, Units = "cm"))
	float CollisionRadius = 8.0f;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Grenade",
		meta = (ClampMin = 0.0, ClampMax = 10000.0, Units = "cm/s"))
	float InitialSpeed = 2000.0f;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Grenade",
		meta = (ClampMin = 0.0, ClampMax = 5.0))
	float GravityScale = 1.0f;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Grenade",
		meta = (ClampMin = 0.0, ClampMax = 1.0))
	float Bounciness = 0.4f;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Grenade",
		meta = (ClampMin = 0.0, ClampMax = 1.0))
	float Friction = 0.5f;

	UFUNCTION()
	void OnProjectileStopped(const FHitResult& HitResult);

private:
	bool bHasDetonated = false;
};
