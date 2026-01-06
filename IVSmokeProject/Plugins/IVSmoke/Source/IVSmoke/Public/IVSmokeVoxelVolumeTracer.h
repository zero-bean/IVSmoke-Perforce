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
	 * @param Resolution Grid resolution for bounds checking
	 * @param OutVoxelIndices Output array of traversed voxel indices
	 * @return Number of voxels traced
	 */
	static int32 Trace(
		const FIntVector& StartVoxel,
		const FIntVector& EndVoxel,
		const int32 Resolution,
		TArray<FIntVector>& OutVoxelIndices);

	/**
	 * @brief Calculate end voxel from entry point and direction
	 * @param EntryVoxel Entry voxel index
	 * @param Direction Normalized direction vector
	 * @param Resolution Grid resolution
	 * @param MaxDistance Maximum trace distance in voxel units (default: Resolution * 2)
	 * @return Exit voxel index (clamped to grid bounds)
	 * @todo In production, must be called on SERVER, then EndVoxel replicated to clients
	 */
	static FIntVector CalculateExitVoxel(
		const FIntVector& EntryVoxel,
		const FVector& Direction,
		const int32 Resolution,
		int32 MaxDistance = 0);

private:
	/** Check if voxel index is within valid range */
	FORCEINLINE static bool IsValidVoxel(const FIntVector& Index, const int32 Resolution)
	{
		return Index.X >= 0 && Index.X < Resolution &&
		       Index.Y >= 0 && Index.Y < Resolution &&
		       Index.Z >= 0 && Index.Z < Resolution;
	}

	/** Clamp voxel index to valid range */
	FORCEINLINE static FIntVector ClampVoxel(const FIntVector& Index, const int32 Resolution)
	{
		return FIntVector(
			FMath::Clamp(Index.X, 0, Resolution - 1),
			FMath::Clamp(Index.Y, 0, Resolution - 1),
			FMath::Clamp(Index.Z, 0, Resolution - 1));
	}
};
