// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "IVSmokeOctree.h"
#include "Components/PrimitiveComponent.h"
#include "IVSmokeCollisionComponent.generated.h"

/** @todo Documentation */
UCLASS(ClassGroup = (IVSmoke), meta = (BlueprintSpawnableComponent))
class IVSMOKE_API UIVSmokeCollisionComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

	//~==============================================================================
	// Component Lifecycle
#pragma region Lifecycle
public:
	UIVSmokeCollisionComponent();

	virtual UBodySetup* GetBodySetup() override;

protected:
	virtual void OnCreatePhysicsState() override;
#pragma endregion

	//~==============================================================================
	// Collision Management
#pragma region Collision
public:
	/** @todo Documentation */
	void UpdateCollision(const TArray<float>& VoxelData, const FIntVector& GridResolution, float VoxelSize);

	/** @todo Documentation */
	void ResetCollision();

	/** @todo Documentation */
	FORCEINLINE const FIVSmokeOctree& GetOctree() const { return Octree; }

	// @todo Documentation
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Config")
	bool bCollisionEnabled = true;

	// @todo Documentation
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Config")
	FName SmokeCollisionProfileName = UCollisionProfile::NoCollision_ProfileName;

	// @todo Documentation
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Config")
	TArray<TEnumAsByte<ECollisionChannel>> BlockChannelArray;

private:
	/** @todo Documentation */
	void RebuildPhysicsGeometry();

	// @todo Documentation
	UPROPERTY(Transient)
	TObjectPtr<UBodySetup> VoxelBodySetup;

	// @todo Documentation
	FIVSmokeOctree Octree;
#pragma endregion

	//~==============================================================================
	// Debug
#pragma region Debug
public:
	/** @todo Documentation */
	void DrawDebugVisualization() const;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug")
	bool bDebugEnabled = false;

#pragma endregion
};
