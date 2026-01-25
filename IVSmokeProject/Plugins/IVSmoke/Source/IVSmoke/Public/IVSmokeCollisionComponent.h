// Copyright (c) 2026, Team SDB. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/CollisionProfile.h"
#include "IVSmokeCollisionComponent.generated.h"

/**
 * A primitive component that dynamically generates collision geometry based on the voxel grid data.
 *
 * ## Overview
 * Unlike standard static meshes, this component constructs a set of box colliders (AggGeom)
 * representing the active voxels. It uses a binary greedy meshing algorithm to merge adjacent voxels into
 * larger boxes to minimize the physics cost.
 *
 * ## Usage
 * This is primarily designed for query-only interactions, such as:
 * - Blocking AI Line of Sight (Visibility channel).
 * - Preventing camera clipping.
 * - Simple projectile blocking.
 *
 * @note Frequent updates to collision geometry are expensive. Use `MinCollisionUpdateInterval`
 * and `MinCollisionUpdateVoxelNum` to throttle updates.
 */
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
	/**
	 * Attempts to update the collision geometry based on the current voxel data.
	 *
	 * It checks `MinCollisionUpdateInterval` and `MinCollisionUpdateVoxelNum` to throttle updates
	 * and prevent performance spikes from frequent physics rebuilding.
	 *
	 * @param VoxelBitArray		A bitmask buffer where each `uint64` element represents a row of voxels along the X-axis.
	 *							@warning The Grid X-resolution must not exceed 64.
	 * @param GridResolution	The resolution of the voxel grid (Width, Depth, Height).
	 * @param VoxelSize			World space size of a single voxel.
	 * @param ActiveVoxelNum	Current count of active voxels (used for threshold checks).
	 * @param SyncTime			Current synchronized world time (used for interval checks).
	 * @param bForce			If true, bypasses optimization checks and forces an immediate rebuild.
	 */
	void TryUpdateCollision(const TArray<uint64>& VoxelBitArray, const FIntVector& GridResolution, float VoxelSize, int32 ActiveVoxelNum, float SyncTime, bool bForce = false);

	/**
	 * Clears all generated physics geometry and resets the collision state.
	 * Called when the simulation is stopped or reset to ensure no "ghost" collision remains.
	 */
	void ResetCollision();

	/** Master switch for voxel collision. If false, no physics geometry will be generated, and all update requests will be ignored. */
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Config")
	bool bCollisionEnabled = true;

	/**
	 * The collision profile to apply to the generated geometry.
	 * Defaults to `NoCollision`. Change this to `BlockAll` or a custom profile to enable interaction.
	 */
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Config", meta = (EditCondition = "bCollisionEnabled"))
	FName SmokeCollisionProfileName = UCollisionProfile::NoCollision_ProfileName;

	/**
	 * List of specific collision channels to set to `ECR_Block`.
	 * Useful if you want to block only specific traces (e.g., Visibility) without affecting physical movement.
	 * Overrides the settings from `SmokeCollisionProfileName` if specified.
	 */
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Config", meta = (EditCondition = "bCollisionEnabled"))
	TArray<TEnumAsByte<ECollisionChannel>> BlockChannelArray;

	/** The minimum number of voxel changes (spawned or destroyed) required to trigger a physics geometry rebuild. */
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Config", meta = (EditCondition = "bCollisionEnabled", ClampMin = "1", UIMin = "10", UIMax = "1000"))
	int32 MinCollisionUpdateVoxelNum = 50;

	/** The minimum time (in seconds) that must pass between two consecutive physics geometry rebuilds. */
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Config", meta = (EditCondition = "bCollisionEnabled", ClampMin = "0.0", UIMax = "2.0"))
	float MinCollisionUpdateInterval = 0.25f;

private:
	/**
	 * Core algorithm that converts raw voxel data into physics geometry.
	 * Uses a greedy meshing approach to merge adjacent voxels into larger `FKBoxElem` boxes,
	 * significantly reducing the number of physics bodies required.
	 * @note This is a computationally expensive operation (O(N) on grid size).
	 */
	void UpdateCollision(const TArray<uint64>& VoxelBitArray, const FIntVector& GridResolution, float VoxelSize);

	/** Applies the user-configured collision profile and channel responses to the generated BodyInstance. */
	void ApplyCollisionSettings();

	/** Commits the new geometry to the physics engine. */
	void FinalizePhysicsUpdate();

	/** Transient BodySetup used to store the dynamic collision geometry (AggGeom). */
	UPROPERTY(Transient)
	TObjectPtr<UBodySetup> VoxelBodySetup;

	/** Timestamp of the last successful collision update. Used for throttling. */
	float LastSyncTime = 0.0f;

	/** Voxel count at the last update. Used to detect if the shape has changed significantly. */
	int32 LastActiveVoxelNum = 0;
#pragma endregion

	//~==============================================================================
	// Debug
#pragma region Debug
public:
	/**
	 * Renders wireframe boxes for each generated collision element (`FKBoxElem`).
	 * Useful for visualizing how the greedy meshing algorithm has optimized the voxels.
	 */
	void DrawDebugVisualization() const;

	/**
	 * If true, draws debug visualization for the collision geometry in the editor.
	 * @note Only works if `IVSmokeDebugSettings::bDebugEnabled` is also true on the main actor.
	 */
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug")
	bool bDebugEnabled = false;

#pragma endregion
};
