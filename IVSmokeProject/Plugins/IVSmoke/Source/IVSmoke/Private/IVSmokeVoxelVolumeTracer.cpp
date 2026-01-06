// Copyright SDB. All Rights Reserved.

#include "IVSmokeVoxelVolumeTracer.h"

int32 FIVSmokeVoxelVolumeTracer::Trace(
	const FIntVector& StartVoxel,
	const FIntVector& EndVoxel,
	const int32 Resolution,
	TArray<FIntVector>& OutVoxelIndices)
{
	OutVoxelIndices.Reset();

	// Validate inputs
	if (Resolution <= 0)
	{
		return 0;
	}

	// Clamp to valid range
	FIntVector Current = ClampVoxel(StartVoxel, Resolution);
	const FIntVector End = ClampVoxel(EndVoxel, Resolution);

	// Calculate deltas
	const int32 DeltaX = FMath::Abs(End.X - Current.X);
	const int32 DeltaY = FMath::Abs(End.Y - Current.Y);
	const int32 DeltaZ = FMath::Abs(End.Z - Current.Z);

	// Step directions
	const int32 StepX = (End.X > Current.X) ? 1 : -1;
	const int32 StepY = (End.Y > Current.Y) ? 1 : -1;
	const int32 StepZ = (End.Z > Current.Z) ? 1 : -1;

	// Determine dominant axis
	const int32 MaxDelta = FMath::Max3(DeltaX, DeltaY, DeltaZ);

	if (MaxDelta == 0)
	{
		// Start and end are same voxel
		OutVoxelIndices.Add(Current);
		return 1;
	}

	// Reserve approximate space
	OutVoxelIndices.Reserve(MaxDelta + 1);

	// 3D Bresenham algorithm
	// Using the dominant axis approach for proper 3D line drawing
	if (DeltaX >= DeltaY && DeltaX >= DeltaZ)
	{
		// X is dominant axis
		int32 ErrY = 2 * DeltaY - DeltaX;
		int32 ErrZ = 2 * DeltaZ - DeltaX;

		while (Current.X != End.X)
		{
			OutVoxelIndices.Add(Current);

			if (ErrY > 0)
			{
				Current.Y += StepY;
				ErrY -= 2 * DeltaX;
			}
			if (ErrZ > 0)
			{
				Current.Z += StepZ;
				ErrZ -= 2 * DeltaX;
			}

			ErrY += 2 * DeltaY;
			ErrZ += 2 * DeltaZ;
			Current.X += StepX;
		}
	}
	else if (DeltaY >= DeltaX && DeltaY >= DeltaZ)
	{
		// Y is dominant axis
		int32 ErrX = 2 * DeltaX - DeltaY;
		int32 ErrZ = 2 * DeltaZ - DeltaY;

		while (Current.Y != End.Y)
		{
			OutVoxelIndices.Add(Current);

			if (ErrX > 0)
			{
				Current.X += StepX;
				ErrX -= 2 * DeltaY;
			}
			if (ErrZ > 0)
			{
				Current.Z += StepZ;
				ErrZ -= 2 * DeltaY;
			}

			ErrX += 2 * DeltaX;
			ErrZ += 2 * DeltaZ;
			Current.Y += StepY;
		}
	}
	else
	{
		// Z is dominant axis
		int32 ErrX = 2 * DeltaX - DeltaZ;
		int32 ErrY = 2 * DeltaY - DeltaZ;

		while (Current.Z != End.Z)
		{
			OutVoxelIndices.Add(Current);

			if (ErrX > 0)
			{
				Current.X += StepX;
				ErrX -= 2 * DeltaZ;
			}
			if (ErrY > 0)
			{
				Current.Y += StepY;
				ErrY -= 2 * DeltaZ;
			}

			ErrX += 2 * DeltaX;
			ErrY += 2 * DeltaY;
			Current.Z += StepZ;
		}
	}

	// Add final voxel
	OutVoxelIndices.Add(End);

	return OutVoxelIndices.Num();
}

FIntVector FIVSmokeVoxelVolumeTracer::CalculateExitVoxel(
	const FIntVector& EntryVoxel,
	const FVector& Direction,
	const int32 Resolution,
	int32 MaxDistance)
{
	if (Resolution <= 0)
	{
		return EntryVoxel;
	}

	// Default max distance
	if (MaxDistance <= 0)
	{
		MaxDistance = Resolution * 2;
	}

	const FVector NormDir = Direction.GetSafeNormal();
	if (NormDir.IsNearlyZero())
	{
		return EntryVoxel;
	}

	FIntVector ExitVoxel = EntryVoxel;

	for (int32 Step = 1; Step <= MaxDistance; ++Step)
	{
		const FIntVector NextVoxel(
			EntryVoxel.X + FMath::RoundToInt(NormDir.X * Step),
			EntryVoxel.Y + FMath::RoundToInt(NormDir.Y * Step),
			EntryVoxel.Z + FMath::RoundToInt(NormDir.Z * Step));

		if (!IsValidVoxel(NextVoxel, Resolution))
		{
			break;
		}

		ExitVoxel = NextVoxel;
	}

	return ExitVoxel;
}
