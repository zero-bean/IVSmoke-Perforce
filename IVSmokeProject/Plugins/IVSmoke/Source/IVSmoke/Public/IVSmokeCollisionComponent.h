// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
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
	void TryUpdateCollision(const TArray<uint64>& VoxelBitArray, const FIntVector& GridResolution, float VoxelSize, int32 ActiveVoxelNum, float SyncTime, bool bForce = false);

	/** @todo Documentation */
	void ResetCollision();

	// @todo Documentation
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Config")
	bool bCollisionEnabled = true;

	// @todo Documentation
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Config")
	FName SmokeCollisionProfileName = UCollisionProfile::NoCollision_ProfileName;

	// @todo Documentation
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Config")
	TArray<TEnumAsByte<ECollisionChannel>> BlockChannelArray;

	// @todo Documentation
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Config", meta = (ClampMin = "1"))
	int32 MinCollisionUpdateVoxelNum = 50;

	// @todo Documentation
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Config", meta = (ClampMin = "0.0"))
	float MinCollisionUpdateInterval = 0.25f;

private:
	/** @todo Documentation */
	void UpdateCollision(const TArray<uint64>& VoxelBitArray, const FIntVector& GridResolution, float VoxelSize);

	/** @todo Documentation */
	void ApplyCollisionSettings();

	/** @todo Documentation */
	void FinalizePhysicsUpdate();

	// @todo Documentation
	UPROPERTY(Transient)
	TObjectPtr<UBodySetup> VoxelBodySetup;

	float LastSyncTime = 0.0f;

	int32 LastActiveVoxelNum = 0;
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
