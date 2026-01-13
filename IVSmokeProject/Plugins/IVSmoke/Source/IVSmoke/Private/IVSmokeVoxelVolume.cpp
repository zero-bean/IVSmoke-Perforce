// Fill out your copyright notice in the Description page of Project Settings.

#include "IVSmokeVoxelVolume.h"

#include "IVSmoke.h"
#include "IVSmokeGridLibrary.h"
#include "IVSmokeRenderer.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "IVSmokeHoleGeneratorComponent.h"
#include "Net/UnrealNetwork.h"

static const FIntVector FloodFillDirections[] = {
	FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
	FIntVector(0, 1, 0), FIntVector(0, -1, 0),
	FIntVector(0, 0, 1), FIntVector(0, 0, -1)
};

//~==============================================================================
// Actor Lifecycle
#pragma region Lifecycle
AIVSmokeVoxelVolume::AIVSmokeVoxelVolume()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

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
	Initialize();

	Super::BeginPlay();

	FIVSmokeRenderer::Get().AddVolume(this);
	HoleGeneratorComponent = FindComponentByClass<UIVSmokeHoleGeneratorComponent>();
	if (HoleGeneratorComponent)
	{
		HoleGeneratorComponent->SyncWithVoxelVolume(VolumeExtent, VoxelSize);
	}
}

void AIVSmokeVoxelVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FIVSmokeRenderer::Get().RemoveVolume(this);

	Super::EndPlay(EndPlayReason);
}

void AIVSmokeVoxelVolume::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

void AIVSmokeVoxelVolume::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	switch (CurrentState)
	{
	case EIVSmokeVoxelVolumeState::Expansion:
		UpdateExpansion(DeltaTime);
		break;
	case EIVSmokeVoxelVolumeState::Sustain:
		UpdateSustain(DeltaTime);
		break;
	case EIVSmokeVoxelVolumeState::Dissipation:
		UpdateDissipation(DeltaTime);
		break;
	case EIVSmokeVoxelVolumeState::Finished:
		[[fallthrough]];
	case EIVSmokeVoxelVolumeState::Idle:
		[[fallthrough]];
	default:
		break;
	}

#if WITH_EDITOR
	if (DebugSettings.bDebugEnabled)
	{
		DrawDebugVisualization();
	}
#endif
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
	bool bShouldResetSimulation =
			PropertyName == GET_MEMBER_NAME_CHECKED(AIVSmokeVoxelVolume, VolumeExtent)		||
			PropertyName == GET_MEMBER_NAME_CHECKED(AIVSmokeVoxelVolume, Radii)				||
			PropertyName == GET_MEMBER_NAME_CHECKED(AIVSmokeVoxelVolume, VoxelSize)			||
			PropertyName == GET_MEMBER_NAME_CHECKED(AIVSmokeVoxelVolume, MaxVoxelNum)		||
			PropertyName == GET_MEMBER_NAME_CHECKED(AIVSmokeVoxelVolume, ExpansionCurve)	||
			PropertyName == GET_MEMBER_NAME_CHECKED(AIVSmokeVoxelVolume, DissipationCurve)	||
			PropertyName == GET_MEMBER_NAME_CHECKED(AIVSmokeVoxelVolume, ExpansionNoise)	||
			PropertyName == GET_MEMBER_NAME_CHECKED(AIVSmokeVoxelVolume, DissipationNoise);

	if (bShouldResetSimulation && DebugSettings.bDebugEnabled)
	{
		PreviewSimulation();
	}
}

void AIVSmokeVoxelVolume::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	if (bFinished && DebugSettings.bDebugEnabled)
	{
		PreviewSimulation();
	}
}
#endif

#pragma endregion

//~==============================================================================
// Flood Fill Simulation
#pragma region Simulation

bool AIVSmokeVoxelVolume::IsVoxelBlocked(const UWorld* World, const FVector& WorldPos) const
{
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

void AIVSmokeVoxelVolume::Initialize()
{
	GridResolution.X = FMath::Max(1, (VolumeExtent.X * 2) - 1);
	GridResolution.Y = FMath::Max(1, (VolumeExtent.Y * 2) - 1);
	GridResolution.Z = FMath::Max(1, (VolumeExtent.Z * 2) - 1);

	CenterOffset = VolumeExtent - FIntVector(1, 1, 1);

	int32 TotalGridSize = GridResolution.X * GridResolution.Y * GridResolution.Z;

	if (VoxelArray.Num() != TotalGridSize)
	{
		VoxelArray.Init(0.0f, TotalGridSize);
	}
	else
	{
		FMemory::Memzero(VoxelArray.GetData(), VoxelArray.Num() * sizeof(float));
	}

	VoxelCostArray.Empty(TotalGridSize);
	VoxelCostArray.Init(FLT_MAX, TotalGridSize);

	GeneratedVoxelIndices.Reset();
	MinHeap.Empty();

	ActiveVoxelCount = 0;
	DirtyLevel = EIVSmokeDirtyLevel::Dirty;
	CurrentState = EIVSmokeVoxelVolumeState::Idle;
	bIsInitialized = true;
}

void AIVSmokeVoxelVolume::RequestStartSimulation_Implementation()
{
	const int32 NewSeed = FMath::Rand();
	StartSimulation(NewSeed);
}

void AIVSmokeVoxelVolume::StartSimulation_Implementation(int32 InRandomSeed)
{
	if (!bIsInitialized)
	{
		Initialize();
	}

	RandomStream.Initialize(InRandomSeed);

	int32 CenterIndex = UIVSmokeGridLibrary::GridToIndex(CenterOffset, GridResolution);
	if (VoxelCostArray.IsValidIndex(CenterIndex))
	{
		VoxelCostArray[CenterIndex] = 0.0f;
		MinHeap.HeapPush({ CenterIndex, 0.0f });
	}

	CurrentState = EIVSmokeVoxelVolumeState::Expansion;
	ElapsedTime = 0.0f;
	bIsInitialized = false;
}

void AIVSmokeVoxelVolume::UpdateExpansion(float DeltaTime)
{
	ElapsedTime += DeltaTime;

	float CurveValue = GetCurveValue(ElapsedTime, ExpansionDuration, ExpansionCurve);

	int32 TargetSpawnNum = FMath::FloorToInt(MaxVoxelNum * CurveValue);
	int32 SpawnNum = TargetSpawnNum - GeneratedVoxelIndices.Num();
	if (SpawnNum > 0)
	{
		ProcessExpansion(SpawnNum);
	}

	bool bIsTimeOver = ElapsedTime >= ExpansionDuration;
	bool bIsFillFinished = MinHeap.IsEmpty() && SpawnNum <= 0;
	if (bIsTimeOver || bIsFillFinished)
	{
		CurrentState = EIVSmokeVoxelVolumeState::Sustain;
		ElapsedTime = 0.0f;
		MinHeap.Empty();
	}
}

void AIVSmokeVoxelVolume::UpdateSustain(float DeltaTime)
{
	ElapsedTime += DeltaTime;

	float CurveValue = GetCurveValue(ElapsedTime, SustainDuration, nullptr);

	bool bIsTimeOver = ElapsedTime >= SustainDuration;

	int32 TotalVoxelNum = GeneratedVoxelIndices.Num();
	int32 TargetVoxelNum = bIsTimeOver ? TotalVoxelNum : FMath::FloorToInt(TotalVoxelNum * CurveValue);
	int32 VoxelNum = TargetVoxelNum - MinHeap.Num();
	if (VoxelNum > 0)
	{
		PrepareDissipation(VoxelNum);
	}

	if (bIsTimeOver)
	{
		CurrentState = EIVSmokeVoxelVolumeState::Dissipation;
		ElapsedTime = 0.0f;
	}
}

void AIVSmokeVoxelVolume::UpdateDissipation(float DeltaTime)
{
	ElapsedTime += DeltaTime;

	float CurveValue = GetCurveValue(ElapsedTime, DissipationDuration, DissipationCurve);

	int32 TotalVoxelNum = GeneratedVoxelIndices.Num();
	int32 TargetVoxelNum = FMath::FloorToInt(TotalVoxelNum * (1.0f - CurveValue));
	int32 VoxelNum = FMath::Max(ActiveVoxelCount - TargetVoxelNum, 0);
	if (VoxelNum > 0)
	{
		checkf(VoxelNum <= MinHeap.Num(), TEXT("[AIVSmokeVoxelVolume::UpdateDissipation] : MinHeap not enough elements to dissipate - Heap: %d, Request: %d"), MinHeap.Num(), VoxelNum);
		ProcessDissipation(VoxelNum);
	}

	bool bIsTimeOver = ElapsedTime >= DissipationDuration;
	if (bIsTimeOver || (MinHeap.IsEmpty() && ActiveVoxelCount <= 0))
	{
		CurrentState = EIVSmokeVoxelVolumeState::Finished;

		GeneratedVoxelIndices.Empty();
		MinHeap.Empty();
		FMemory::Memzero(VoxelArray.GetData(), VoxelArray.Num() * sizeof(float));
		ActiveVoxelCount = 0;
		DirtyLevel = EIVSmokeDirtyLevel::Dirty;
	}
}

void AIVSmokeVoxelVolume::ProcessExpansion(int32 VoxelNum)
{
	UWorld* World = GetWorld();

	FTransform ActorTrans = GetActorTransform();

	int32 SpawnCount = 0;

	FVector InvRadii;
	InvRadii.X = 1.0f / FMath::Max(UE_KINDA_SMALL_NUMBER, Radii.X);
	InvRadii.Y = 1.0f / FMath::Max(UE_KINDA_SMALL_NUMBER, Radii.Y);
	InvRadii.Z = 1.0f / FMath::Max(UE_KINDA_SMALL_NUMBER, Radii.Z);

	while (SpawnCount < VoxelNum && !MinHeap.IsEmpty())
	{
		FIVSmokeVoxelNode CurrentNode;
		MinHeap.HeapPop(CurrentNode);

		if (CurrentNode.Cost > VoxelCostArray[CurrentNode.Index])
		{
			continue;
		}

		if (VoxelArray[CurrentNode.Index] > 0.0f)
		{
			continue;
		}

		GeneratedVoxelIndices.Add(CurrentNode.Index);
		SetVoxelDensityByIndex(CurrentNode.Index, 1.0f);
		++SpawnCount;

		if (GeneratedVoxelIndices.Num() >= MaxVoxelNum)
		{
			return;
		}

		FIntVector CurrentGrid = UIVSmokeGridLibrary::IndexToGrid(CurrentNode.Index, GridResolution);
		for (const FIntVector& Direction : FloodFillDirections)
		{
			FIntVector NextGrid = CurrentGrid + Direction;
			if (NextGrid.X < 0 || NextGrid.X >= GridResolution.X ||
				NextGrid.Y < 0 || NextGrid.Y >= GridResolution.Y ||
				NextGrid.Z < 0 || NextGrid.Z >= GridResolution.Z)
			{
				continue;
			}

			int32 NextIndex = UIVSmokeGridLibrary::GridToIndex(NextGrid, GridResolution);
			if (VoxelCostArray[NextIndex] != FLT_MAX)
			{
				continue;
			}

			FVector NextLocalPos = UIVSmokeGridLibrary::GridToLocal(NextGrid, VoxelSize, CenterOffset);
			FVector NextWorldPos = ActorTrans.TransformPosition(NextLocalPos);
			if (IsVoxelBlocked(World, NextWorldPos))
			{
				continue;
			}

			float NormX = NextLocalPos.X * InvRadii.X;
			float NormY = NextLocalPos.Y * InvRadii.Y;
			float NormZ = NextLocalPos.Z * InvRadii.Z;

			float DistCost = FMath::Sqrt((NormX * NormX) + (NormY * NormY) + (NormZ * NormZ));
			float NoiseCost = RandomStream.FRandRange(0.0f, ExpansionNoise);

			float NewCost = DistCost + NoiseCost;
			if (NewCost < VoxelCostArray[NextIndex])
			{
				VoxelCostArray[NextIndex] = NewCost;
				MinHeap.HeapPush({ NextIndex, NewCost });
			}
		}
	}
}

void AIVSmokeVoxelVolume::PrepareDissipation(int32 VoxelNum)
{
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
			float DissipationPriority = VoxelCostArray[VoxelIndex] + RandomStream.FRandRange(0.0f, DissipationNoise);
			MinHeap.HeapPush({ VoxelIndex, -DissipationPriority });
		}
	}
}

void AIVSmokeVoxelVolume::ProcessDissipation(int32 VoxelNum)
{
	int32 RemoveCount = 0;
	while (RemoveCount < VoxelNum && !MinHeap.IsEmpty())
	{
		FIVSmokeVoxelNode CurrentNode;
		MinHeap.HeapPop(CurrentNode);

		if (VoxelArray.IsValidIndex(CurrentNode.Index))
		{
			SetVoxelDensityByIndex(CurrentNode.Index, 0.0f);
		}
		++RemoveCount;
	}
}

#pragma endregion

//~==============================================================================
// Data Access
#pragma region DataAccess
FTextureRHIRef AIVSmokeVoxelVolume::GetHoleTexture() const
{
	if (HoleGeneratorComponent)
	{
		return HoleGeneratorComponent->GetHoleTexture();
	}
	return nullptr;
}

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

	VoxelArray[LinearIndex] = Density;

	if (bIsActive && !bWasActive)
	{
		++ActiveVoxelCount;
	}
	else if (!bIsActive && bWasActive)
	{
		--ActiveVoxelCount;
	}

	DirtyLevel = EIVSmokeDirtyLevel::Dirty;
}

#pragma endregion

//~==============================================================================
// Debug
#pragma region Debug

void AIVSmokeVoxelVolume::PreviewSimulation()
{
	bIsEditorPreviewing = true;
	ResetSimulation();
	StartSimulation(RandomSeed);
}

void AIVSmokeVoxelVolume::ResetSimulation()
{
	Initialize();

	FlushPersistentDebugLines(GetWorld());

#if WITH_EDITOR
	if (DebugMeshComponent)
	{
		DebugMeshComponent->ClearInstances();
	}
#endif
}

void AIVSmokeVoxelVolume::DrawDebugVisualization() const
{
#if WITH_EDITOR
	if (!DebugSettings.bDebugEnabled)
	{
		return;
	}

	DrawDebugBounds();
	DrawDebugVoxelWireframes();
	DrawDebugVoxelMeshes();
	DrawDebugStatusText();
#endif
}

void AIVSmokeVoxelVolume::DrawDebugBounds() const
{
#if WITH_EDITOR
	if (!DebugSettings.bShowVolumeBounds)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector TotalSize(
		GridResolution.X * VoxelSize,
		GridResolution.Y * VoxelSize,
		GridResolution.Z * VoxelSize
	);

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

void AIVSmokeVoxelVolume::DrawDebugVoxelWireframes() const
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
	int32 VoxelNum = GeneratedVoxelIndices.Num();
	int32 MaxVisibleIndex = FMath::Clamp(VoxelNum * DebugSettings.VisibleStepCountPercent / 100.0f, 0, VoxelNum);

	const FVector HalfVoxelSize(VoxelSize * 0.5f);
	for (int32 i = 0; i < MaxVisibleIndex; ++i)
	{
		int32 VoxelIndex = GeneratedVoxelIndices[i];
		if (!VoxelArray.IsValidIndex(VoxelIndex) || VoxelArray[VoxelIndex] <= 0.0f)
		{
			continue;
		}

		FIntVector GridPos = UIVSmokeGridLibrary::IndexToGrid(VoxelIndex, GridResolution);
		float NormHeight = static_cast<float>(GridPos.Z) / static_cast<float>(GridResolution.Z);
		if (NormHeight > DebugSettings.SliceHeight)
		{
			continue;
		}

		FVector LocalPos = UIVSmokeGridLibrary::GridToLocal(GridPos, VoxelSize, CenterOffset);
		FVector WorldPos = ActorTrans.TransformPosition(LocalPos);

		DrawDebugBox(
			World,
			WorldPos,
			HalfVoxelSize,
			ActorTrans.GetRotation(),
			DebugSettings.DebugWireframeColor,
			false, -1.0f, 0, 1.5f
		);
	}
#endif
}

void AIVSmokeVoxelVolume::DrawDebugVoxelMeshes() const
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

	int32 VoxelNum = GeneratedVoxelIndices.Num();
	int32 MaxVisibleIndex = FMath::Clamp(static_cast<int32>(VoxelNum * DebugSettings.VisibleStepCountPercent / 100.0f), 0, VoxelNum);

	TArray<FTransform> InstanceTransforms;
	InstanceTransforms.Reserve(MaxVisibleIndex);

	TArray<float> InstanceCustomData;
	InstanceCustomData.Reserve(MaxVisibleIndex);

	const FVector Scale3D(VoxelSize / 100.0f * 0.98f);

	for (int32 i = 0; i < MaxVisibleIndex; ++i)
	{
		int32 VoxelIndex = GeneratedVoxelIndices[i];

		if (!VoxelArray.IsValidIndex(VoxelIndex) || VoxelArray[VoxelIndex] <= 0.0f)
		{
			continue;
		}

		FIntVector GridPos = UIVSmokeGridLibrary::IndexToGrid(VoxelIndex, GridResolution);

		float NormHeight = static_cast<float>(GridPos.Z) / static_cast<float>(GridResolution.Z);
		if (NormHeight > DebugSettings.SliceHeight)
		{
			continue;
		}

		FVector LocalPos = UIVSmokeGridLibrary::GridToLocal(GridPos, VoxelSize, CenterOffset);

		FTransform InstanceTrans;
		InstanceTrans.SetLocation(LocalPos);
		InstanceTrans.SetRotation(FQuat::Identity);
		InstanceTrans.SetScale3D(Scale3D);

		InstanceTransforms.Add(InstanceTrans);

		float DataValue = 0.0f;
		if (DebugSettings.ViewMode == EIVSmokeDebugViewMode::Heatmap)
		{
			DataValue = (VoxelNum > 1) ? static_cast<float>(i) / static_cast<float>(VoxelNum - 1) : 0.0f;
		}
		InstanceCustomData.Add(DataValue);
	}

	if (InstanceTransforms.Num() > 0)
	{
		DebugMeshComponent->AddInstances(InstanceTransforms, false, false);

		int32 InstanceNum = InstanceTransforms.Num();
		for (int32 i = 0; i < InstanceNum; ++i)
		{
			bool bIsLast = (i == InstanceNum - 1);
			DebugMeshComponent->SetCustomDataValue(i, 0, InstanceCustomData[i], bIsLast);
		}
	}
#endif
}

void AIVSmokeVoxelVolume::DrawDebugStatusText() const
{
#if WITH_EDITOR
	if (!DebugSettings.bDebugEnabled || !DebugSettings.bShowStatusText)
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
	case EIVSmokeVoxelVolumeState::Idle:		StateStr = TEXT("Idle"); break;
	case EIVSmokeVoxelVolumeState::Expansion:	StateStr = TEXT("Expansion"); break;
	case EIVSmokeVoxelVolumeState::Sustain:		StateStr = TEXT("Sustain"); break;
	case EIVSmokeVoxelVolumeState::Dissipation:	StateStr = TEXT("Dissipation"); break;
	case EIVSmokeVoxelVolumeState::Finished:	StateStr = TEXT("Finished"); break;
	default:									StateStr = TEXT("Unknown"); break;
	}

	float Percent = MaxVoxelNum > 0 ? (static_cast<float>(ActiveVoxelCount) / MaxVoxelNum * 100.0f) : 0.0f;

	FString DebugMsg = FString::Printf(
		TEXT("State: %s\nTime: %.2fs\nVoxels: %d / %d (%.1f%%)\nHeap: %d"),
		*StateStr,
		ElapsedTime,
		ActiveVoxelCount,
		MaxVoxelNum,
		Percent,
		MinHeap.Num()
	);

	FVector TextPos = GetActorLocation();
	TextPos.Z += (GridResolution.Z * VoxelSize * 0.5f) + 50.0f;

	DrawDebugString(World, TextPos, DebugMsg, nullptr, FColor::White, 0.0f, true, 1.2f);
#endif
}

#pragma endregion
