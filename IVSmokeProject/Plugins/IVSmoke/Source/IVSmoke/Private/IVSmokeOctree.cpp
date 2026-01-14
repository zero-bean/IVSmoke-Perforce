// Fill out your copyright notice in the Description page of Project Settings.

#include "IVSmokeOctree.h"

#include "IVSmokeGridLibrary.h"

void FIVSmokeOctree::Build(const TArray<float>& VoxelData, const FIntVector& GridResolution, float VoxelSize)
{
	Reset();

	if (VoxelData.IsEmpty())
	{
		return;
	}

	RootIndex = NodeArray.Add(FIVSmokeOctreeNode());
	FIVSmokeOctreeNode& RootNode = NodeArray[RootIndex];

	int32 MaxGridDim = FMath::Max3(GridResolution.X, GridResolution.Y, GridResolution.Z);

	RootNode.Center = FVector::ZeroVector;
	RootNode.Extent = static_cast<float>(MaxGridDim) * VoxelSize * 0.5f;

	FIntVector MaxGridBound(MaxGridDim - 1, MaxGridDim - 1, MaxGridDim - 1);

	BuildRecursive(RootIndex, FIntVector::ZeroValue, MaxGridBound, VoxelData, GridResolution);
}

void FIVSmokeOctree::Reset()
{
	NodeArray.Empty();
	ActiveLeafIndexArray.Empty();
	RootIndex = INDEX_NONE;
}

SIZE_T FIVSmokeOctree::GetAllocatedSize() const
{
	return NodeArray.GetAllocatedSize() * ActiveLeafIndexArray.GetAllocatedSize();
}

void FIVSmokeOctree::BuildRecursive(int32 NodeIndex, const FIntVector& GridMin, const FIntVector& GridMax, const TArray<float>& VoxelData, const FIntVector& GridResolution)
{
	EIVSmokeNodeState NodeState = CheckNodeState(GridMin, GridMax, VoxelData, GridResolution);

	if (NodeState == EIVSmokeNodeState::Empty)
	{
		NodeArray[NodeIndex].bIsOccupied = false;
		NodeArray[NodeIndex].bIsLeafNode = true;
		return;
	}

	if (NodeState == EIVSmokeNodeState::Full || GridMin == GridMax)
	{
		NodeArray[NodeIndex].bIsOccupied = true;
		NodeArray[NodeIndex].bIsLeafNode = true;
		ActiveLeafIndexArray.Add(NodeIndex);
		return;
	}

	NodeArray[NodeIndex].bIsOccupied = true;
	NodeArray[NodeIndex].bIsLeafNode = false;

	float ChildExtent = NodeArray[NodeIndex].Extent * 0.5f;
	FIntVector GridMid = (GridMin + GridMax) / 2;
	int32 ChildCounter = 0;

	for (int32 Z = 0; Z < 2; ++Z)
	{
		for (int32 Y = 0; Y < 2; ++Y)
		{
			for (int32 X = 0; X < 2; ++X)
			{
				FIntVector ChildMin, ChildMax;

				ChildMin.X = (X == 0) ? GridMin.X : GridMid.X + 1;
				ChildMax.X = (X == 0) ? GridMid.X : GridMax.X;
				ChildMin.Y = (Y == 0) ? GridMin.Y : GridMid.Y + 1;
				ChildMax.Y = (Y == 0) ? GridMid.Y : GridMax.Y;
				ChildMin.Z = (Z == 0) ? GridMin.Z : GridMid.Z + 1;
				ChildMax.Z = (Z == 0) ? GridMid.Z : GridMax.Z;

				if (ChildMin.X > ChildMax.X || ChildMin.Y > ChildMax.Y || ChildMin.Z > ChildMax.Z)
				{
					continue;
				}

				int32 ChildNodeIndex = NodeArray.Add(FIVSmokeOctreeNode());
				NodeArray[NodeIndex].Children[ChildCounter] = ChildNodeIndex;

				FIVSmokeOctreeNode& ChildNode = NodeArray[ChildNodeIndex];
				ChildNode.Extent = ChildExtent;

				FVector Offset((X == 0 ? -1.0f : 1.0f), (Y == 0 ? -1.0f : 1.0f), (Z == 0 ? -1.0f : 1.0f));
				ChildNode.Center = NodeArray[NodeIndex].Center + (Offset * ChildExtent);

				BuildRecursive(ChildNodeIndex, ChildMin, ChildMax, VoxelData, GridResolution);
				++ChildCounter;
			}
		}
	}
}

EIVSmokeNodeState FIVSmokeOctree::CheckNodeState(const FIntVector& GridMin, const FIntVector& GridMax, const TArray<float>& VoxelData, const FIntVector& GridResolution) const
{
	bool bHasEmpty = false;
	bool bHasFull = false;

	int32 BeginX = FMath::Max(GridMin.X, 0);
	int32 EndX = FMath::Min(GridMax.X, GridResolution.X - 1);
	int32 BeginY = FMath::Max(GridMin.Y, 0);
	int32 EndY = FMath::Min(GridMax.Y, GridResolution.Y - 1);
	int32 BeginZ = FMath::Max(GridMin.Z, 0);
	int32 EndZ = FMath::Min(GridMax.Z, GridResolution.Z - 1);

	if (BeginX > EndX || BeginY > EndY || BeginZ > EndZ)
	{
		return EIVSmokeNodeState::Empty;
	}

	for (int32 Z = BeginZ; Z <= EndZ; ++Z)
	{
		for (int32 Y = BeginY; Y <= EndY; ++Y)
		{
			for (int32 X = BeginX; X <= EndX; ++X)
			{
				int32 Index = UIVSmokeGridLibrary::GridToIndex(FIntVector(X, Y, Z), GridResolution);
				if (VoxelData.IsValidIndex(Index) && VoxelData[Index] > 0.0f)
				{
					bHasFull = true;
				}
				else
				{
					bHasEmpty = true;
				}

				if (bHasFull && bHasEmpty)
				{
					return EIVSmokeNodeState::Mixed;
				}
			}
		}
	}

	return bHasFull ? EIVSmokeNodeState::Full : EIVSmokeNodeState::Empty;
}
