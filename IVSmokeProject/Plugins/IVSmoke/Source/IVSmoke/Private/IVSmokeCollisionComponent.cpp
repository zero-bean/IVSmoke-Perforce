// Fill out your copyright notice in the Description page of Project Settings.

#include "IVSmokeCollisionComponent.h"

#include "PhysicsEngine/BodySetup.h"

//~==============================================================================
// Component Lifecycle
#pragma region Lifecycle

UIVSmokeCollisionComponent::UIVSmokeCollisionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	SetGenerateOverlapEvents(false);
}

UBodySetup* UIVSmokeCollisionComponent::GetBodySetup()
{
	if (!VoxelBodySetup)
	{
		VoxelBodySetup = NewObject<UBodySetup>(this, NAME_None, RF_Transient);
		VoxelBodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		VoxelBodySetup->bNeverNeedsCookedCollisionData = true;
	}
	return VoxelBodySetup;
}

void UIVSmokeCollisionComponent::OnCreatePhysicsState()
{
	GetBodySetup();

	Super::OnCreatePhysicsState();
}

#pragma endregion

//~==============================================================================
// Collision Management
#pragma region Collision

void UIVSmokeCollisionComponent::UpdateCollision(const TArray<float>& VoxelData, const FIntVector& GridResolution, float VoxelSize)
{
	if (!bCollisionEnabled)
	{
		return;
	}

	Octree.Build(VoxelData, GridResolution, VoxelSize);

	RebuildPhysicsGeometry();
}

void UIVSmokeCollisionComponent::ResetCollision()
{
	Octree.Reset();

	if (VoxelBodySetup)
	{
		VoxelBodySetup->AggGeom.EmptyElements();
		VoxelBodySetup->InvalidatePhysicsData();
		VoxelBodySetup->CreatePhysicsMeshes();
	}

	RecreatePhysicsState();
}

void UIVSmokeCollisionComponent::RebuildPhysicsGeometry()
{
	UBodySetup* BodySetup = GetBodySetup();
	if (!BodySetup)
	{
		return;
	}

	BodySetup->AggGeom.EmptyElements();

	const TArray<FIVSmokeOctreeNode>& NodeArray = Octree.GetNodeArray();
	const TArray<int32>& ActiveLeafIndexArray = Octree.GetActiveLeafIndexArray();

	for (int32 NodeIndex : ActiveLeafIndexArray)
	{
		if (!NodeArray.IsValidIndex(NodeIndex))
		{
			continue;
		}

		const FIVSmokeOctreeNode& Node = NodeArray[NodeIndex];
		if (!Node.bIsOccupied)
		{
			continue;
		}

		FKBoxElem BoxElem;
		BoxElem.Center = Node.Center;

		float Diameter = Node.Extent * 2.0f;
		BoxElem.X = Diameter;
		BoxElem.Y = Diameter;
		BoxElem.Z = Diameter;

		BoxElem.Rotation = FRotator::ZeroRotator;

		BodySetup->AggGeom.BoxElems.Add(BoxElem);
	}

	bool bUseProfile = !SmokeCollisionProfileName.IsNone() &&
						SmokeCollisionProfileName != UCollisionProfile::NoCollision_ProfileName &&
						SmokeCollisionProfileName != UCollisionProfile::CustomCollisionProfileName;

	if (bUseProfile)
	{
		BodyInstance.SetCollisionProfileName(SmokeCollisionProfileName);
	}
	else
	{
		BodyInstance.SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
		BodyInstance.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		BodyInstance.SetObjectType(ECC_WorldDynamic);
		BodyInstance.SetResponseToAllChannels(ECR_Ignore);

		for (const auto& Channel : BlockChannelArray)
		{
			BodyInstance.SetResponseToChannel(Channel, ECR_Block);
		}
	}

	BodySetup->InvalidatePhysicsData();

	BodyInstance.UpdatePhysicsFilterData();
	RecreatePhysicsState();
}

void UIVSmokeCollisionComponent::DrawDebugVisualization() const
{
#if WITH_EDITOR
	if (!bDebugEnabled)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const TArray<FIVSmokeOctreeNode>& NodeArray = Octree.GetNodeArray();
	const TArray<int32>& ActiveLeafIndexArray = Octree.GetActiveLeafIndexArray();

	FTransform ComponentTrans = GetComponentTransform();
	FQuat Rotation = ComponentTrans.GetRotation();
	FColor WireColor = FColor::Green;

	for (int32 NodeIndex : ActiveLeafIndexArray)
	{
		if (!NodeArray.IsValidIndex(NodeIndex))
		{
			continue;
		}

		const FIVSmokeOctreeNode& Node = NodeArray[NodeIndex];

		FVector WorldCenter = ComponentTrans.TransformPosition(Node.Center);
		FVector Extent(Node.Extent);

		DrawDebugBox(
			World,
			WorldCenter,
			Extent,
			Rotation,
			WireColor,
			false, -1.0f, 0, 1.5f
		);
	}
	FVector TextPos = GetComponentLocation() + FVector(0, 0, 50.0f);
	FString DebugMsg = FString::Printf(TEXT("Octree Leaves: %d"), ActiveLeafIndexArray.Num());
	DrawDebugString(World, TextPos, DebugMsg, nullptr, FColor::White, 0.0f, true);
#endif
}

#pragma endregion
