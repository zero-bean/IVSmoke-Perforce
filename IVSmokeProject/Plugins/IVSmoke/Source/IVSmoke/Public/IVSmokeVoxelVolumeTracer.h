// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IVSmokeVoxelVolumeTracer.generated.h"

/**
 * @struct FIVSmokeVoxelVolumeTracer
 * @brief Traces integer-based voxel path using 3D Bresenham algorithm.
 * @todo Network sync strategy:
 *       - Server calculates StartVoxel, EndVoxel (integers)
 *       - Send integers to clients (StartPoint, EndPoint, Creation Time)
 */
USTRUCT(BlueprintType)
struct IVSMOKE_API FIVSmokeVoxelVolumeTracer
{
	GENERATED_BODY()

	/**
	 * @brief Trace voxels between two points using 3D Bresenham
	 * @param StartVoxel Starting voxel index (integer, network-safe)
	 * @param EndVoxel Ending voxel index (integer, network-safe)
	 * @param Resolution Grid resolution for bounds checking (X x Y x Z)
	 * @param OutVoxelIndices Output array of traversed voxel indices
	 * @return Number of voxels traced
	 */
	static int32 Trace(
		const FIntVector& StartVoxel,
		const FIntVector& EndVoxel,
		const FIntVector& Resolution,
		TArray<FIntVector>& OutVoxelIndices);

	/**
	 * @brief Calculate end voxel from entry point and direction
	 * @param EntryVoxel Entry voxel index
	 * @param Direction Normalized direction vector
	 * @param Resolution Grid resolution (X x Y x Z)
	 * @param MaxDistance Maximum trace distance in voxel units (default: max(Resolution) * 2)
	 * @return Exit voxel index (clamped to grid bounds)
	 * @todo In production, must be called on SERVER, then EndVoxel replicated to clients
	 */
	static FIntVector CalculateExitVoxel(
		const FIntVector& EntryVoxel,
		const FVector& Direction,
		const FIntVector& Resolution,
		int32 MaxDistance = 0);

	/**
	 * @brief Collect voxels within a cone (frustum) shape along a trace line.
	 *        Uses float distance calculation to avoid staircase artifacts.
	 * @param StartLocalPos Starting position in local space (float precision)
	 * @param EndLocalPos Ending position in local space (float precision)
	 * @param StartRadius Radius at start point (world units) - typically larger
	 * @param EndRadius Radius at end point (world units) - typically smaller
	 * @param VoxelSize Size of each voxel in world units
	 * @param Resolution Grid resolution for bounds checking (X x Y x Z)
	 * @param OutVoxelIndices Output array of voxel indices within the cone
	 * @return Number of voxels collected
	 */
	static int32 CollectVoxelsInCone(
		const FVector& StartLocalPos,
		const FVector& EndLocalPos,
		float StartRadius,
		float EndRadius,
		const FVector& VoxelSize,
		const FIntVector& Resolution,
		TArray<FIntVector>& OutVoxelIndices);

private:
	/** Check if voxel index is within valid range */
	FORCEINLINE static bool IsValidVoxel(const FIntVector& Index, const FIntVector& Resolution)
	{
		return Index.X >= 0 && Index.X < Resolution.X &&
		       Index.Y >= 0 && Index.Y < Resolution.Y &&
		       Index.Z >= 0 && Index.Z < Resolution.Z;
	}

	/** Clamp voxel index to valid range */
	FORCEINLINE static FIntVector ClampVoxel(const FIntVector& Index, const FIntVector& Resolution)
	{
		return FIntVector(
			FMath::Clamp(Index.X, 0, Resolution.X - 1),
			FMath::Clamp(Index.Y, 0, Resolution.Y - 1),
			FMath::Clamp(Index.Z, 0, Resolution.Z - 1)
		);
	}
};
