// Copyright SDB. All Rights Reserved.

#include "IVSmokeVoxelVolumeCurator.h"

void FIVSmokeVoxelVolumeCurator::Initialize(int32 InResolution, const FVector& InVolumeExtent)
{
	Resolution = FMath::Clamp(InResolution, 16, 256);
	VolumeExtent = InVolumeExtent;
	VoxelSize = VolumeExtent / static_cast<float>(Resolution);

	const int32 TotalVoxels = Resolution * Resolution * Resolution;
	VoxelTextureData.SetNum(TotalVoxels * 4);  // RGBA per voxel

	// Initialize all voxels to full smoke (Density=1, CreationTime=0)
	for (int32 i = 0; i < TotalVoxels; ++i)
	{
		const int32 BaseIndex = i * 4;
		VoxelTextureData[BaseIndex + 0] = 1.0f;  // R: Density (full smoke)
		VoxelTextureData[BaseIndex + 1] = 0.0f;  // G: CreationTime (no hole)
		VoxelTextureData[BaseIndex + 2] = 0.0f;  // B: Reserved
		VoxelTextureData[BaseIndex + 3] = 1.0f;  // A: Reserved
	}

	bTextureDirty = true;
}

FIntVector FIVSmokeVoxelVolumeCurator::LocalToVoxel(const FVector& LocalPosition) const
{
	// LocalPosition is relative to volume center
	// Convert to 0-based voxel coordinates
	const FVector NormalizedPos = (LocalPosition + VolumeExtent * 0.5f) / VolumeExtent;

	return FIntVector(
		FMath::Clamp(static_cast<int32>(NormalizedPos.X * Resolution), 0, Resolution - 1),
		FMath::Clamp(static_cast<int32>(NormalizedPos.Y * Resolution), 0, Resolution - 1),
		FMath::Clamp(static_cast<int32>(NormalizedPos.Z * Resolution), 0, Resolution - 1)
	);
}

void FIVSmokeVoxelVolumeCurator::ApplyHole(const TArray<FIntVector>& VoxelIndices, float CurrentTime)
{
	for (const FIntVector& Index : VoxelIndices)
	{
		ApplyHoleToVoxel(Index, CurrentTime);
	}
}

void FIVSmokeVoxelVolumeCurator::ApplyHoleToVoxel(const FIntVector& Index, float CurrentTime)
{
	if (!IsValidIndex(Index))
	{
		return;
	}

	const int32 BaseIndex = ToArrayIndex(Index) * 4;
	VoxelTextureData[BaseIndex + 0] = 0.0f;        // R: Density (hole)
	VoxelTextureData[BaseIndex + 1] = CurrentTime; // G: CreationTime
	bTextureDirty = true;
}

int32 FIVSmokeVoxelVolumeCurator::RemoveExpiredHoles(float CurrentTime, float HoleLifeTime)
{
	int32 RemovedCount = 0;
	const int32 TotalVoxels = Resolution * Resolution * Resolution;

	for (int32 i = 0; i < TotalVoxels; ++i)
	{
		const int32 BaseIndex = i * 4;
		const float Density = VoxelTextureData[BaseIndex + 0];
		const float CreationTime = VoxelTextureData[BaseIndex + 1];

		// Check if this voxel has a hole and it's expired
		if (Density < 1.0f && CreationTime > 0.0f)
		{
			const float Age = CurrentTime - CreationTime;
			if (Age > HoleLifeTime)
			{
				// Reset to full smoke
				VoxelTextureData[BaseIndex + 0] = 1.0f;  // R: Density (full smoke)
				VoxelTextureData[BaseIndex + 1] = 0.0f;  // G: CreationTime (no hole)
				++RemovedCount;
			}
		}
	}

	if (RemovedCount > 0)
	{
		bTextureDirty = true;
	}

	return RemovedCount;
}
