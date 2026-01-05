// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "IVSmokeGridLibrary.generated.h"

/** @todo Documentation */
UCLASS()
class IVSMOKE_API UIVSmokeGridLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// @todo Documentation
	static const FIntVector InvalidGridPos;

	/** @todo Documentation */
	UFUNCTION(BlueprintPure, Category = "IVSmoke | Math")
	static FORCEINLINE int32 GridToIndex(const FIntVector& GridPos, const FIntVector& Resolution)
	{
		return GridPos.X + (GridPos.Y * Resolution.X) + (GridPos.Z * Resolution.X * Resolution.Y);
	}

	/** @todo Documentation */
	UFUNCTION(BlueprintPure, Category = "IVSmoke | Math")
	static FORCEINLINE FIntVector IndexToGrid(int32 Index, const FIntVector& Resolution)
	{
		if (Resolution.X <= 0 || Resolution.Y <= 0 || Resolution.Z <= 0)
		{
			return FIntVector::ZeroValue;
		}

		const int32 BaseArea = Resolution.X * Resolution.Y;
		const int32 Z = Index / BaseArea;
		const int32 Remainder = Index % BaseArea;
		const int32 Y = Remainder / Resolution.X;
		const int32 X = Remainder % Resolution.X;

		return FIntVector(X, Y, Z);
	}

	/** @todo Documentation */
	UFUNCTION(BlueprintPure, Category = "IVSmoke | Math")
	static FORCEINLINE FVector GridToLocal(const FIntVector& GridPos, float VoxelSize, const FIntVector& CenterOffset)
	{
		return FVector(
			(GridPos.X - CenterOffset.X) * VoxelSize,
			(GridPos.Y - CenterOffset.Y) * VoxelSize,
			(GridPos.Z - CenterOffset.Z) * VoxelSize
		);
	}

	/** @todo Documentation */
	UFUNCTION(BlueprintPure, Category = "IVSmoke | Math")
	static FORCEINLINE FIntVector LocalToGrid(
		const FVector& LocalPos,
		float VoxelSize,
		const FIntVector& CenterOffset,
		const FIntVector& Resolution
	)
	{
		if (VoxelSize <= UE_SMALL_NUMBER)
		{
			return InvalidGridPos;
		}

		const int32 X = FMath::RoundToInt(LocalPos.X / VoxelSize) + CenterOffset.X;
		const int32 Y = FMath::RoundToInt(LocalPos.Y / VoxelSize) + CenterOffset.Y;
		const int32 Z = FMath::RoundToInt(LocalPos.Z / VoxelSize) + CenterOffset.Z;

		if (X >= 0 && X < Resolution.X &&
			Y >= 0 && Y < Resolution.Y &&
			Z >= 0 && Z < Resolution.Z)
		{
			return FIntVector(X, Y, Z);
		}

		return InvalidGridPos;
	}
};
