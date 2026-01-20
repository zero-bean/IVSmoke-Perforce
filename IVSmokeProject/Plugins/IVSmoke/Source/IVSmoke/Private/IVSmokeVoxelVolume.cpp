// Fill out your copyright notice in the Description page of Project Settings.

#include "IVSmokeVoxelVolume.h"

#include "IVSmoke.h"
#include "IVSmokeCollisionComponent.h"
#include "IVSmokeGridLibrary.h"
#include "IVSmokeRenderer.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "IVSmokeHoleGeneratorComponent.h"

#if ENABLE_VISUAL_LOG
#include "VisualLogger/VisualLogger.h"
#endif

DECLARE_CYCLE_STAT(TEXT("Update Expansion"),	STAT_IVSmoke_UpdateExpansion,		STATGROUP_IVSmoke);
DECLARE_CYCLE_STAT(TEXT("Update Sustain"),		STAT_IVSmoke_UpdateSustain,			STATGROUP_IVSmoke);
DECLARE_CYCLE_STAT(TEXT("Update Dissipation"),	STAT_IVSmoke_UpdateDissipation,		STATGROUP_IVSmoke);
DECLARE_CYCLE_STAT(TEXT("Process Expansion"),	STAT_IVSmoke_ProcessExpansion,		STATGROUP_IVSmoke);
DECLARE_CYCLE_STAT(TEXT("Prepare Dissipation"),	STAT_IVSmoke_PrepareDissipation,	STATGROUP_IVSmoke);
DECLARE_CYCLE_STAT(TEXT("Process Dissipation"),	STAT_IVSmoke_ProcessDissipation,	STATGROUP_IVSmoke);

DECLARE_DWORD_COUNTER_STAT(TEXT("Active Voxel Count"),					STAT_IVSmoke_ActiveVoxelCount,	STATGROUP_IVSmoke);
DECLARE_DWORD_COUNTER_STAT(TEXT("Created Voxel Count (Per Frame)"),		STAT_IVSmoke_CreatedVoxel,		STATGROUP_IVSmoke);
DECLARE_DWORD_COUNTER_STAT(TEXT("Destroyed Voxel Count (Per Frame)"),	STAT_IVSmoke_DestroyedVoxel,	STATGROUP_IVSmoke);


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

#if !UE_SERVER
	FIVSmokeRenderer::Get().AddVolume(this);
#endif

	HoleGeneratorComponent = FindComponentByClass<UIVSmokeHoleGeneratorComponent>();

	CollisionComponent = FindComponentByClass<UIVSmokeCollisionComponent>();
}

void AIVSmokeVoxelVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if !UE_SERVER
	FIVSmokeRenderer::Get().RemoveVolume(this);
#endif

	Super::EndPlay(EndPlayReason);
}

void AIVSmokeVoxelVolume::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

void AIVSmokeVoxelVolume::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (ActiveVoxelCount > 0)
	{
		INC_DWORD_STAT_BY(STAT_IVSmoke_ActiveVoxelCount, ActiveVoxelCount);
	}

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

#if ENABLE_VISUAL_LOG
	UpdateVisualLogger();
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

bool AIVSmokeVoxelVolume::IsConnectionBlocked(const UWorld* World, const FVector& BeginPos, const FVector& EndPos) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT("IVSmoke::AIVSmokeVoxelVolume::IsConnectionBlocked");

	if (!World)
	{
		return false;
	}

	FCollisionQueryParams CollisionParams;
	CollisionParams.bTraceComplex = false;

	FHitResult HitResult;
	return World->LineTraceSingleByChannel(
		HitResult,
		BeginPos,
		EndPos,
		VoxelCollisionChannel,
		CollisionParams
	);
}

void AIVSmokeVoxelVolume::Initialize()
{
	VoxelWorldAABBMin = GetActorLocation();
	VoxelWorldAABBMax = GetActorLocation();

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

	int32 TotalGridSizeYZ = GridResolution.Y * GridResolution.Z;

	if (VoxelBitArray.Num() != TotalGridSizeYZ)
	{
		VoxelBitArray.Init(0ULL, TotalGridSizeYZ);
	}
	else
	{
		FMemory::Memzero(VoxelBitArray.GetData(), TotalGridSizeYZ * sizeof(uint64));
	}

	VoxelCostArray.Empty(TotalGridSize);
	VoxelCostArray.Init(FLT_MAX, TotalGridSize);

	GeneratedVoxelIndices.Reset();
	MinHeap.Empty();

	ActiveVoxelCount = 0;
	DirtyLevel = EIVSmokeDirtyLevel::Dirty;
	CurrentState = EIVSmokeVoxelVolumeState::Idle;
	bIsInitialized = true;

	LastCollisionUpdateTime = 0.0f;
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
		MinHeap.HeapPush({ CenterIndex, INDEX_NONE, 0.0f });
	}

	CurrentState = EIVSmokeVoxelVolumeState::Expansion;
	ElapsedTime = 0.0f;
	bIsInitialized = false;
}

void AIVSmokeVoxelVolume::UpdateExpansion(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_IVSmoke_UpdateExpansion);

	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT("IVSmoke::AIVSmokeVoxelVolume::UpdateExpansion");

	int32 TotalVoxelNum = GeneratedVoxelIndices.Num();

	ElapsedTime += DeltaTime;

	float CurveValue = 1.0f;
	if (ElapsedTime <= ExpansionDuration)
	{
		CurveValue = GetCurveValue(ElapsedTime, ExpansionDuration, ExpansionCurve);
	}

	int32 TargetSpawnNum = FMath::FloorToInt(MaxVoxelNum * CurveValue);
	int32 SpawnNum = TargetSpawnNum - TotalVoxelNum;

	if (!MinHeap.IsEmpty() && SpawnNum > 0)
	{
		ProcessExpansion(SpawnNum);
	}

	float CurrentProgress = (MaxVoxelNum > 0) ? (static_cast<float>(TotalVoxelNum) / MaxVoxelNum) : 0.0f;

	bool bIsTimeOver = ElapsedTime >= ExpansionDuration + FadeInDuration;

	TryUpdateCollision(CurrentProgress, bIsTimeOver);

	if (bIsTimeOver)
	{
		ElapsedTime = 0.0f;
		LastCollisionUpdateTime = 0.0f;
		LastCollisionUpdateProgress = 0.0f;
		MinHeap.Empty();

		CurrentState = EIVSmokeVoxelVolumeState::Sustain;
	}
}

void AIVSmokeVoxelVolume::UpdateSustain(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_IVSmoke_UpdateSustain);

	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT("IVSmoke::AIVSmokeVoxelVolume::UpdateSustain");

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
		ElapsedTime = 0.0f;

		CurrentState = EIVSmokeVoxelVolumeState::Dissipation;
	}
}

void AIVSmokeVoxelVolume::UpdateDissipation(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_IVSmoke_UpdateDissipation);

	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT("IVSmoke::AIVSmokeVoxelVolume::UpdateDissipation");

	int32 TotalVoxelNum = GeneratedVoxelIndices.Num();

	ElapsedTime += DeltaTime;

	float CurveValue = 1.0f;
	if (ElapsedTime <= DissipationDuration)
	{
		CurveValue = GetCurveValue(ElapsedTime, DissipationDuration, DissipationCurve);
	}

	int32 TargetVoxelNum = FMath::FloorToInt(TotalVoxelNum * (1.0f - CurveValue));
	int32 VoxelNum = FMath::Max(ActiveVoxelCount - TargetVoxelNum, 0);
	if (!MinHeap.IsEmpty() && VoxelNum > 0)
	{
		checkf(VoxelNum <= MinHeap.Num(), TEXT("[AIVSmokeVoxelVolume::UpdateDissipation] : MinHeap not enough elements to dissipate - Heap: %d, Request: %d"), MinHeap.Num(), VoxelNum);
		ProcessDissipation(VoxelNum);
	}

	float CurrentProgress = (TotalVoxelNum > 0) ? 1.0f - (static_cast<float>(ActiveVoxelCount) / TotalVoxelNum) : 0.0f;

	bool bIsTimeOver = ElapsedTime >= DissipationDuration + FadeOutDuration;
	if (!bIsTimeOver)
	{
		TryUpdateCollision(CurrentProgress, false);
	}

	if (bIsTimeOver)
	{
		GeneratedVoxelIndices.Empty();
		MinHeap.Empty();
		FMemory::Memzero(VoxelArray.GetData(), VoxelArray.Num() * sizeof(float));
		ActiveVoxelCount = 0;
		DirtyLevel = EIVSmokeDirtyLevel::Dirty;

		if (CollisionComponent)
		{
			CollisionComponent->ResetCollision();
		}

		CurrentState = EIVSmokeVoxelVolumeState::Finished;
	}
}

void AIVSmokeVoxelVolume::ProcessExpansion(int32 VoxelNum)
{
	SCOPE_CYCLE_COUNTER(STAT_IVSmoke_ProcessExpansion);

	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT("IVSmoke::AIVSmokeVoxelVolume::ProcessExpansion");

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

		if (IsVoxelActive(CurrentNode.Index))
		{
			continue;
		}

		GeneratedVoxelIndices.Add(CurrentNode.Index);
		SetVoxelStateByIndex(CurrentNode.Index, true);
		++SpawnCount;

		if (GeneratedVoxelIndices.Num() >= MaxVoxelNum)
		{
			return;
		}

		if (CurrentNode.ParentIndex != INDEX_NONE)
		{
			FIntVector CurrentGrid = UIVSmokeGridLibrary::IndexToGrid(CurrentNode.Index, GridResolution);
			FIntVector ParentGrid = UIVSmokeGridLibrary::IndexToGrid(CurrentNode.ParentIndex, GridResolution);

			FVector CurrentLocalPos = UIVSmokeGridLibrary::GridToLocal(CurrentGrid, VoxelSize, CenterOffset);
			FVector ParentLocalPos = UIVSmokeGridLibrary::GridToLocal(ParentGrid, VoxelSize, CenterOffset);

			FVector CurrentWorldPos = ActorTrans.TransformPosition(CurrentLocalPos);
			FVector ParentWorldPos = ActorTrans.TransformPosition(ParentLocalPos);

			if (IsConnectionBlocked(World, CurrentWorldPos, ParentWorldPos))
			{
				continue;
			}
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
			float NormX = NextLocalPos.X * InvRadii.X;
			float NormY = NextLocalPos.Y * InvRadii.Y;
			float NormZ = NextLocalPos.Z * InvRadii.Z;

			float DistCost = FMath::Sqrt((NormX * NormX) + (NormY * NormY) + (NormZ * NormZ));
			float NoiseCost = RandomStream.FRandRange(0.0f, ExpansionNoise);

			float NewCost = DistCost + NoiseCost;
			if (NewCost < VoxelCostArray[NextIndex])
			{
				VoxelCostArray[NextIndex] = NewCost;
				MinHeap.HeapPush({ NextIndex, CurrentNode.Index, NewCost });
			}
		}
	}
}

void AIVSmokeVoxelVolume::PrepareDissipation(int32 VoxelNum)
{
	SCOPE_CYCLE_COUNTER(STAT_IVSmoke_PrepareDissipation);

	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT("IVSmoke::AIVSmokeVoxelVolume::PrepareDissipation");

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
			MinHeap.HeapPush({ VoxelIndex, INDEX_NONE, -DissipationPriority });
		}
	}
}

void AIVSmokeVoxelVolume::ProcessDissipation(int32 VoxelNum)
{
	SCOPE_CYCLE_COUNTER(STAT_IVSmoke_ProcessDissipation);

	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT("IVSmoke::AIVSmokeVoxelVolume::ProcessDissipation");

	int32 RemoveCount = 0;
	while (RemoveCount < VoxelNum && !MinHeap.IsEmpty())
	{
		FIVSmokeVoxelNode CurrentNode;
		MinHeap.HeapPop(CurrentNode);

		if (VoxelArray.IsValidIndex(CurrentNode.Index))
		{
			SetVoxelStateByIndex(CurrentNode.Index, false);
		}
		++RemoveCount;
	}
}

#pragma endregion

//~==============================================================================
// Collision
#pragma region Collision

void AIVSmokeVoxelVolume::TryUpdateCollision(float CurrentProgress, bool bForceUpdate)
{
	if (!CollisionComponent)
	{
		return;
	}

	bool bShouldUpdateByTime = ElapsedTime - LastCollisionUpdateTime >= MinCollisionUpdateTimeInterval;
	bool bShouldUpdateByProgress = CurrentProgress - LastCollisionUpdateProgress >= MinCollisionUpdateProgressInterval;
	if (bForceUpdate || (bShouldUpdateByTime && bShouldUpdateByProgress))
	{
		// CollisionComponent->UpdateCollisionWithOctree(VoxelArray, GridResolution, VoxelSize);
		CollisionComponent->UpdateCollision(VoxelBitArray, GridResolution, VoxelSize);
		LastCollisionUpdateTime = ElapsedTime;
		LastCollisionUpdateProgress = CurrentProgress;
	}
}

#pragma endregion

//~==============================================================================
// Data Access
#pragma region DataAccess
TObjectPtr<UIVSmokeHoleGeneratorComponent> AIVSmokeVoxelVolume::GetHoleGeneratorComponent()
{
	if (!IsValid(HoleGeneratorComponent))
	{
		HoleGeneratorComponent = FindComponentByClass<UIVSmokeHoleGeneratorComponent>();
	}

	return HoleGeneratorComponent;
}

TObjectPtr<UIVSmokeCollisionComponent> AIVSmokeVoxelVolume::GetCollisionComponent()
{
	if (!IsValid(CollisionComponent))
	{
		CollisionComponent = FindComponentByClass<UIVSmokeCollisionComponent>();
	}

	return CollisionComponent;
}

FTextureRHIRef AIVSmokeVoxelVolume::GetHoleTexture() const
{
	if (HoleGeneratorComponent)
	{
		return HoleGeneratorComponent->GetHoleTextureRHI();
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

		INC_DWORD_STAT(STAT_IVSmoke_CreatedVoxel);
		const FIntVector GridPos = UIVSmokeGridLibrary::IndexToGrid(LinearIndex, GridResolution);
		const FVector LocalPos = UIVSmokeGridLibrary::GridToLocal(GridPos, VoxelSize, CenterOffset);
		const FVector WorldPos = GetActorTransform().TransformPosition(LocalPos);
		VoxelWorldAABBMin = FVector::Min(WorldPos, VoxelWorldAABBMin);
		VoxelWorldAABBMax = FVector::Max(WorldPos, VoxelWorldAABBMax);
	}
	else if (!bIsActive && bWasActive)
	{
		--ActiveVoxelCount;
		INC_DWORD_STAT(STAT_IVSmoke_DestroyedVoxel);
	}

	DirtyLevel = EIVSmokeDirtyLevel::Dirty;
}

void AIVSmokeVoxelVolume::SetVoxelState(const FIntVector& GridPos, bool bIsActive)
{
	int32 LinearIndex = UIVSmokeGridLibrary::GridToIndex(GridPos, GridResolution);
	SetVoxelStateByIndex(LinearIndex, bIsActive);
}

void AIVSmokeVoxelVolume::SetVoxelStateByIndex(int32 LinearIndex, bool bIsActive)
{
	if (!VoxelArray.IsValidIndex(LinearIndex))
	{
		return;
	}

	const float CurrentWorldTime = FMath::Max(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f, 0.001f);

	const float NewValue = bIsActive ? CurrentWorldTime : -CurrentWorldTime;
	const float OldValue = VoxelArray[LinearIndex];

	if (OldValue != NewValue)
	{
		VoxelArray[LinearIndex] = NewValue;

		UIVSmokeGridLibrary::SetVoxelBit(VoxelBitArray, LinearIndex, GridResolution, bIsActive);

		const bool bWasActive = OldValue > 0.0f;

		if (bIsActive && !bWasActive)
		{
			++ActiveVoxelCount;
			INC_DWORD_STAT(STAT_IVSmoke_CreatedVoxel);

			const FIntVector GridPos = UIVSmokeGridLibrary::IndexToGrid(LinearIndex, GridResolution);
			const FVector LocalPos = UIVSmokeGridLibrary::GridToLocal(GridPos, VoxelSize, CenterOffset);
			const FVector WorldPos = GetActorTransform().TransformPosition(LocalPos);
			VoxelWorldAABBMin = FVector::Min(WorldPos, VoxelWorldAABBMin);
			VoxelWorldAABBMax = FVector::Max(WorldPos, VoxelWorldAABBMax);
		}
		else if (!bIsActive && bWasActive)
		{
			--ActiveVoxelCount;
			INC_DWORD_STAT(STAT_IVSmoke_DestroyedVoxel);
		}

		DirtyLevel = EIVSmokeDirtyLevel::Dirty;
	}
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

	if (CollisionComponent)
	{
		CollisionComponent->ResetCollision();
	}

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

	if (CollisionComponent)
	{
		CollisionComponent->DrawDebugVisualization();
	}
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
		if (!VoxelArray.IsValidIndex(VoxelIndex) || !IsVoxelActive(VoxelIndex))
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

		if (!VoxelArray.IsValidIndex(VoxelIndex) || !IsVoxelActive(VoxelIndex))
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

void AIVSmokeVoxelVolume::UpdateVisualLogger() const
{
#if ENABLE_VISUAL_LOG
	const FVector MyLoc = GetActorLocation();
	const uint32 Checksum = CalculateSimulationChecksum();

	UE_VLOG(this, LogIVSmokeVis, Log,
		TEXT("State: %d | Voxels: %d/%d | Heap: %d | Hash: 0x%08X"),
		(int32)CurrentState,
		ActiveVoxelCount,
		GeneratedVoxelIndices.Num(),
		MinHeap.Num(),
		Checksum
	);

	if (ActiveVoxelCount > MaxVoxelNum)
	{
		UE_VLOG(this, LogIVSmokeVis, Error, TEXT("Active Voxel Count Exceeded Max Limit!"));
	}

	FBox VoxelBounds(VoxelWorldAABBMin, VoxelWorldAABBMax);

	FColor DrawColor = FColor::White;
	switch(CurrentState)
	{
	case EIVSmokeVoxelVolumeState::Expansion: DrawColor = FColor::Green; break;
	case EIVSmokeVoxelVolumeState::Sustain:   DrawColor = FColor::Yellow; break;
	case EIVSmokeVoxelVolumeState::Dissipation: DrawColor = FColor::Red; break;
	}

	UE_VLOG_BOX(this, LogIVSmokeVis, Log, VoxelBounds, DrawColor, TEXT("SmokeBounds"));

	UE_VLOG_LOCATION(this, LogIVSmokeVis, Log, MyLoc, 30.0f, FColor::Blue, TEXT("Center"));
#endif
}

uint32 AIVSmokeVoxelVolume::CalculateSimulationChecksum() const
{
	uint32 Checksum = 0;

	Checksum = FCrc::MemCrc32(&ActiveVoxelCount, sizeof(int32), Checksum);

	int32 StateInt = (int32)CurrentState;
	Checksum = FCrc::MemCrc32(&StateInt, sizeof(int32), Checksum);

	if (VoxelBitArray.Num() > 0)
	{
		Checksum = FCrc::MemCrc32(VoxelBitArray.GetData(), VoxelBitArray.Num() * sizeof(uint64), Checksum);
	}

	return Checksum;
}

#pragma endregion
