// Fill out your copyright notice in the Description page of Project Settings.

#include "IVSmokeVoxelVolume.h"

#include "IVSmokeGridLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogSmokeDebug, Log, All);

AIVSmokeVoxelVolume::AIVSmokeVoxelVolume()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AIVSmokeVoxelVolume::BeginPlay()
{
	Super::BeginPlay();
}

void AIVSmokeVoxelVolume::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	switch (CurrentState)
	{
	case EIVSmokeVoxelVolumeState::Expansion:
	{
		ElapsedTime += DeltaTime;
		float Alpha = FMath::Clamp(ElapsedTime / ExpansionDuration, 0.0f, 1.0f);

		float CurveValue = ExpansionCurve ? FMath::Clamp(ExpansionCurve->GetFloatValue(Alpha), 0.0f, 1.0f) : Alpha;

		int32 TargetSpawnNum = FMath::FloorToInt(MaxVoxelNum * CurveValue);

		int32 SpawnNum = TargetSpawnNum - ActiveVoxelIndices.Num();
		if (SpawnNum > 0)
		{
			ProcessFloodFill(SpawnNum);
		}

		if (Alpha >= 1.0f || (PriorityQueue.IsEmpty() && SpawnNum <= 0))
		{
			CurrentState = EIVSmokeVoxelVolumeState::Sustain;
			ElapsedTime = 0.0f;
		}
		break;
	}
	case EIVSmokeVoxelVolumeState::Sustain:
	{
		ElapsedTime += DeltaTime;
		if (ElapsedTime >= SustainDuration)
		{
			CurrentState = EIVSmokeVoxelVolumeState::Dissipation;
			ElapsedTime = 0.0f;
		}
		break;
	}
	case EIVSmokeVoxelVolumeState::Dissipation:
	{
		ElapsedTime += DeltaTime;
		// @todo Dissipation logic
		if (ElapsedTime >= DissipationDuration)
		{
			CurrentState = EIVSmokeVoxelVolumeState::Idle;
		}
		break;
	}
	default:
	{
		break;
	}
	}

	DrawDebugVisualization();
}

void AIVSmokeVoxelVolume::StartFloodFill()
{
	GridResolution.X = (VolumeExtent.X * 2) - 1;
	GridResolution.Y = (VolumeExtent.Y * 2) - 1;
	GridResolution.Z = (VolumeExtent.Z * 2) - 1;

	GridResolution.X = FMath::Max(1, GridResolution.X);
	GridResolution.Y = FMath::Max(1, GridResolution.Y);
	GridResolution.Z = FMath::Max(1, GridResolution.Z);

	CenterOffset.X = VolumeExtent.X - 1;
	CenterOffset.Y = VolumeExtent.Y - 1;
	CenterOffset.Z = VolumeExtent.Z - 1;

	int32 TotalGridSize = GridResolution.X * GridResolution.Y * GridResolution.Z;

	PriorityQueue.Empty();

	ActiveVoxelIndices.Empty();
	ActiveVoxelIndices.Reserve(MaxVoxelNum);

	VoxelArray.Empty();
	VoxelArray.Init(0, TotalGridSize);

	VoxelCostArray.Empty();
	VoxelCostArray.Init(FLT_MAX, TotalGridSize);

	int32 CenterIndex = UIVSmokeGridLibrary::GridToIndex(CenterOffset, GridResolution);
	if (VoxelCostArray.IsValidIndex(CenterIndex))
	{
		VoxelCostArray[CenterIndex] = 0.0f;
		PriorityQueue.HeapPush({ CenterIndex, 0.0f });
	}

	CurrentState = EIVSmokeVoxelVolumeState::Expansion;
	ElapsedTime = 0.0f;
}

void AIVSmokeVoxelVolume::ProcessFloodFill(int32 SpawnNum)
{
	static const FIntVector Directions[] = {
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
		FIntVector(0, 1, 0), FIntVector(0, -1, 0),
		FIntVector(0, 0, 1), FIntVector(0, 0, -1)
	};

	FTransform ActorTransform = GetActorTransform();
	int32 SpawnCount = 0;
	while (SpawnCount < SpawnNum && !PriorityQueue.IsEmpty())
	{
		FVoxelNode CurrentNode;
		PriorityQueue.HeapPop(CurrentNode);

		if (CurrentNode.Cost > VoxelCostArray[CurrentNode.Index])
		{
			continue;
		}

		if (VoxelArray[CurrentNode.Index] == 0)
		{
			ActiveVoxelIndices.Add(CurrentNode.Index);
			VoxelArray[CurrentNode.Index] = 1;
			++SpawnCount;

			if (ActiveVoxelIndices.Num() >= MaxVoxelNum)
			{
				PriorityQueue.Empty();
				return;
			}
		}

		FIntVector CurrentGrid = UIVSmokeGridLibrary::IndexToGrid(CurrentNode.Index, GridResolution);

		for (int32 i = 0; i < 6; ++i)
		{
			FIntVector NextGrid = CurrentGrid + Directions[i];
			if (NextGrid.X < 0 || NextGrid.X >= GridResolution.X ||
				NextGrid.Y < 0 || NextGrid.Y >= GridResolution.Y ||
				NextGrid.Z < 0 || NextGrid.Z >= GridResolution.Z)
			{
				continue;
			}

			float StepCost = CostBase;
			if (Directions[i].Z == 1)
			{
				StepCost *= CostUpModifier;
			}
			else if (Directions[i].Z == -1)
			{
				StepCost *= CostDownModifier;
			}

			float DistX = NextGrid.X - CenterOffset.X;
			float DistY = NextGrid.Y - CenterOffset.Y;
			float DistZ = NextGrid.Z - CenterOffset.Z;
			float DistFromCenter = FMath::Sqrt(DistX * DistX + DistY * DistY + DistZ * DistZ);

			StepCost += (DistFromCenter * CostDistanceModifier);

			float NewCost = CurrentNode.Cost + StepCost;

			int32 NextIndex = UIVSmokeGridLibrary::GridToIndex(NextGrid, GridResolution);
			if (NewCost < VoxelCostArray[NextIndex])
			{
				if (VoxelCostArray[NextIndex] == FLT_MAX)
				{
					FVector LocalPos = UIVSmokeGridLibrary::GridToLocal(NextGrid, VoxelSize, CenterOffset);
					FVector WorldPos = ActorTransform.TransformPosition(LocalPos);
					if (IsVoxelBlocked(WorldPos))
					{
						continue;
					}
				}

				VoxelCostArray[NextIndex] = NewCost;
				PriorityQueue.HeapPush({ NextIndex, NewCost });
			}
		}
	}
}

bool AIVSmokeVoxelVolume::IsVoxelBlocked(const FVector& WorldPos) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FCollisionQueryParams CollisionParams;
	CollisionParams.bTraceComplex = false;

	const float VoxelExtent = VoxelSize * 0.5f * CollisionExtentScale;
	const FCollisionShape CollisionShape = FCollisionShape::MakeBox(FVector(VoxelExtent));

	return World->OverlapBlockingTestByChannel(
		WorldPos,
		FQuat::Identity,
		VoxelCollisionChannel,
		CollisionShape,
		CollisionParams
	);
}

void AIVSmokeVoxelVolume::DrawDebugVisualization()
{
	if (!bDebugEnabled) return;

	UWorld* World = GetWorld();
	if (!World) return;

	FTransform ActorTrans = GetActorTransform();
	FQuat ActorRot = ActorTrans.GetRotation();
	FVector ActorLoc = GetActorLocation();

	if (bShowVolume)
	{
		FVector TotalSize;
		TotalSize.X = GridResolution.X * VoxelSize;
		TotalSize.Y = GridResolution.Y * VoxelSize;
		TotalSize.Z = GridResolution.Z * VoxelSize;

		DrawDebugBox(
			World,
			ActorLoc,
			TotalSize * 0.5f,
			ActorRot,
			FColor::Green,
			false,
			-1.0f,
			0,
			1.0f
		);
	}

	if (bShowVoxel && !ActiveVoxelIndices.IsEmpty())
	{
		FVector VoxelDrawExtent(VoxelSize * 0.45f);

		for (int32 VoxelID : ActiveVoxelIndices)
		{
			FIntVector GridPos = UIVSmokeGridLibrary::IndexToGrid(VoxelID, GridResolution);
			FVector LocalPos = UIVSmokeGridLibrary::GridToLocal(GridPos, VoxelSize, CenterOffset);
			FVector WorldPos = ActorTrans.TransformPosition(LocalPos);

			DrawDebugBox(
				World,
				WorldPos,
				VoxelDrawExtent,
				ActorRot,
				FColor::Red,
				false,
				-1.0f,
				0,
				0.0f
			);
		}
	}
}

