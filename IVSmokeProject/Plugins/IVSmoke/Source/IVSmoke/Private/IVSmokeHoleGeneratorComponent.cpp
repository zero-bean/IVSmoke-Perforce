// Copyright (c) 2026, Team SDB. All rights reserved.

#include "IVSmokeHoleGeneratorComponent.h"

#include "Engine/TextureRenderTargetVolume.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GlobalShader.h"
#include "IVSmoke.h"
#include "IVSmokeHoleCarveCS.h"
#include "IVSmokeHolePreset.h"
#include "IVSmokePostProcessPass.h"
#include "IVSmokeVoxelVolume.h"
#include "Net/UnrealNetwork.h"
#include "RHICommandList.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "TextureResource.h"

UIVSmokeHoleGeneratorComponent::UIVSmokeHoleGeneratorComponent()
	: bHoleTextureDirty(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UIVSmokeHoleGeneratorComponent::BeginPlay()
{
	Super::BeginPlay();

	// 1. Setup Fast TArray owner for replication callbacks
	ActiveHoles.OwnerComponent = this;
	ActiveHoles.Reserve(MaxHoles);

	// Join process
	if (ActiveHoles.Num() > 0)
	{
		MarkHoleTextureDirty();
	}

#if !UE_SERVER
	Local_InitializeHoleTexture();
#endif
}

void UIVSmokeHoleGeneratorComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 1. Server cleans up expired holes and updates dynamic objects
	if (GetOwner()->HasAuthority())
	{
		Authority_CleanupExpiredHoles();
		Authority_UpdateDynamicSubjectList();
	}

	// 2. All host update voxel volume area
	SetBoxToVoxelAABB();

	// 3. If any holes exist, then must be updated
	if (ActiveHoles.Num() > 0)
	{
		MarkHoleTextureDirty();
	}

	// 4. Client & Standalone rebuild texture
#if !UE_SERVER
	if (bHoleTextureDirty && ActiveHoles.Num() > 0)
	{
		Local_RebuildHoleTexture();
		MarkHoleTextureDirty(false);
	}
#endif
}

void UIVSmokeHoleGeneratorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void UIVSmokeHoleGeneratorComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UIVSmokeHoleGeneratorComponent, ActiveHoles);
}

//~============================================================================
// Public API (Blueprint & C++)
#pragma region API
void UIVSmokeHoleGeneratorComponent::RequestPenetrationHole(const FVector3f InOrigin, const FVector3f Direction, UIVSmokeHolePreset* Preset)
{
	if (!Preset)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[RequestPenetrationHole] Preset is null"));
		return;
	}

	if (Preset->HoleType != EIVSmokeHoleType::Penetration)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[RequestPenetrationHole] Preset is not Penetration type"));
		return;
	}

	Internal_RequestPenetrationHole(InOrigin, Direction, Preset->GetPresetID());
}

void UIVSmokeHoleGeneratorComponent::RequestExplosionHole(const FVector3f Origin, UIVSmokeHolePreset* Preset)
{
	if (!Preset)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[RequestExplosionHole] Preset is null"));
		return;
	}

	if (Preset->HoleType != EIVSmokeHoleType::Explosion)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[RequestExplosionHole] Preset is not Explosion type"));
		return;
	}

	Internal_RequestExplosionHole(Origin, Preset->GetPresetID());
}

void UIVSmokeHoleGeneratorComponent::RequestTrackDynamicObject(AActor* TargetActor, UIVSmokeHolePreset* Preset)
{
	if (!TargetActor)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[RequestTrackDynamicObject] TargetActor is null"));
		return;
	}

	if (!Preset)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[RequestTrackDynamicObject] Preset is null"));
		return;
	}

	if (Preset->HoleType != EIVSmokeHoleType::Dynamic)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[RequestTrackDynamicObject] Preset is not Dynamic type"));
		return;
	}

	Internal_RequestDynamicHole(TargetActor, Preset->GetPresetID());
}
#pragma endregion

//~============================================================================
// Internal Server RPC
#pragma region Server RPC
void UIVSmokeHoleGeneratorComponent::Internal_RequestPenetrationHole_Implementation(const FVector3f& InOrigin, const FVector3f& InDirection, const uint8 PresetID)
{
	const TObjectPtr<UIVSmokeHolePreset> Preset = UIVSmokeHolePreset::FindByID(PresetID);
	if (!Preset)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[Internal_RequestPenetrationHole] Invalid PresetID: %d"), PresetID);
		return;
	}

	if (Preset->Duration <= 0.0f)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[Internal_RequestPenetrationHole] Invalid Lifetime: %f"), Preset->Duration);
		return;
	}

	if (Preset->HoleType != EIVSmokeHoleType::Penetration)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[Internal_RequestPenetrationHole] Preset is not Penetration type"));
		return;
	}

	// 1. Check whether it passes through the smoke volume
	FVector3f EntryPoint, ExitPoint;
	if (!Authority_CalculatePenetrationPoints(InOrigin, InDirection, Preset->Radius, EntryPoint, ExitPoint))
	{
		return;
	}

	// 2. Create Hole
	FIVSmokeHoleData HoleData;
	HoleData.Position = EntryPoint;
	HoleData.EndPosition = ExitPoint;
	HoleData.PresetID = PresetID;
	HoleData.ExpirationServerTime = GetSyncedTime() + Preset->Duration;
	Authority_CreateHole(HoleData);
}

void UIVSmokeHoleGeneratorComponent::Internal_RequestExplosionHole_Implementation(const FVector3f& Origin, const uint8 PresetID)
{
	const TObjectPtr<UIVSmokeHolePreset> Preset = UIVSmokeHolePreset::FindByID(PresetID);
	if (!Preset)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[Internal_RequestExplosionHole] Invalid PresetID: %d"), PresetID);
		return;
	}

	if (Preset->Duration <= 0.0f)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[Internal_RequestExplosionHole] Invalid Lifetime: %f"), Preset->Duration);
		return;
	}

	if (Preset->HoleType != EIVSmokeHoleType::Explosion)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[Internal_RequestExplosionHole] Preset is not Explosion type"));
		return;
	}

	// 1. Check whether the smoke volume intersects with the explosion
	const FBox3f VolumeBox = FBox3f(Bounds.GetBox());
	const FVector3f ExpandedMin = VolumeBox.Min - FVector3f(Preset->Radius);
	const FVector3f ExpandedMax = VolumeBox.Max + FVector3f(Preset->Radius);
	if (const FBox3f ExpandedBox(ExpandedMin, ExpandedMax); !ExpandedBox.IsInside(Origin))
	{
		return;
	}

	// 2. Create Hole
	FIVSmokeHoleData HoleData;
	HoleData.Position = Origin;
	HoleData.EndPosition = Origin;
	HoleData.PresetID = PresetID;
	HoleData.ExpirationServerTime = GetSyncedTime() + Preset->Duration;
	Authority_CreateHole(HoleData);
}

void UIVSmokeHoleGeneratorComponent::Internal_RequestDynamicHole_Implementation(AActor* TargetActor, const uint8 PresetID)
{
	const TObjectPtr<UIVSmokeHolePreset> Preset = UIVSmokeHolePreset::FindByID(PresetID);
	if (!Preset)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[Internal_RequestDynamicHole] Invalid PresetID: %d"), PresetID);
		return;
	}

	if (Preset->Duration <= 0.0f)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[Internal_RequestDynamicHole] Invalid Duration: %f"), Preset->Duration);
		return;
	}

	if (Preset->HoleType != EIVSmokeHoleType::Dynamic)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[Internal_RequestDynamicHole] Preset is not Dynamic type"));
		return;
	}

	// 1. Check if already registered
	for (const FIVSmokeHoleDynamicSubject& Tracker : DynamicSubjectList)
	{
		if (Tracker.TargetActor == TargetActor)
		{
			UE_LOG(LogIVSmoke, Warning, TEXT("[Internal_RequestDynamicHole] Actor already registered"));
			return;
		}
	}

	// 2. Register new subject
	FIVSmokeHoleDynamicSubject NewDynamicSubject;
	NewDynamicSubject.TargetActor = TargetActor;
	NewDynamicSubject.PresetID = PresetID;
	NewDynamicSubject.LastWorldPosition = FVector3f(TargetActor->GetActorLocation());
	NewDynamicSubject.LastWorldRotation = TargetActor->GetActorQuat();
	DynamicSubjectList.Add(NewDynamicSubject);
}
#pragma endregion

//~============================================================================
// Authority Only
#pragma region Authority Only
void UIVSmokeHoleGeneratorComponent::Authority_CreateHole(const FIVSmokeHoleData& HoleData)
{
	if (ActiveHoles.Num() < MaxHoles)
	{
		ActiveHoles.AddHole(HoleData);
	}
	else
	{
		int32 OldestIndex = 0;
		float MinTime = FLT_MAX;

		for (int32 i = 0; i < ActiveHoles.Num(); ++i)
		{
			if (ActiveHoles[i].ExpirationServerTime < MinTime)
			{
				MinTime = ActiveHoles[i].ExpirationServerTime;
				OldestIndex = i;
			}
		}

		FIVSmokeHoleData& Target = ActiveHoles[OldestIndex];
		Target.Position = HoleData.Position;
		Target.EndPosition = HoleData.EndPosition;
		Target.ExpirationServerTime = HoleData.ExpirationServerTime;
		Target.PresetID = HoleData.PresetID;
		ActiveHoles.MarkItemDirty(Target);
	}

	MarkHoleTextureDirty();
}

void UIVSmokeHoleGeneratorComponent::Authority_CleanupExpiredHoles()
{
	const float CurrentServerTime = GetSyncedTime();

	for (int32 i = ActiveHoles.Num() - 1; i >= 0; --i)
	{
		if (ActiveHoles[i].IsExpired(CurrentServerTime))
		{
			ActiveHoles.RemoveAtSwap(i);
			MarkHoleTextureDirty();
		}
	}
}

bool UIVSmokeHoleGeneratorComponent::Authority_CalculatePenetrationPoints(
	const FVector3f& Origin, const FVector3f& Direction, const float Radius, FVector3f& OutEntry, FVector3f& OutExit)
{
	FVector3f NormalizedDirection = Direction.GetSafeNormal();
	if (NormalizedDirection.IsNearlyZero())
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[CalculatePenetrationPoints] Direction is zero"));
		return false;
	}

	const float DistToCenter = FVector3f::Dist(Origin, FVector3f(GetComponentLocation()));
	const float DiagonalLength = GetScaledBoxExtent().Size() * 2.0f;
	const float MaxDistance = DistToCenter + DiagonalLength;

	const FVector3f RayEnd = Origin + NormalizedDirection * MaxDistance;

	FHitResult HitEntry, HitExit;
	FCollisionQueryParams QueryParams;
	QueryParams.bTraceComplex = false;

	// 1. Forward trace (Origin -> RayEnd) to find Entry point
	if (!LineTraceComponent(HitEntry, FVector(Origin), FVector(RayEnd), QueryParams))
	{
		return false;
	}

	// 2. Reverse trace (RayEnd -> Origin) to find Exit point
	if (!LineTraceComponent(HitExit, FVector(RayEnd), FVector(Origin), QueryParams))
	{
		OutExit = FVector3f(HitEntry.Location);
	}
	else
	{
		OutExit = FVector3f(HitExit.Location);
	}

	OutEntry = FVector3f(HitEntry.Location);

	// 3. Obstacle detection using SphereTrace between Entry and Exit
	if (ObstacleObjectTypes.Num() > 0)
	{
		FHitResult ObstacleHit;
		FCollisionQueryParams WorldParams;
		const FCollisionShape SweepShape = FCollisionShape::MakeSphere(Radius);
		const FCollisionObjectQueryParams ObjectParams(ObstacleObjectTypes);

		if (GetWorld()->SweepSingleByObjectType(
			ObstacleHit, FVector(OutEntry), FVector(OutExit), FQuat::Identity,
			ObjectParams, SweepShape, WorldParams))
		{
			OutExit = FVector3f(ObstacleHit.Location);
		}
	}

	return true;
}

void UIVSmokeHoleGeneratorComponent::Authority_UpdateDynamicSubjectList()
{
	const float CurrentTime = GetSyncedTime();
	const FBox3f SmokeVolume = FBox3f(Bounds.GetBox());

	for (int32 i = DynamicSubjectList.Num() - 1; i >= 0; --i)
	{
		FIVSmokeHoleDynamicSubject& DynamicSubject = DynamicSubjectList[i];

		// 0. Delete if object is not alive
		if (!DynamicSubject.TargetActor.IsValid())
		{
			DynamicSubjectList.RemoveAtSwap(i);
			continue;
		}

		const TObjectPtr<AActor> Actor = DynamicSubject.TargetActor.Get();
		const TObjectPtr<UIVSmokeHolePreset> Preset = UIVSmokeHolePreset::FindByID(DynamicSubject.PresetID);

		// 0. Delete if preset is not valid
		if (!Preset)
		{
			DynamicSubjectList.RemoveAtSwap(i);
			continue;
		}

		const FVector3f CurrentPos = FVector3f(Actor->GetActorLocation());
		const FVector3f LastPos = FVector3f(DynamicSubject.LastWorldPosition);

		if (!SmokeVolume.IsInside(CurrentPos))
		{
			continue;
		}

		// 1. Ignore if object moves a little bit
		if (Preset->DistanceThreshold > FVector3f::Dist(CurrentPos, LastPos))
		{
			continue;
		}

		// 2. Create Hole
		FIVSmokeHoleData HoleData;
		HoleData.Position = LastPos;
		HoleData.EndPosition = CurrentPos;
		HoleData.PresetID = DynamicSubject.PresetID;
		HoleData.ExpirationServerTime = CurrentTime + Preset->Duration;
		Authority_CreateHole(HoleData);

		// 3. Update registered object
		DynamicSubject.LastWorldPosition = CurrentPos;
		DynamicSubject.LastWorldRotation = Actor->GetActorQuat();
	}
}
#pragma endregion

//~============================================================================
// Local Only
#pragma region Local Only
#if !UE_SERVER
void UIVSmokeHoleGeneratorComponent::Local_InitializeHoleTexture()
{
	if (VoxelResolution.X <= 0 || VoxelResolution.Y <= 0 || VoxelResolution.Z <= 0)
	{
		return;
	}

	// Create UTextureRenderTargetVolume
	HoleTexture = NewObject<UTextureRenderTargetVolume>(this, TEXT("HoleTexture"));
	HoleTexture->Init(VoxelResolution.X, VoxelResolution.Y, VoxelResolution.Z, PF_FloatRGBA);
	HoleTexture->bCanCreateUAV = true;
	HoleTexture->ClearColor = FLinearColor::White;
	HoleTexture->SRGB = false;
	HoleTexture->UpdateResourceImmediate(true);
}

void UIVSmokeHoleGeneratorComponent::Local_RebuildHoleTexture()
{
	if (!HoleTexture)
	{
		return;
	}

	const FTextureRenderTargetResource* RenderTargetResource = HoleTexture->GameThread_GetRenderTargetResource();
	if (!RenderTargetResource)
	{
		return;
	}

	TArray<FIVSmokeHoleGPU> GPUHoles = ActiveHoles.GetHoleGPUData(GetSyncedTime());

	const TObjectPtr<AIVSmokeVoxelVolume> VoxelVolume = Cast<AIVSmokeVoxelVolume>(GetOwner());
	if (VoxelVolume == nullptr)
	{
		return;
	}

	const FVector3f WorldVolumeMin = FVector3f(VoxelVolume->GetVoxelWorldAABBMin());
	const FVector3f WorldVolumeMax = FVector3f(VoxelVolume->GetVoxelWorldAABBMax());

	const FIntVector Resolution = VoxelResolution;
	const int32 NumHoles = ActiveHoles.Num();
	FTextureRHIRef Texture = RenderTargetResource->GetRenderTargetTexture();

	ENQUEUE_RENDER_COMMAND(IVSmokeHoleCarveFullRebuild)(
		[Texture, GPUHoles = MoveTemp(GPUHoles), WorldVolumeMin, WorldVolumeMax, Resolution, NumHoles]
		(FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			const FRDGTextureRef RDGTexture = GraphBuilder.RegisterExternalTexture(
				CreateRenderTarget(Texture, TEXT("IVSmokeHoleTexture"))
			);

			const FRDGBufferRef HoleBuffer = CreateStructuredBuffer(
				GraphBuilder,
				TEXT("IVSmokeHoleBuffer"),
				sizeof(FIVSmokeHoleGPU),
				GPUHoles.Num(),
				GPUHoles.GetData(),
				sizeof(FIVSmokeHoleGPU) * GPUHoles.Num()
			);

			FIVSmokeHoleCarveCS::FParameters* Parameters = GraphBuilder.AllocParameters<FIVSmokeHoleCarveCS::FParameters>();
			Parameters->VolumeTexture = GraphBuilder.CreateUAV(RDGTexture);
			Parameters->HoleBuffer = GraphBuilder.CreateSRV(HoleBuffer);
			Parameters->VolumeMin = WorldVolumeMin;
			Parameters->VolumeMax = WorldVolumeMax;
			Parameters->Resolution = Resolution;
			Parameters->NumHoles = NumHoles;

			const TShaderMapRef<FIVSmokeHoleCarveCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
			FIVSmokePostProcessPass::AddComputeShaderPass<FIVSmokeHoleCarveCS>(GraphBuilder, GetGlobalShaderMap(GMaxRHIFeatureLevel), ComputeShader, Parameters, Resolution);
			GraphBuilder.Execute();
		}
	);
}
#endif
#pragma endregion

//~============================================================================
// Common
#pragma region Common
float UIVSmokeHoleGeneratorComponent::GetSyncedTime() const
{
	if (const TObjectPtr<UWorld> World = GetWorld())
	{
		if (const TObjectPtr<AGameStateBase> GameState = World->GetGameState())
		{
			return GameState->GetServerWorldTimeSeconds();
		}
		return World->GetTimeSeconds();
	}
	return 0.0f;
}

#if !UE_SERVER
void UIVSmokeHoleGeneratorComponent::SetBoxToVoxelAABB()
{
	if (const TObjectPtr<AIVSmokeVoxelVolume> VoxelVolume = Cast<AIVSmokeVoxelVolume>(GetOwner()))
	{
		const FVector WorldVoxelAABBMin = VoxelVolume->GetVoxelWorldAABBMin();
		const FVector WorldVoxelAABBMax = VoxelVolume->GetVoxelWorldAABBMax();
		const FVector Extent = (WorldVoxelAABBMax - WorldVoxelAABBMin) * 0.5f;
		const FVector WorldVoxelCenter = (WorldVoxelAABBMax + WorldVoxelAABBMin) * 0.5f;

		SetWorldLocation(WorldVoxelCenter);
		SetBoxExtent(Extent, false);
	}
}

FTextureRHIRef UIVSmokeHoleGeneratorComponent::GetHoleTextureRHI() const
{
	if (HoleTexture)
	{
		if (const FTextureRenderTargetResource* Resource = HoleTexture->GameThread_GetRenderTargetResource())
		{
			return Resource->GetRenderTargetTexture();
		}
	}

	return nullptr;
}
#endif

#pragma endregion
