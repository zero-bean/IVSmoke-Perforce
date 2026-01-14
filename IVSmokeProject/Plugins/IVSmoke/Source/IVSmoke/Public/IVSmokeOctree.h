// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/** @todo Documentation */
struct FIVSmokeOctreeNode
{
	// @todo Documentation
	FVector Center = FVector::ZeroVector;

	// @todo Documentation
	float Extent = 0.0f;

	// @todo Documentation
	bool bIsLeafNode = false;

	// @todo Documentation
	bool bIsOccupied = false;

	// @todo Documentation
	int32 Children[8];

	FIVSmokeOctreeNode()
	{
		for (int32 i = 0; i < 8; i++)
		{
			Children[i] = INDEX_NONE;
		}
	}
};

/** @todo Documentation */
enum class EIVSmokeNodeState : uint8
{
	// @todo Documentation
	Empty,

	// @todo Documentation
	Full,

	// @todo Documentation
	Mixed
};

/** @todo Documentation */
struct FIVSmokeOctree
{
public:
	/** @todo Documentation */
	void Build(const TArray<float>& VoxelData, const FIntVector& GridResolution, float VoxelSize);

	/** @todo Documentation */
	void Reset();

	/** @todo Documentation */
	FORCEINLINE const TArray<FIVSmokeOctreeNode>& GetNodeArray() const { return NodeArray; }

	/** @todo Documentation */
	FORCEINLINE const TArray<int32>& GetActiveLeafIndexArray() const { return ActiveLeafIndexArray; }

	/** @todo Documentation */
	SIZE_T GetAllocatedSize() const;

private:
	/** @todo Documentation */
	void BuildRecursive(int32 NodeIndex, const FIntVector& GridMin, const FIntVector& GridMax, const TArray<float>& VoxelData, const FIntVector& GridResolution);

	/** @todo Documentation */
	EIVSmokeNodeState CheckNodeState(const FIntVector& GridMin, const FIntVector& GridMax, const TArray<float>& VoxelData, const FIntVector& GridResolution) const;

	// @todo Documentation
	TArray<FIVSmokeOctreeNode> NodeArray;

	// @todo Documentation
	TArray<int32> ActiveLeafIndexArray;

	// @todo Documentation
	int32 RootIndex = INDEX_NONE;
};
