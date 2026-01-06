// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IVSmokeGridLibrary.h"
#include "IVSmokeVoxelVolumeCurator.generated.h"

/**
 * @struct FIVSmokeVoxelVolumeCurator
 * @brief Manages 3D voxel data for smoke density with hole support.
 *        Data is stored directly in GPU-ready RGBA32F format.
 *        R=Density, G=CreationTime, BA=Reserved for future use.
 */
USTRUCT(BlueprintType)
struct IVSMOKE_API FIVSmokeVoxelVolumeCurator
{
	GENERATED_BODY()

	// ============================================================================
	// Initialization
	// ============================================================================

	/** Initialize the voxel grid */
	void Initialize(const FIntVector& InResolution, const FVector& InVolumeExtent);

	FORCEINLINE bool IsInitialized() const { return Resolution.X > 0 && Resolution.Y > 0 && Resolution.Z > 0; }

	// ============================================================================
	// Voxel Access
	// ============================================================================

	/** Get density at voxel index (0.0 = hole, 1.0 = full smoke) */
	FORCEINLINE float GetDensity(const FIntVector& Index) const
	{
		if (!IsValidIndex(Index)) return 1.0f;
		return VoxelTextureData[UIVSmokeGridLibrary::GridToIndex(Index, Resolution) * 4 + 0];
	}

	/** Get creation time at voxel index */
	FORCEINLINE float GetCreationTime(const FIntVector& Index) const
	{
		if (!IsValidIndex(Index)) return 0.0f;
		return VoxelTextureData[UIVSmokeGridLibrary::GridToIndex(Index, Resolution) * 4 + 1];
	}

	/** Check if voxel has an active hole */
	FORCEINLINE bool HasHole(const FIntVector& Index) const
	{
		return GetDensity(Index) < 1.0f;
	}

	FORCEINLINE FIntVector GetResolution() const { return Resolution; }

	FORCEINLINE FVector GetVolumeExtent() const { return VolumeExtent; }

	FORCEINLINE FVector GetVoxelSize() const { return VoxelSize; }

	// ============================================================================
	// Coordinate Conversion
	// ============================================================================

	/** Convert local position to voxel index */
	FIntVector LocalToVoxel(const FVector& LocalPosition) const;

	/** Convert voxel index to local position */
	FORCEINLINE FVector VoxelToLocal(const FIntVector& Index) const
	{
		return FVector(
			(static_cast<float>(Index.X) + 0.5f) * VoxelSize.X - VolumeExtent.X * 0.5f,
			(static_cast<float>(Index.Y) + 0.5f) * VoxelSize.Y - VolumeExtent.Y * 0.5f,
			(static_cast<float>(Index.Z) + 0.5f) * VoxelSize.Z - VolumeExtent.Z * 0.5f
		);
	}

	FORCEINLINE bool IsValidIndex(const FIntVector& Index) const
	{
		return Index.X >= 0 && Index.X < Resolution.X &&
			   Index.Y >= 0 && Index.Y < Resolution.Y &&
			   Index.Z >= 0 && Index.Z < Resolution.Z;
	}

	// ============================================================================
	// Hole Operations
	// ============================================================================

	/**
	 * @brief Apply a hole to specified voxels
	 * @param VoxelIndices Array of voxel indices to affect
	 * @param CurrentTime Server-synchronized timestamp
	 */
	void ApplyHole(const TArray<FIntVector>& VoxelIndices, float CurrentTime);

	/**
	 * @brief Apply hole to a single voxel
	 * @param Index Voxel index
	 * @param CurrentTime Server-synchronized timestamp
	 */
	void ApplyHoleToVoxel(const FIntVector& Index, float CurrentTime);

	/**
	 * @brief Remove expired holes (reset Density to 1.0)
	 * @param CurrentTime Current game time
	 * @param HoleLifeTime Lifetime of holes in seconds
	 * @return Number of holes removed
	 */
	int32 RemoveExpiredHoles(float CurrentTime, float HoleLifeTime);

	// ============================================================================
	// Texture Data
	// ============================================================================

	/** Check if texture needs update */
	FORCEINLINE bool IsTextureDirty() const { return bTextureDirty; }

	/** Mark texture as needing update */
	FORCEINLINE void MarkTextureDirty() { bTextureDirty = true; }

	/** Clear dirty flag after texture upload */
	FORCEINLINE void ClearTextureDirty() { bTextureDirty = false; }

	/**
	 * @brief Get texture data for RGBA32F volume texture (GPU-ready)
	 * @return Array of float (R=Density, G=CreationTime, BA=Reserved)
	 * @note 4 elements per voxel (RGBA), no conversion needed
	 */
	FORCEINLINE const TArray<float>& GetTextureData() const { return VoxelTextureData; }

private:
	/** Grid resolution (X x Y x Z) */
	FIntVector Resolution = FIntVector::ZeroValue;

	/** Volume extent in world units */
	FVector VolumeExtent = FVector::ZeroVector;

	/** Size of each voxel in world units */
	FVector VoxelSize = FVector::ZeroVector;

	/**
	 * Unified voxel/texture data in RGBA32F format (4 floats per voxel)
	 * [0] R: Density (0.0 = hole, 1.0 = full smoke)
	 * [1] G: CreationTime (seconds, server-synced)
	 * [2] B: Reserved
	 * [3] A: Reserved
	 */
	TArray<float> VoxelTextureData;

	/** Flag indicating texture data needs update */
	bool bTextureDirty = true;
};
