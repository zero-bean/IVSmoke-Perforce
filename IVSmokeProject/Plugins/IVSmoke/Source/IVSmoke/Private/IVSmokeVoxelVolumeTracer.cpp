// Copyright SDB. All Rights Reserved.

#include "IVSmokeVoxelVolumeTracer.h"

int32 FIVSmokeVoxelVolumeTracer::Trace(
	const FIntVector& StartVoxel,
	const FIntVector& EndVoxel,
	const FIntVector& Resolution,
	TArray<FIntVector>& OutVoxelIndices)
{
	OutVoxelIndices.Reset();

	// Validate inputs
	if (Resolution.X <= 0 || Resolution.Y <= 0 || Resolution.Z <= 0)
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
	const FIntVector& Resolution,
	int32 MaxDistance)
{
	if (Resolution.X <= 0 || Resolution.Y <= 0 || Resolution.Z <= 0)
	{
		return EntryVoxel;
	}

	// Default max distance
	if (MaxDistance <= 0)
	{
		MaxDistance = FMath::Max3(Resolution.X, Resolution.Y, Resolution.Z) * 2;
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

/**
 * Capped Cone SDF (Signed Distance Field)
 * Based on Inigo Quilez's SDF functions: https://iquilezles.org/articles/distfunctions/
 *
 * @param P Point to test
 * @param A Start point of cone (larger radius)
 * @param B End point of cone (smaller radius)
 * @param RA Radius at point A
 * @param RB Radius at point B
 * @return Signed distance (negative = inside, positive = outside)
 */
static float CappedConeSDF(const FVector& P, const FVector& A, const FVector& B, float RA, float RB)
{
	const float RBA = RB - RA;
	const float BABA = FVector::DotProduct(B - A, B - A);
	const float PAPA = FVector::DotProduct(P - A, P - A);
	const float PABA = FVector::DotProduct(P - A, B - A) / BABA;

	const float X = FMath::Sqrt(FMath::Max(0.0f, PAPA - PABA * PABA * BABA));
	const float CAX = FMath::Max(0.0f, X - ((PABA < 0.5f) ? RA : RB));
	const float CAY = FMath::Abs(PABA - 0.5f) - 0.5f;

	const float K = RBA * RBA + BABA;
	const float F = FMath::Clamp((RBA * (X - RA) + PABA * BABA) / K, 0.0f, 1.0f);

	const float CBX = X - RA - F * RBA;
	const float CBY = PABA - F;

	const float S = (CBX < 0.0f && CAY < 0.0f) ? -1.0f : 1.0f;

	return S * FMath::Sqrt(FMath::Min(CAX * CAX + CAY * CAY * BABA, CBX * CBX + CBY * CBY * BABA));
}

int32 FIVSmokeVoxelVolumeTracer::CollectVoxelsInCone(
	const FVector& StartLocalPos,
	const FVector& EndLocalPos,
	float StartRadius,
	float EndRadius,
	const FVector& VoxelSize,
	const FIntVector& Resolution,
	TArray<FIntVector>& OutVoxelIndices)
{
	OutVoxelIndices.Reset();

	// Validate inputs
	if (Resolution.X <= 0 || Resolution.Y <= 0 || Resolution.Z <= 0)
	{
		return 0;
	}

	if (VoxelSize.X <= 0.0f || VoxelSize.Y <= 0.0f || VoxelSize.Z <= 0.0f)
	{
		return 0;
	}

	const FVector HalfExtent = FVector(Resolution) * VoxelSize * 0.5f;

	// Lambda: Convert voxel index to local position (voxel center)
	auto VoxelToLocal = [&](const FIntVector& Index) -> FVector
	{
		return FVector(
			(static_cast<float>(Index.X) + 0.5f) * VoxelSize.X - HalfExtent.X,
			(static_cast<float>(Index.Y) + 0.5f) * VoxelSize.Y - HalfExtent.Y,
			(static_cast<float>(Index.Z) + 0.5f) * VoxelSize.Z - HalfExtent.Z
		);
	};

	// Lambda: Convert local position to voxel float coordinates
	auto LocalToVoxelFloat = [&](const FVector& LocalPos) -> FVector
	{
		return FVector(
			(LocalPos.X + HalfExtent.X) / VoxelSize.X,
			(LocalPos.Y + HalfExtent.Y) / VoxelSize.Y,
			(LocalPos.Z + HalfExtent.Z) / VoxelSize.Z
		);
	};

	// Calculate bounding box in local space (with max radius padding)
	const float MaxRadius = FMath::Max(StartRadius, EndRadius);

	const FVector MinBound(
		FMath::Min(StartLocalPos.X, EndLocalPos.X) - MaxRadius,
		FMath::Min(StartLocalPos.Y, EndLocalPos.Y) - MaxRadius,
		FMath::Min(StartLocalPos.Z, EndLocalPos.Z) - MaxRadius
	);

	const FVector MaxBound(
		FMath::Max(StartLocalPos.X, EndLocalPos.X) + MaxRadius,
		FMath::Max(StartLocalPos.Y, EndLocalPos.Y) + MaxRadius,
		FMath::Max(StartLocalPos.Z, EndLocalPos.Z) + MaxRadius
	);

	// Convert bounds to voxel indices
	const FVector MinVoxelF = LocalToVoxelFloat(MinBound);
	const FVector MaxVoxelF = LocalToVoxelFloat(MaxBound);

	const int32 MinX = FMath::Clamp(FMath::FloorToInt(MinVoxelF.X), 0, Resolution.X - 1);
	const int32 MaxX = FMath::Clamp(FMath::FloorToInt(MaxVoxelF.X), 0, Resolution.X - 1);
	const int32 MinY = FMath::Clamp(FMath::FloorToInt(MinVoxelF.Y), 0, Resolution.Y - 1);
	const int32 MaxY = FMath::Clamp(FMath::FloorToInt(MaxVoxelF.Y), 0, Resolution.Y - 1);
	const int32 MinZ = FMath::Clamp(FMath::FloorToInt(MinVoxelF.Z), 0, Resolution.Z - 1);
	const int32 MaxZ = FMath::Clamp(FMath::FloorToInt(MaxVoxelF.Z), 0, Resolution.Z - 1);

	// Reserve approximate space
	const int32 EstimatedCount = (MaxX - MinX + 1) * (MaxY - MinY + 1) * (MaxZ - MinZ + 1);
	OutVoxelIndices.Reserve(FMath::Min(EstimatedCount, 4096));

	// Iterate through bounding box and test each voxel using SDF
	for (int32 Z = MinZ; Z <= MaxZ; ++Z)
	{
		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const FIntVector VoxelIndex(X, Y, Z);
				const FVector VoxelCenter = VoxelToLocal(VoxelIndex);

				// SDF test: negative distance means inside the cone
				const float Distance = CappedConeSDF(VoxelCenter, StartLocalPos, EndLocalPos, StartRadius, EndRadius);

				if (Distance <= 0.0f)
				{
					OutVoxelIndices.Add(VoxelIndex);
				}
			}
		}
	}

	return OutVoxelIndices.Num();
}
