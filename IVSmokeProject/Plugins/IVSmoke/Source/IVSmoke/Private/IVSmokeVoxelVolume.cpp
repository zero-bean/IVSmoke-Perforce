// Copyright (c) 2026, Team SDB. All rights reserved.

#include "IVSmokeVoxelVolume.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "IVSmoke.h"
#include "IVSmokeCollisionComponent.h"
#include "IVSmokeGridLibrary.h"
#include "IVSmokeHoleGeneratorComponent.h"
#include "IVSmokeRenderer.h"
#include "Net/UnrealNetwork.h"

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

	if (HasAuthority())
	{
		if (bAutoStart)
		{
			StartSimulation();
		}
	}
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

	DOREPLIFETIME(AIVSmokeVoxelVolume, ServerState);
}

void AIVSmokeVoxelVolume::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_Client)
	{
		if (World->GetGameState() == nullptr)
		{
			return;
		}
	}

	Super::Tick(DeltaTime);

	if (ActiveVoxelNum > 0)
	{
		INC_DWORD_STAT_BY(STAT_IVSmoke_ActiveVoxelCount, ActiveVoxelNum);
	}

	switch (ServerState.State)
	{
	case EIVSmokeVoxelVolumeState::Expansion:
		UpdateExpansion();
		break;
	case EIVSmokeVoxelVolumeState::Sustain:
		UpdateSustain();
		break;
	case EIVSmokeVoxelVolumeState::Dissipation:
		UpdateDissipation();
		break;
	case EIVSmokeVoxelVolumeState::Finished:
		[[fallthrough]];
	case EIVSmokeVoxelVolumeState::Idle:
		[[fallthrough]];
	default:
		break;
	}

	TryUpdateCollision();

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
			PropertyName == GET_MEMBER_NAME_CHECKED(AIVSmokeVoxelVolume, ExpansionNoise)	||
			PropertyName == GET_MEMBER_NAME_CHECKED(AIVSmokeVoxelVolume, DissipationNoise);

	if (bShouldResetSimulation && DebugSettings.bDebugEnabled)
	{
		StartPreviewSimulation();
	}
}

void AIVSmokeVoxelVolume::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	if (bFinished && DebugSettings.bDebugEnabled)
	{
		StartPreviewSimulation();
	}
}
#endif

#pragma endregion

//~==============================================================================
// Flood Fill Simulation
#pragma region Simulation

void AIVSmokeVoxelVolume::Initialize()
{
	FIntVector GridResolution = GetGridResolution();

	const int32 TotalGridSizeYZ = GridResolution.Y * GridResolution.Z;
	const int32 TotalGridSize = GridResolution.X * TotalGridSizeYZ;

	if (VoxelBirthTimes.Num() != TotalGridSize)
	{
		VoxelBirthTimes.SetNumUninitialized(TotalGridSize);
	}

	if (VoxelDeathTimes.Num() != TotalGridSize)
	{
		VoxelDeathTimes.SetNumUninitialized(TotalGridSize);
	}

	if (VoxelCosts.Num() != TotalGridSize)
	{
		VoxelCosts.SetNumUninitialized(TotalGridSize);
	}

	if (VoxelBits.Num() != TotalGridSizeYZ)
	{
		VoxelBits.SetNumUninitialized(TotalGridSizeYZ);
	}

	GeneratedVoxelIndices.Reserve(MaxVoxelNum);

	ExpansionHeap.Reserve(MaxVoxelNum);
	DissipationHeap.Reserve(MaxVoxelNum);

	bIsInitialized = true;
}

void AIVSmokeVoxelVolume::StartSimulation_Implementation()
{
	StartSimulationInternal();
}

void AIVSmokeVoxelVolume::StopSimulation_Implementation(bool bImmediate)
{
	StopSimulationInternal(bImmediate);
}

void AIVSmokeVoxelVolume::ResetSimulation_Implementation()
{
	ResetSimulationInternal();
}

void AIVSmokeVoxelVolume::OnRep_ServerState()
{
	UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_Client)
	{
		AGameStateBase* GameState = World->GetGameState();

		if (!GameState || GameState->GetServerWorldTimeSeconds() == 0.0f)
		{
			FTimerHandle RetryHandle;
			World->GetTimerManager().SetTimer(RetryHandle, this, &AIVSmokeVoxelVolume::OnRep_ServerState, 0.1f, false);

			UE_LOG(LogIVSmoke, Warning, TEXT("[AIVSmokeVoxelVolume::OnRep_ServerState] GameState not ready yet. Retrying in 0.1s..."));
			return;
		}
	}

	if (!bIsInitialized)
	{
		Initialize();
	}

	if (LocalGeneration != ServerState.Generation)
	{
		FastForwardSimulation();

		LocalGeneration = ServerState.Generation;

		TryUpdateCollision(true);

		return;
	}

	HandleStateTransition(ServerState.State);
}

void AIVSmokeVoxelVolume::HandleStateTransition(EIVSmokeVoxelVolumeState NewState)
{
	if (LocalState == NewState)
	{
		return;
	}

	SimTime = 0.0f;

	switch (NewState)
	{
	case EIVSmokeVoxelVolumeState::Idle:
		ClearSimulationData();
		break;
	case EIVSmokeVoxelVolumeState::Expansion:
	{
		if (LocalState != EIVSmokeVoxelVolumeState::Idle ||
			LocalState != EIVSmokeVoxelVolumeState::Finished)
		{
			ClearSimulationData();
		}

		RandomStream.Initialize(ServerState.RandomSeed);

		int32 CenterIndex = UIVSmokeGridLibrary::GridToIndex(GetCenterOffset(), GetGridResolution());

		if (VoxelCosts.IsValidIndex(CenterIndex))
		{
			VoxelCosts[CenterIndex] = 0.0f;
			ExpansionHeap.HeapPush({CenterIndex, INDEX_NONE, 0.0f});

		}
		break;
	}
	case EIVSmokeVoxelVolumeState::Sustain:
		TryUpdateCollision(true);
		break;
	case EIVSmokeVoxelVolumeState::Dissipation:
		break;
	case EIVSmokeVoxelVolumeState::Finished:
		if (bDestroyOnFinish)
		{
			if (GetWorld() && GetWorld()->IsGameWorld())
			{
				Destroy();
			}
			else
			{
				ClearSimulationData();
				bIsEditorPreviewing = false;
			}
		}
		ClearSimulationData();
		break;
	}

	LocalState = NewState;
}

void AIVSmokeVoxelVolume::ClearSimulationData()
{
	if (!bIsInitialized)
	{
		Initialize();
	}

	FIntVector GridResolution = GetGridResolution();

	const int32 TotalGridSizeYZ = GridResolution.Y * GridResolution.Z;
	const int32 TotalGridSize = GridResolution.X * TotalGridSizeYZ;

	checkf(VoxelBirthTimes.Num() == TotalGridSize, TEXT("[AIVSmokeVoxelVolume::ClearSimulationData] Invalid Buffer Size : VoxelBirthTimes size %d does not match TotalGridSize %d."), VoxelBirthTimes.Num(), TotalGridSize);
	FMemory::Memzero(VoxelBirthTimes.GetData(), VoxelBirthTimes.Num() * sizeof(float));

	checkf(VoxelDeathTimes.Num() == TotalGridSize, TEXT("[AIVSmokeVoxelVolume::ClearSimulationData] Invalid Buffer Size : VoxelDeathTimes size %d does not match TotalGridSize %d."), VoxelDeathTimes.Num(), TotalGridSize);
	FMemory::Memzero(VoxelDeathTimes.GetData(), VoxelDeathTimes.Num() * sizeof(float));

	checkf(VoxelBits.Num() == TotalGridSizeYZ, TEXT("[AIVSmokeVoxelVolume::ClearSimulationData] Invalid Buffer Size : VoxelBits size %d does not match TotalGridSizeYZ %d."), VoxelBits.Num(), TotalGridSizeYZ);
	FMemory::Memzero(VoxelBits.GetData(), VoxelBits.Num() * sizeof(uint64));

	VoxelCosts.Init(FLT_MAX, VoxelCosts.Num());

	GeneratedVoxelIndices.Reset();

	ExpansionHeap.Reset();
	DissipationHeap.Reset();

	ActiveVoxelNum = 0;
	SimTime = 0.0f;
	DirtyLevel = EIVSmokeDirtyLevel::Dirty;

	if (CollisionComponent)
	{
		CollisionComponent->ResetCollision();
	}
}

bool AIVSmokeVoxelVolume::IsConnectionBlocked(const UWorld* World, const FVector& BeginPos, const FVector& EndPos) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT("IVSmoke::AIVSmokeVoxelVolume::IsConnectionBlocked");

	if (!bEnableSimulationCollision)
	{
		return false;
	}

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

void AIVSmokeVoxelVolume::StartSimulationInternal()
{
	if (!bIsInitialized)
	{
		Initialize();
	}

	ResetSimulationInternal();

	ServerState.RandomSeed = FMath::Rand();
	ServerState.ExpansionStartTime = GetSyncWorldTimeSeconds();

	ServerState.SustainStartTime = 0.0f;
	ServerState.DissipationStartTime = 0.0f;

	ServerState.State = EIVSmokeVoxelVolumeState::Expansion;

	HandleStateTransition(ServerState.State);
}

void AIVSmokeVoxelVolume::StopSimulationInternal(bool bImmediate)
{
	if (ServerState.State == EIVSmokeVoxelVolumeState::Finished)
	{
		return;
	}

	if (bImmediate)
	{
		ServerState.State = EIVSmokeVoxelVolumeState::Finished;
	}
	else if (ServerState.State == EIVSmokeVoxelVolumeState::Expansion ||
			 ServerState.State == EIVSmokeVoxelVolumeState::Sustain)
	{
		ServerState.State = EIVSmokeVoxelVolumeState::Dissipation;
		ServerState.DissipationStartTime = GetSyncWorldTimeSeconds();
	}

	HandleStateTransition(ServerState.State);
}

void AIVSmokeVoxelVolume::ResetSimulationInternal()
{
	ServerState.State = EIVSmokeVoxelVolumeState::Idle;
	ServerState.Generation += 1;

	ServerState.ExpansionStartTime = 0.0f;
	ServerState.SustainStartTime = 0.0f;
	ServerState.DissipationStartTime = 0.0f;

	HandleStateTransition(ServerState.State);
}

void AIVSmokeVoxelVolume::FastForwardSimulation()
{
	bIsFastForwarding = true;

	if (ServerState.State == EIVSmokeVoxelVolumeState::Expansion	||
		ServerState.State == EIVSmokeVoxelVolumeState::Sustain		||
		ServerState.State == EIVSmokeVoxelVolumeState::Dissipation)
	{
		HandleStateTransition(EIVSmokeVoxelVolumeState::Expansion);
		UpdateExpansion();
	}
	if (ServerState.State == EIVSmokeVoxelVolumeState::Sustain ||
		ServerState.State == EIVSmokeVoxelVolumeState::Dissipation)
	{
		HandleStateTransition(EIVSmokeVoxelVolumeState::Sustain);
		UpdateSustain();
	}
	if (ServerState.State == EIVSmokeVoxelVolumeState::Dissipation)
	{
		HandleStateTransition(EIVSmokeVoxelVolumeState::Dissipation);
		UpdateDissipation();
	}

	HandleStateTransition(ServerState.State);

	bIsFastForwarding = false;
}


void AIVSmokeVoxelVolume::UpdateExpansion()
{
	SCOPE_CYCLE_COUNTER(STAT_IVSmoke_UpdateExpansion);
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT("IVSmoke::AIVSmokeVoxelVolume::UpdateExpansion");

	const float CurrentSyncTime = GetSyncWorldTimeSeconds();
	const float CurrentSimTime = CurrentSyncTime - ServerState.ExpansionStartTime;

	float StartSimTime = SimTime;
	float EndSimTime = CurrentSimTime;

	SimTime = CurrentSimTime;

	int32 TargetSpawnNum = 0;

	if (EndSimTime < ExpansionDuration)
	{
		float CurveValue = GetCurveValue(CurrentSimTime, ExpansionDuration, ExpansionCurve);
		TargetSpawnNum = FMath::FloorToInt(MaxVoxelNum * CurveValue);
	}
	else
	{
		EndSimTime = ExpansionDuration;
		TargetSpawnNum = MaxVoxelNum;
	}

	int32 SpawnNum = TargetSpawnNum - ActiveVoxelNum;

	if (!ExpansionHeap.IsEmpty() && SpawnNum > 0)
	{
		ProcessExpansion(SpawnNum, StartSimTime, EndSimTime);
	}

	if (CurrentSimTime >= ExpansionDuration + FadeInDuration)
	{
		if (HasAuthority())
		{
			ServerState.State = EIVSmokeVoxelVolumeState::Sustain;
			ServerState.SustainStartTime = GetSyncWorldTimeSeconds();

			HandleStateTransition(ServerState.State);
		}
	}
}

void AIVSmokeVoxelVolume::UpdateSustain()
{
	SCOPE_CYCLE_COUNTER(STAT_IVSmoke_UpdateSustain);
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT("IVSmoke::AIVSmokeVoxelVolume::UpdateSustain");

	const float CurrentSyncTime = GetSyncWorldTimeSeconds();
	const float CurrentSimTime = CurrentSyncTime - ServerState.SustainStartTime;

	SimTime = CurrentSimTime;

	if (!bIsInfinite && CurrentSimTime >= SustainDuration)
	{
		if (HasAuthority())
		{
			ServerState.State = EIVSmokeVoxelVolumeState::Dissipation;
			ServerState.DissipationStartTime = GetSyncWorldTimeSeconds();

			HandleStateTransition(ServerState.State);
		}
	}
}

void AIVSmokeVoxelVolume::UpdateDissipation()
{
	SCOPE_CYCLE_COUNTER(STAT_IVSmoke_UpdateDissipation);
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT("IVSmoke::AIVSmokeVoxelVolume::UpdateDissipation");

	const float CurrentSyncTime = GetSyncWorldTimeSeconds();
	const float CurrentSimTime = CurrentSyncTime - ServerState.DissipationStartTime;

	float StartSimTime = SimTime;
	float EndSimTime = CurrentSimTime;

	SimTime = CurrentSimTime;

	int32 TargetAliveNum = GeneratedVoxelIndices.Num();

	if (CurrentSimTime < DissipationDuration)
	{
		float CurveValue = GetCurveValue(CurrentSimTime, DissipationDuration, DissipationCurve);
		TargetAliveNum = FMath::FloorToInt(GeneratedVoxelIndices.Num() * CurveValue);
	}
	else
	{
		EndSimTime = DissipationDuration;
		TargetAliveNum = 0;
	}

	int32 RemoveNum = DissipationHeap.Num() - TargetAliveNum;

	if (RemoveNum > 0)
	{
		ProcessDissipation(RemoveNum, StartSimTime, EndSimTime);
	}

	if (CurrentSimTime >= DissipationDuration + FadeOutDuration)
	{
		SimTime = 0.0f;

		TryUpdateCollision(true);

		if (HasAuthority())
		{
			ServerState.State = EIVSmokeVoxelVolumeState::Finished;

			HandleStateTransition(ServerState.State);
		}
	}
}

void AIVSmokeVoxelVolume::ProcessExpansion(int32 SpawnNum, float StartSimTime, float EndSimTime)
{
	SCOPE_CYCLE_COUNTER(STAT_IVSmoke_ProcessExpansion);
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT("IVSmoke::AIVSmokeVoxelVolume::ProcessExpansion");

	if (SpawnNum <= 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTransform ActorTrans = GetActorTransform();

	FIntVector GridResolution = GetGridResolution();
	FIntVector CenterOffset = GetCenterOffset();

	int32 SpawnCount = 0;

	FVector InvRadii;
	InvRadii.X = 1.0f / FMath::Max(UE_KINDA_SMALL_NUMBER, Radii.X);
	InvRadii.Y = 1.0f / FMath::Max(UE_KINDA_SMALL_NUMBER, Radii.Y);
	InvRadii.Z = 1.0f / FMath::Max(UE_KINDA_SMALL_NUMBER, Radii.Z);

	const float InvSpawnNum = 1.0f / SpawnNum;

	while (SpawnCount < SpawnNum && !ExpansionHeap.IsEmpty())
	{
		FIVSmokeVoxelNode CurrentNode;
		ExpansionHeap.HeapPop(CurrentNode);

		if (CurrentNode.Cost > VoxelCosts[CurrentNode.Index])
		{
			continue;
		}

		if (IsVoxelActive(CurrentNode.Index))
		{
			continue;
		}

		float Alpha = SpawnCount * InvSpawnNum;
		float BirthTime = ServerState.ExpansionStartTime + FMath::Lerp(StartSimTime, EndSimTime, Alpha);
		SetVoxelBirthTime(CurrentNode.Index, BirthTime);

		GeneratedVoxelIndices.Add(CurrentNode.Index);
		++SpawnCount;

		float DissipationCost = VoxelCosts[CurrentNode.Index] + RandomStream.FRandRange(0.0f, DissipationNoise);
		DissipationHeap.HeapPush({CurrentNode.Index, INDEX_NONE, DissipationCost});

		if (GetActiveVoxelNum() >= MaxVoxelNum)
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
			if (VoxelCosts[NextIndex] != FLT_MAX)
			{
				continue;
			}

			FVector NextLocalPos = UIVSmokeGridLibrary::GridToLocal(NextGrid, VoxelSize, CenterOffset);
			float NormX = NextLocalPos.X * InvRadii.X;
			float NormY = NextLocalPos.Y * InvRadii.Y;
			float NormZ = NextLocalPos.Z * InvRadii.Z;

			float DistCost = FMath::Sqrt((NormX * NormX) + (NormY * NormY) + (NormZ * NormZ));
			float NoiseCost = RandomStream.FRandRange(0.0f, ExpansionNoise);

			float ExpansionCost = DistCost + NoiseCost;
			if (ExpansionCost < VoxelCosts[NextIndex])
			{
				VoxelCosts[NextIndex] = ExpansionCost;
				ExpansionHeap.HeapPush({ NextIndex, CurrentNode.Index, ExpansionCost });
			}
		}
	}
}

void AIVSmokeVoxelVolume::ProcessDissipation(int32 RemoveNum, float StartSimTime, float EndSimTime)
{
	SCOPE_CYCLE_COUNTER(STAT_IVSmoke_ProcessDissipation);
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT("IVSmoke::AIVSmokeVoxelVolume::ProcessDissipation");

	if (RemoveNum <= 0)
	{
		return;
	}

	float InvRemoveNum = 1.0f / RemoveNum;

	int32 RemoveCount = 0;
	while (RemoveCount < RemoveNum && !DissipationHeap.IsEmpty())
	{
		FIVSmokeVoxelNode CurrentNode;
		DissipationHeap.HeapPop(CurrentNode);

		float Alpha = RemoveCount * InvRemoveNum;
		float DeathTime = ServerState.DissipationStartTime + FMath::Lerp(StartSimTime, EndSimTime, Alpha);

		SetVoxelDeathTime(CurrentNode.Index, DeathTime);

		++RemoveCount;
	}
}

void AIVSmokeVoxelVolume::SetVoxelBirthTime(int32 Index, float BirthTime)
{
	if (!VoxelBirthTimes.IsValidIndex(Index))
	{
		return;
	}

	if (VoxelBirthTimes[Index] > 0.0f)
	{
		return;
	}

	float SafeBirthTime = FMath::Max(BirthTime, 0.001f);
	VoxelBirthTimes[Index] = SafeBirthTime;

	if (VoxelDeathTimes.IsValidIndex(Index))
	{
		VoxelDeathTimes[Index] = 0.0f;
	}

	FIntVector GridResolution = GetGridResolution();
	FIntVector CenterOffset = GetCenterOffset();

	UIVSmokeGridLibrary::SetVoxelBit(VoxelBits, Index, GridResolution, true);

	++ActiveVoxelNum;
	INC_DWORD_STAT(STAT_IVSmoke_CreatedVoxel);

	DirtyLevel = EIVSmokeDirtyLevel::Dirty;

	const FIntVector GridPos = UIVSmokeGridLibrary::IndexToGrid(Index, GridResolution);
	const FVector LocalPos = UIVSmokeGridLibrary::GridToLocal(GridPos, VoxelSize, CenterOffset);
	const FVector WorldPos = GetActorTransform().TransformPosition(LocalPos);
	VoxelWorldAABBMin = FVector::Min(WorldPos, VoxelWorldAABBMin);
	VoxelWorldAABBMax = FVector::Max(WorldPos, VoxelWorldAABBMax);
}

void AIVSmokeVoxelVolume::SetVoxelDeathTime(int32 Index, float DeathTime)
{
	if (!VoxelDeathTimes.IsValidIndex(Index))
	{
		return;
	}

	if (VoxelDeathTimes[Index] > 0.0f)
	{
		return;
	}

	const float SafeDeathTime = FMath::Max(DeathTime, 0.001f);
	VoxelDeathTimes[Index] = SafeDeathTime;

	FIntVector GridResolution = GetGridResolution();

	UIVSmokeGridLibrary::SetVoxelBit(VoxelBits, Index, GridResolution, false);

	--ActiveVoxelNum;
	INC_DWORD_STAT(STAT_IVSmoke_DestroyedVoxel)

	DirtyLevel = EIVSmokeDirtyLevel::Dirty;
}

#pragma endregion

//~==============================================================================
// Collision
#pragma region Collision

void AIVSmokeVoxelVolume::TryUpdateCollision(bool bForce)
{
	if (bIsFastForwarding)
	{
		return;
	}

	if (CollisionComponent)
	{
		CollisionComponent->TryUpdateCollision(
			VoxelBits,
			GetGridResolution(),
			VoxelSize,
			ActiveVoxelNum,
			GetSyncWorldTimeSeconds(),
			bForce
		);
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

float AIVSmokeVoxelVolume::GetSyncWorldTimeSeconds() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}

	if (World->GetNetMode() == NM_Client)
	{
		if (AGameStateBase* GameState = World->GetGameState())
		{
			return GameState->GetServerWorldTimeSeconds();
		}
	}

	return World->GetTimeSeconds();
}

#pragma endregion

//~==============================================================================
// Debug
#pragma region Debug

void AIVSmokeVoxelVolume::StartPreviewSimulation()
{
#if WITH_EDITOR
	if (!DebugSettings.bDebugEnabled)
	{
		return;
	}

	bIsEditorPreviewing = true;

	StartSimulationInternal();
#endif
}

void AIVSmokeVoxelVolume::StopPreviewSimulation()
{
#if WITH_EDITOR
	bIsEditorPreviewing = false;

	ResetSimulationInternal();

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

	FIntVector GridResolution = GetGridResolution();

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
		if (!IsVoxelActive(VoxelIndex))
		{
			continue;
		}

		FIntVector GridResolution = GetGridResolution();
		FIntVector CenterOffset = GetCenterOffset();

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

		if (!IsVoxelActive(VoxelIndex))
		{
			continue;
		}

		FIntVector GridResolution = GetGridResolution();
		FIntVector CenterOffset = GetCenterOffset();

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
	switch (ServerState.State)
	{
	case EIVSmokeVoxelVolumeState::Idle:		StateStr = TEXT("Idle"); break;
	case EIVSmokeVoxelVolumeState::Expansion:	StateStr = TEXT("Expansion"); break;
	case EIVSmokeVoxelVolumeState::Sustain:		StateStr = TEXT("Sustain"); break;
	case EIVSmokeVoxelVolumeState::Dissipation:	StateStr = TEXT("Dissipation"); break;
	case EIVSmokeVoxelVolumeState::Finished:	StateStr = TEXT("Finished"); break;
	default:									StateStr = TEXT("Unknown"); break;
	}

	float Percent = MaxVoxelNum > 0 ? (static_cast<float>(ActiveVoxelNum) / MaxVoxelNum * 100.0f) : 0.0f;

	FString DebugMsg = FString::Printf(
		TEXT("State: %s\nSeed: %d\nTime: %.2fs\nVoxels: %d / %d (%.1f%%)\nHeap: %d\nChecksum: %u"),
		*StateStr,
		ServerState.RandomSeed,
		SimTime,
		ActiveVoxelNum,
		MaxVoxelNum,
		Percent,
		ExpansionHeap.Num(),
		CalculateSimulationChecksum()
	);

	FIntVector GridResolution = GetGridResolution();

	FVector TextPos = GetActorLocation();
	TextPos.Z += (GridResolution.Z * VoxelSize * 0.5f) + 50.0f;

	DrawDebugString(World, TextPos, DebugMsg, nullptr, FColor::White, 0.0f, true, 1.2f);
#endif
}

uint32 AIVSmokeVoxelVolume::CalculateSimulationChecksum() const
{
	uint32 Checksum = 0;

	Checksum = FCrc::MemCrc32(&ActiveVoxelNum, sizeof(int32), Checksum);

	int32 StateInt = (int32)ServerState.State;
	Checksum = FCrc::MemCrc32(&StateInt, sizeof(int32), Checksum);

	if (VoxelBits.Num() > 0)
	{
		Checksum = FCrc::MemCrc32(VoxelBits.GetData(), VoxelBits.Num() * sizeof(uint64), Checksum);
	}

	return Checksum;
}

#pragma endregion
