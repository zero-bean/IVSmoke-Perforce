// Copyright SDB. All Rights Reserved.

#include "IVSmokeHoleGeneratorComponent.h"
#include "IVSmokeHolePreset.h"
#include "IVSmokeHoleCarveCS.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "GlobalShader.h"
#include "IVSmoke.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "IVSmokeVoxelVolume.h"
#include "IVSmokePostProcessPass.h"

UIVSmokeHoleGeneratorComponent::UIVSmokeHoleGeneratorComponent()
	: bHoleTextureDirty(false)
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UIVSmokeHoleGeneratorComponent::BeginPlay()
{
	Super::BeginPlay();

	// Setup Fast TArray owner for replication callbacks
	ActiveHoles.OwnerComponent = this;
	ActiveHoles.Reserve(MaxHoles);

	// Join process
	if (ActiveHoles.Num() > 0)
	{
		bHoleTextureDirty = true;
	}

#if !UE_SERVER
	InitializeHoleTexture();
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

void UIVSmokeHoleGeneratorComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Authority: Cleanup expired holes
	if (GetOwner()->HasAuthority())
	{
		Authority_CleanupExpiredHoles();
	}

	// Join process
	if (ActiveHoles.Num() > 0)
	{
		bHoleTextureDirty = true;
	}

	// Client / Standalone: Rebuild texture if dirty
#if !UE_SERVER
	SetBoxToVoxelAABB();
	if (bHoleTextureDirty && ActiveHoles.Num() > 0)
	{
		Local_RebuildHoleTexture();
		bHoleTextureDirty = false;
	}
#endif
}

// ============================================================================
// Public API (Blueprint & C++)
// ============================================================================

void UIVSmokeHoleGeneratorComponent::RequestPenetrationHole(FVector Origin, FVector Direction, UIVSmokeHolePreset* Preset)
{
	if (!Preset)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[RequestPenetrationHole] Preset is null"));
		return;
	}

	Internal_RequestPenetrationHole(Origin, Direction, Preset->GetPresetID());
}

void UIVSmokeHoleGeneratorComponent::RequestExplosionHole(FVector Origin, UIVSmokeHolePreset* Preset)
{
	if (!Preset)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[RequestExplosionHole] Preset is null"));
		return;
	}

	Internal_RequestExplosionHole(Origin, Preset->GetPresetID());
}

// ============================================================================
// Internal Server RPC Implementation
// ============================================================================

void UIVSmokeHoleGeneratorComponent::Internal_RequestPenetrationHole_Implementation(FVector Origin, FVector Direction, uint8 PresetID)
{
	const TObjectPtr<UIVSmokeHolePreset> Preset = UIVSmokeHolePreset::FindByID(PresetID);
	if (!Preset)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[Internal_RequestPenetrationHole] Invalid PresetID: %d"), PresetID);
		return;
	}

	if (Preset->Lifetime <= 0.0f)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[Internal_RequestPenetrationHole] Invalid Lifetime: %f"), Preset->Lifetime);
		return;
	}

	FVector EntryPoint, ExitPoint;
	if (!CalculatePenetrationPoints(Origin, Direction, Preset->EndRadius, EntryPoint, ExitPoint))
	{
		return;
	}

	FIVSmokeHoleData HoleData;
	HoleData.Position = EntryPoint;
	HoleData.EndPosition = ExitPoint;
	HoleData.PresetID = PresetID;
	HoleData.ExpirationServerTime = GetSyncedTime() + Preset->Lifetime;

	Authority_CreateHole(HoleData);
}

void UIVSmokeHoleGeneratorComponent::Internal_RequestExplosionHole_Implementation(FVector Origin, uint8 PresetID)
{
	TObjectPtr<UIVSmokeHolePreset> Preset = UIVSmokeHolePreset::FindByID(PresetID);
	if (!Preset)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[Internal_RequestExplosionHole] Invalid PresetID: %d"), PresetID);
		return;
	}

	if (Preset->Lifetime <= 0.0f)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[Internal_RequestExplosionHole] Invalid Lifetime: %f"), Preset->Lifetime);
		return;
	}

	const FBox VolumeBox = Bounds.GetBox();
	const FVector ExpandedMin = VolumeBox.Min - FVector(Preset->StartRadius);
	const FVector ExpandedMax = VolumeBox.Max + FVector(Preset->StartRadius);

	if (const FBox ExpandedBox(ExpandedMin, ExpandedMax); !ExpandedBox.IsInside(Origin))
	{
		return;
	}

	FIVSmokeHoleData HoleData;
	HoleData.Position = Origin;
	HoleData.EndPosition = Origin;
	HoleData.PresetID = PresetID;
	HoleData.ExpirationServerTime = GetSyncedTime() + Preset->Lifetime;

	Authority_CreateHole(HoleData);
}


// ============================================================================
// Authority Only
// ============================================================================

void UIVSmokeHoleGeneratorComponent::Authority_CreateHole(const FIVSmokeHoleData& InHoleData)
{
	if (ActiveHoles.Num() >= MaxHoles)
	{
		ActiveHoles.RemoveAtSwap(0);
	}

	ActiveHoles.AddHole(InHoleData);

	// Server/Standalone also needs texture update
	bHoleTextureDirty = true;
}

void UIVSmokeHoleGeneratorComponent::Authority_CleanupExpiredHoles()
{
	const float CurrentServerTime = GetSyncedTime();

	for (int32 i = ActiveHoles.Num() - 1; i >= 0; --i)
	{
		if (ActiveHoles[i].IsExpired(CurrentServerTime))
		{
			ActiveHoles.RemoveAtSwap(i);
			bHoleTextureDirty = true;
		}
	}
}

// ============================================================================
// Local Only
// ============================================================================

#if !UE_SERVER
void UIVSmokeHoleGeneratorComponent::Local_RebuildHoleTexture()
{
	const FTextureRenderTargetResource* RenderTargetResource =
		HoleTexture ? HoleTexture->GameThread_GetRenderTargetResource() : nullptr;
	if (!RenderTargetResource)
	{
		return;
	}

	TArray<FIVSmokeHoleGPU> GPUHoles = BuildGPUHoleBuffer();

	const AIVSmokeVoxelVolume* VoxelVolume = Cast<AIVSmokeVoxelVolume>(GetOwner());
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

// ============================================================================
// Texture Access
// ============================================================================

#if !UE_SERVER
FTextureRHIRef UIVSmokeHoleGeneratorComponent::GetHoleTextureRHI() const
{
	if (HoleTexture)
	{
		// Use GameThread-safe accessor
		if (const FTextureRenderTargetResource* Resource = HoleTexture->GameThread_GetRenderTargetResource())
		{
			return Resource->GetRenderTargetTexture();
		}
	}

	return nullptr;
}
#endif

// ============================================================================
// Helper
// ============================================================================

float UIVSmokeHoleGeneratorComponent::GetSyncedTime() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			return GameState->GetServerWorldTimeSeconds();
		}
		return World->GetTimeSeconds();
	}
	return 0.0f;
}

bool UIVSmokeHoleGeneratorComponent::CalculatePenetrationPoints(
	FVector Origin, FVector Direction, float EndRadius, FVector& OutEntry, FVector& OutExit)
{
	Direction = Direction.GetSafeNormal();

	// 0. Edge Case
	if (Direction.IsNearlyZero())
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[CalculatePenetrationPoints] Direction is zero"));
		return false;
	}

	const float DistToCenter = FVector::Dist(Origin, GetComponentLocation());
	const float DiagonalLength = GetScaledBoxExtent().Size() * 2.0f;
	const float MaxDistance = DistToCenter + DiagonalLength;

	const FVector RayEnd = Origin + Direction * MaxDistance;

	FHitResult HitEntry, HitExit;
	FCollisionQueryParams QueryParams;
	QueryParams.bTraceComplex = false;

	// 1. Forward trace (Origin -> RayEnd) to find Entry point
	if (!LineTraceComponent(HitEntry, Origin, RayEnd, QueryParams))
	{
		return false;
	}

	// 2. Reverse trace (RayEnd -> Origin) to find Exit point
	if (!LineTraceComponent(HitExit, RayEnd, Origin, QueryParams))
	{
		OutExit = HitEntry.Location;
	}
	else
	{
		OutExit = HitExit.Location;
	}

	OutEntry = HitEntry.Location;

	// 3. Obstacle detection using SphereTrace between Entry and Exit
	if (ObstacleObjectTypes.Num() > 0)
	{
		FHitResult ObstacleHit;
		FCollisionQueryParams WorldParams;
		const FCollisionShape SweepShape = FCollisionShape::MakeSphere(EndRadius);
		const FCollisionObjectQueryParams ObjectParams(ObstacleObjectTypes);

		if (GetWorld()->SweepSingleByObjectType(
			ObstacleHit, OutEntry, OutExit, FQuat::Identity,
			ObjectParams, SweepShape, WorldParams))
		{
			OutExit = ObstacleHit.Location;
		}
	}

	return true;
}

#if !UE_SERVER
void UIVSmokeHoleGeneratorComponent::InitializeHoleTexture()
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
#endif

#if !UE_SERVER
TArray<FIVSmokeHoleGPU> UIVSmokeHoleGeneratorComponent::BuildGPUHoleBuffer() const
{
	const float CurrentServerTime = GetSyncedTime();
	return ActiveHoles.GetHoleGPUDatas(CurrentServerTime);
}
#endif

#if !UE_SERVER
void UIVSmokeHoleGeneratorComponent::SetBoxToVoxelAABB()
{
	const AIVSmokeVoxelVolume* VoxelVolume = Cast<AIVSmokeVoxelVolume>(GetOwner());
	if (VoxelVolume == nullptr)
	{
		return;
	}
	const FVector WorldVoxelAABBMin = VoxelVolume->GetVoxelWorldAABBMin();
	const FVector WorldVoxelAABBMax = VoxelVolume->GetVoxelWorldAABBMax();
	const FVector Extent = (WorldVoxelAABBMax - WorldVoxelAABBMin) * 0.5f;
	const FVector WorldVoxelCenter = (WorldVoxelAABBMax + WorldVoxelAABBMin) * 0.5f;

	SetWorldLocation(WorldVoxelCenter);
	SetBoxExtent(Extent, false);
}
#endif
