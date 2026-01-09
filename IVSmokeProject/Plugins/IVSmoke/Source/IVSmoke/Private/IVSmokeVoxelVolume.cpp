// Fill out your copyright notice in the Description page of Project Settings.

#include "IVSmokeVoxelVolume.h"

#include "IVSmokeGridLibrary.h"
#include "IVSmokeRenderer.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "IVSmokeHoleGeneratorComponent.h"

AIVSmokeVoxelVolume::AIVSmokeVoxelVolume()
{
	PrimaryActorTick.bCanEverTick = true;

#if WITH_EDITORONLY_DATA
	DebugMeshComponent = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("DebugMeshComponent"));
	DebugMeshComponent->SetupAttachment(RootComponent);
	DebugMeshComponent->SetCastShadow(false);
	DebugMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DebugMeshComponent->SetGenerateOverlapEvents(false);
	DebugMeshComponent->NumCustomDataFloats = 1;
#endif
}

void AIVSmokeVoxelVolume::BeginPlay()
{
	Super::BeginPlay();
	FIVSmokeRenderer::Get().AddVolume(this);
	HoleGeneratorComponent = FindComponentByClass<UIVSmokeHoleGeneratorComponent>();
	StartFloodFill();
}

void AIVSmokeVoxelVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FIVSmokeRenderer::Get().RemoveVolume(this);
	Super::EndPlay(EndPlayReason);
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

		int32 SpawnNum = TargetSpawnNum - GeneratedVoxelIndices.Num();
		if (SpawnNum > 0)
		{
			ProcessFloodFill(SpawnNum);
		}

		if (Alpha >= 1.0f || (MinHeap.IsEmpty() && SpawnNum <= 0))
		{
			CurrentState = EIVSmokeVoxelVolumeState::Sustain;
			ElapsedTime = 0.0f;
			MinHeap.Empty();
		}
		break;
	}
	case EIVSmokeVoxelVolumeState::Sustain:
	{
		ElapsedTime += DeltaTime;
		float Alpha = FMath::Clamp(ElapsedTime / SustainDuration, 0.0f, 1.0f);

		int32 TotalVoxelNum = GeneratedVoxelIndices.Num();
		int32 TargetVoxelNum = FMath::FloorToInt(TotalVoxelNum * Alpha);

		int32 VoxelNum = TargetVoxelNum - MinHeap.Num();
		if (VoxelNum > 0)
		{
			PrepareDissipation(VoxelNum);
		}

		if (ElapsedTime >= SustainDuration)
		{
			int32 RemainingVoxelNum = TotalVoxelNum - MinHeap.Num();
			if (RemainingVoxelNum)
			{
				PrepareDissipation(RemainingVoxelNum);
			}
			CurrentState = EIVSmokeVoxelVolumeState::Dissipation;
			ElapsedTime = 0.0f;
		}
		break;
	}
	case EIVSmokeVoxelVolumeState::Dissipation:
	{
		ElapsedTime += DeltaTime;

		float Alpha = FMath::Clamp(ElapsedTime / DissipationDuration, 0.0f, 1.0f);
		float CurveValue = DissipationCurve ? FMath::Clamp(DissipationCurve->GetFloatValue(Alpha), 0.0f, 1.0f) : Alpha;

		int32 TotalVoxelCount = GeneratedVoxelIndices.Num();
		int32 TargetRemovedCount = FMath::FloorToInt(TotalVoxelCount * CurveValue);
		int32 CurrentRemovedCount = TotalVoxelCount - MinHeap.Num();

		int32 RemoveNum = TargetRemovedCount - CurrentRemovedCount;
		if (RemoveNum > 0)
		{
			ProcessDissipation(RemoveNum);
		}

		if (Alpha >= 1.0f)
		{
			CurrentState = EIVSmokeVoxelVolumeState::Finished;
			GeneratedVoxelIndices.Empty();
			MinHeap.Empty();
			VoxelArray.Init(0.0f, VoxelArray.Num());
			ActiveVoxelCount = 0;
			DirtyLevel = EIVSmokeDirtyLevel::Dirty;
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

bool AIVSmokeVoxelVolume::ShouldTickIfViewportsOnly() const
{
	if (GetWorld() != nullptr && GetWorld()->WorldType == EWorldType::Editor && DebugSettings.bDebugEnabled)
	{
		return bIsEditorPreviewing;
	}
	return false;
}

#if WITH_EDITOR
void AIVSmokeVoxelVolume::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AIVSmokeVoxelVolume, CostBase)			   ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AIVSmokeVoxelVolume, CostUpModifier)	   ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AIVSmokeVoxelVolume, CostDownModifier)	   ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AIVSmokeVoxelVolume, CostDistanceModifier) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(AIVSmokeVoxelVolume, MaxVoxelNum))
	{
		if (DebugSettings.bDebugEnabled)
		{
			PreviewSimulation();
		}
	}
}
#endif

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

	MinHeap.Empty();

	GeneratedVoxelIndices.Empty();
	GeneratedVoxelIndices.Reserve(MaxVoxelNum);

	VoxelArray.Empty();
	VoxelArray.Init(0.0f, TotalGridSize);

	ActiveVoxelCount = 0;
	DirtyLevel = EIVSmokeDirtyLevel::Dirty;

	VoxelCostArray.Empty();
	VoxelCostArray.Init(FLT_MAX, TotalGridSize);

	int32 CenterIndex = UIVSmokeGridLibrary::GridToIndex(CenterOffset, GridResolution);
	if (VoxelCostArray.IsValidIndex(CenterIndex))
	{
		VoxelCostArray[CenterIndex] = 0.0f;
		MinHeap.HeapPush({ CenterIndex, 0.0f });
	}

	CurrentState = EIVSmokeVoxelVolumeState::Expansion;
	ElapsedTime = 0.0f;
}

void AIVSmokeVoxelVolume::PreviewSimulation()
{
	ResetSimulation();

	bIsEditorPreviewing = true;
	StartFloodFill();
}

void AIVSmokeVoxelVolume::ResetSimulation()
{
	bIsEditorPreviewing = false;
	CurrentState = EIVSmokeVoxelVolumeState::Idle;
	GeneratedVoxelIndices.Empty();
	MinHeap.Empty();

	FlushPersistentDebugLines(GetWorld());
	if (DebugMeshComponent)
	{
		DebugMeshComponent->ClearInstances();
	}
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
	while (SpawnCount < SpawnNum && !MinHeap.IsEmpty())
	{
		FIVSmokeVoxelNode CurrentNode;
		MinHeap.HeapPop(CurrentNode);

		if (CurrentNode.Cost > VoxelCostArray[CurrentNode.Index])
		{
			continue;
		}

		if (VoxelArray[CurrentNode.Index] <= 0.0f)
		{
			GeneratedVoxelIndices.Add(CurrentNode.Index);
			SetVoxelDensityByIndex(CurrentNode.Index, 1.0f);  // Full density (continuous: 0.0~N)
			++SpawnCount;

			if (GeneratedVoxelIndices.Num() >= MaxVoxelNum)
			{
				MinHeap.Empty();
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
				MinHeap.HeapPush({ NextIndex, NewCost });
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

void AIVSmokeVoxelVolume::PrepareDissipation(int32 VoxelNum)
{
	static FRandomStream Random(FMath::Rand());

	if (GeneratedVoxelIndices.IsEmpty())
	{
		return;
	}

	int32 BeginIndex = MinHeap.Num();
	int32 EndIndex = FMath::Min(BeginIndex + VoxelNum, GeneratedVoxelIndices.Num());

	for (int32 i = BeginIndex; i < EndIndex; ++i)
	{
		int32 VoxelIndex = GeneratedVoxelIndices[i];
		if (VoxelCostArray.IsValidIndex(VoxelIndex))
		{
			float Cost = VoxelCostArray[VoxelIndex] + Random.FRandRange(0.0f, CostDissipationNoise);
			MinHeap.HeapPush({ VoxelIndex, -Cost });
		}
	}
}

void AIVSmokeVoxelVolume::ProcessDissipation(int32 RemoveNum)
{
	int32 Count = 0;
	while (Count < RemoveNum && !MinHeap.IsEmpty())
	{
		FIVSmokeVoxelNode NodeToRemove;
		MinHeap.HeapPop(NodeToRemove);

		if (VoxelArray.IsValidIndex(NodeToRemove.Index))
		{
			SetVoxelDensityByIndex(NodeToRemove.Index, 0.0f);
		}

		++Count;
	}
}

// ============================================================================
// Voxel Data Management
// ============================================================================

void AIVSmokeVoxelVolume::SetVoxelDensity(const FIntVector& GridPos, float Density)
{
	int32 LinearIndex = UIVSmokeGridLibrary::GridToIndex(GridPos, GridResolution);
	SetVoxelDensityByIndex(LinearIndex, Density);
}

void AIVSmokeVoxelVolume::SetVoxelDensityByIndex(int32 LinearIndex, float Density)
{
	if (!VoxelArray.IsValidIndex(LinearIndex))
	{
		return;
	}

	const float OldDensity = VoxelArray[LinearIndex];
	const bool bWasActive = OldDensity > 0.0f;
	const bool bIsActive = Density > 0.0f;

	// Update dense array
	VoxelArray[LinearIndex] = Density;

	// Update active voxel count
	if (bIsActive && !bWasActive)
	{
		++ActiveVoxelCount;
	}
	else if (!bIsActive && bWasActive)
	{
		--ActiveVoxelCount;
	}

	// Mark dirty for GPU upload
	DirtyLevel = EIVSmokeDirtyLevel::Dirty;
}

void AIVSmokeVoxelVolume::DrawDebugVisualization()
{
#if WITH_EDITOR
	if (!DebugSettings.bDebugEnabled)
	{
		FlushPersistentDebugLines(GetWorld());
		if (DebugMeshComponent)
		{
			DebugMeshComponent->ClearInstances();
		}
		return;
	}

	FlushPersistentDebugLines(GetWorld());

	DrawDebugBounds();
	DrawDebugVoxelWireframes();
	DrawDebugVoxelMeshes();
	DrawDebugStatusText();
#endif
}

void AIVSmokeVoxelVolume::DrawDebugBounds()
{
#if WITH_EDITOR
	if (!DebugSettings.bShowVolumeBounds)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;

	FVector TotalSize;
	TotalSize.X = GridResolution.X * VoxelSize;
	TotalSize.Y = GridResolution.Y * VoxelSize;
	TotalSize.Z = GridResolution.Z * VoxelSize;

	DrawDebugBox(
		World,
		GetActorLocation(),
		TotalSize * 0.5f,
		GetActorQuat(),
		FColor::Green,
		false, -1.0f, 0, 2.0f
	);
#endif
}

void AIVSmokeVoxelVolume::DrawDebugVoxelWireframes()
{
#if WITH_EDITOR
	if (!DebugSettings.bShowVoxelWireframe || GeneratedVoxelIndices.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTransform ActorTrans = GetActorTransform();

	int32 MaxVisibleIndex = (GeneratedVoxelIndices.Num() * DebugSettings.VisibleStepCountPercent) / 100;
	MaxVisibleIndex = FMath::Clamp(MaxVisibleIndex, 0, GeneratedVoxelIndices.Num());

	for (int32 i = 0; i < MaxVisibleIndex; ++i)
	{
		int32 VoxelIndex = GeneratedVoxelIndices[i];
		if (!VoxelArray.IsValidIndex(VoxelIndex) || VoxelArray[VoxelIndex] <= 0.0f)
		{
			continue;
		}

		FIntVector GridPos = UIVSmokeGridLibrary::IndexToGrid(VoxelIndex, GridResolution);

		float HeightRatio = static_cast<float>(GridPos.Z) / static_cast<float>(GridResolution.Z);
		if (HeightRatio > DebugSettings.SliceHeight)
		{
			continue;
		}

		FVector LocalPos = UIVSmokeGridLibrary::GridToLocal(GridPos, VoxelSize, CenterOffset);
		FVector WorldPos = ActorTrans.TransformPosition(LocalPos);

		DrawDebugBox(
			World,
			WorldPos,
			FVector(VoxelSize * 0.5f),
			ActorTrans.GetRotation(),
			DebugSettings.DebugWireframeColor,
			false, -1.0f, 0, 1.5f
		);
	}
#endif
}

void AIVSmokeVoxelVolume::DrawDebugVoxelMeshes()
{
#if WITH_EDITOR
	if (!DebugMeshComponent)
	{
		return;
	}

	if (!DebugSettings.bShowVoxelMesh || GeneratedVoxelIndices.IsEmpty())
	{
		DebugMeshComponent->ClearInstances();
		return;
	}

	if (DebugVoxelMesh && DebugMeshComponent->GetStaticMesh() != DebugVoxelMesh)
	{
		DebugMeshComponent->SetStaticMesh(DebugVoxelMesh);
	}
	if (DebugVoxelMaterial && DebugMeshComponent->GetMaterial(0) != DebugVoxelMaterial)
	{
		DebugMeshComponent->SetMaterial(0, DebugVoxelMaterial);
	}

	DebugMeshComponent->ClearInstances();

	TArray<FTransform> InstanceTransforms;
	TArray<float> InstanceCustomData;

	int32 NumVoxels = GeneratedVoxelIndices.Num();
	InstanceTransforms.Reserve(NumVoxels);
	InstanceCustomData.Reserve(NumVoxels);

	int32 MaxVisibleIndex = (NumVoxels * DebugSettings.VisibleStepCountPercent) / 100;
	MaxVisibleIndex = FMath::Clamp(MaxVisibleIndex, 0, NumVoxels);

	for (int32 i = 0; i < MaxVisibleIndex; ++i)
	{
		int32 VoxelIndex = GeneratedVoxelIndices[i];
		if (!VoxelArray.IsValidIndex(VoxelIndex) || VoxelArray[VoxelIndex] <= 0.0f)
		{
			continue;
		}

		FIntVector GridPos = UIVSmokeGridLibrary::IndexToGrid(VoxelIndex, GridResolution);

		float HeightRatio = static_cast<float>(GridPos.Z) / static_cast<float>(GridResolution.Z);
		if (HeightRatio > DebugSettings.SliceHeight)
		{
			continue;
		}

		FVector LocalPos = UIVSmokeGridLibrary::GridToLocal(GridPos, VoxelSize, CenterOffset);
		FVector Scale(VoxelSize / 100.0f * 0.98);

		FTransform Trans;
		Trans.SetLocation(LocalPos);
		Trans.SetScale3D(Scale);
		Trans.SetRotation(FQuat::Identity);

		float DataValue = 0.0f;
		if (DebugSettings.ViewMode == EIVSmokeDebugViewMode::Heatmap && VoxelCostArray.IsValidIndex(VoxelIndex))
		{
			DataValue = FMath::GetMappedRangeValueClamped(
				FVector2D(DebugSettings.HeatmapMin, DebugSettings.HeatmapMax),
				FVector2D(0.0f, 1.0f),
				VoxelCostArray[VoxelIndex]
			);
		}

		InstanceTransforms.Add(Trans);
		InstanceCustomData.Add(DataValue);
	}

	if (InstanceTransforms.Num() > 0)
	{
		DebugMeshComponent->AddInstances(InstanceTransforms, false, false);

		for (int32 i = 0; i < InstanceCustomData.Num(); ++i)
		{
			bool bIsLast = (i == InstanceCustomData.Num() - 1);
			DebugMeshComponent->SetCustomDataValue(i, 0, InstanceCustomData[i], bIsLast);
		}
	}
#endif
}

void AIVSmokeVoxelVolume::DrawDebugStatusText()
{
#if WITH_EDITOR
	if (!DebugSettings.bDebugEnabled)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FString StateStr;
	switch (CurrentState)
	{
	case EIVSmokeVoxelVolumeState::Idle:        StateStr = TEXT("Idle"); break;
	case EIVSmokeVoxelVolumeState::Expansion:   StateStr = TEXT("Expansion"); break;
	case EIVSmokeVoxelVolumeState::Sustain:     StateStr = TEXT("Sustain"); break;
	case EIVSmokeVoxelVolumeState::Dissipation: StateStr = TEXT("Dissipation"); break;
	case EIVSmokeVoxelVolumeState::Finished:    StateStr = TEXT("Finished"); break;
	default:                                    StateStr = TEXT("Unknown"); break;
	}

	float Percent = MaxVoxelNum > 0 ? (float)ActiveVoxelCount / (float)MaxVoxelNum * 100.0f : 0.0f;

	FString DebugMsg = FString::Printf(
		TEXT("State: %s\nVoxels: %d / %d (%.1f%%)\nTime: %.2fs"),
		*StateStr,
		ActiveVoxelCount,
		MaxVoxelNum,
		Percent,
		ElapsedTime
	);

	FVector TextLoc = GetActorLocation();
	TextLoc.Z += (VolumeExtent.Z * VoxelSize) + 50.0f;

	DrawDebugString(
		World,
		TextLoc,
		DebugMsg,
		nullptr,
		FColor::White,
		0.0f,
		true,
		1.5f
	);
#endif
}

