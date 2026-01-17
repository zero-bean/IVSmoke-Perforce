// Copyright SDB. All Rights Reserved.

#include "IVSmokeHoleGeneratorComponent.h"
#include "IVSmokeDebugRenderer.h"
#include "IVSmokeHoleCarveCS.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "GlobalShader.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "IVSmokeVoxelVolume.h"

#if ENABLE_DRAW_DEBUG
#include "DrawDebugHelpers.h"
#endif

UIVSmokeHoleGeneratorComponent::UIVSmokeHoleGeneratorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UIVSmokeHoleGeneratorComponent::BeginPlay()
{
	Super::BeginPlay();

	ActiveHoles.Reserve(MaxHoles);

	// todo: FIVSmokeDebugRender must be deleted soon ! (PYB, 260116)
#if !UE_SERVER
	FIVSmokeDebugRenderer::Get().Register(this);
	InitializeHoleTexture();
#endif

}

void UIVSmokeHoleGeneratorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// todo: FIVSmokeDebugRender must be deleted soon ! (PYB, 260116)
#if !UE_SERVER
	FIVSmokeDebugRenderer::Get().Unregister(this);
#endif

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
#if !UE_SERVER
	SetBoxToVoxelAABB();
#endif
	// Authority: Cleanup expired holes
	if (GetOwner()->HasAuthority())
	{
		Authority_CleanupExpiredHoles();
	}

	// Client / Standalone: Rebuild texture if holes exist
#if !UE_SERVER
	if (ActiveHoles.Num() > 0)
	{
		Local_RebuildHoleTexture();
	}
#endif

#if ENABLE_DRAW_DEBUG
	if (bShowVolumeDebug)
	{
		FIVSmokeDebugRenderer::Get().UpdateRenderData(this);
	}
#endif
}

// ============================================================================
// Public API (Server RPC Implementation)
// ============================================================================

void UIVSmokeHoleGeneratorComponent::RequestPenetrationHole_Implementation(const FIVSmokePenetrationRequest& Request)
{
	FVector EntryPoint, ExitPoint;
	if (!CalculatePenetrationPoints(Request, EntryPoint, ExitPoint))
	{
		return;
	}

	FIVSmokeHoleData HoleData;
	HoleData.HoleType = IVSmokeHoleType::Penetration;
	HoleData.Position = EntryPoint;
	HoleData.EndPosition = ExitPoint;
	HoleData.Radius = Request.StartRadius;
	HoleData.EndRadius = Request.EndRadius;
	HoleData.InitialLifetime = Request.LifeTime;

	Authority_CreateHole(HoleData);
}

void UIVSmokeHoleGeneratorComponent::RequestExplosionHole_Implementation(const FIVSmokeExplosionRequest& Request)
{
	const FBox VolumeBox = Bounds.GetBox();
	const FVector ExpandedMin = VolumeBox.Min - FVector(Request.Radius);
	const FVector ExpandedMax = VolumeBox.Max + FVector(Request.Radius);

	if (const FBox ExpandedBox(ExpandedMin, ExpandedMax); !ExpandedBox.IsInside(Request.Origin))
	{
		return;
	}

	FIVSmokeHoleData HoleData;
	HoleData.HoleType = IVSmokeHoleType::Explosion;
	HoleData.Position = Request.Origin;
	HoleData.EndPosition = Request.Origin;
	HoleData.Radius = Request.Radius;
	HoleData.EndRadius = Request.Radius;
	HoleData.InitialLifetime = Request.LifeTime;

	Authority_CreateHole(HoleData);
}


// ============================================================================
// Authority Only
// ============================================================================

void UIVSmokeHoleGeneratorComponent::Authority_CreateHole(const FIVSmokeHoleData& InHoleData)
{
	if (ActiveHoles.Num() >= MaxHoles)
	{
		ActiveHoles.RemoveAt(0);
	}

	FIVSmokeHoleData HoleData = InHoleData;
	HoleData.ExpirationServerTime = GetSyncedTime() + HoleData.InitialLifetime;

	ActiveHoles.Add(HoleData);
}

void UIVSmokeHoleGeneratorComponent::Authority_CleanupExpiredHoles()
{
	const float CurrentServerTime = GetSyncedTime();

	for (int32 i = ActiveHoles.Num() - 1; i >= 0; --i)
	{
		if (ActiveHoles[i].IsExpired(CurrentServerTime))
		{
			ActiveHoles.RemoveAtSwap(i);
		}
	}
}

// ============================================================================
// Local Only
// ============================================================================

#if !UE_SERVER
void UIVSmokeHoleGeneratorComponent::Local_RebuildHoleTexture()
{
	if (!HoleTexture.IsValid())
	{
		return;
	}

	TArray<FIVSmokeHoleGPU> GPUHoles = BuildGPUHoleBuffer();


	AIVSmokeVoxelVolume* VoxelVolume = Cast<AIVSmokeVoxelVolume>(GetOwner());
	if (VoxelVolume == nullptr)
	{
		return;
	}

	const FVector3f WorldlVolumeMin = FVector3f(VoxelVolume->GetVoxelWorldAABBMin());
	const FVector3f WorldlVolumeMax = FVector3f(VoxelVolume->GetVoxelWorldAABBMax());

	//UE_LOG(LogTemp, Display, TEXT("Min : %s, Max : %s"), *WorldlVolumeMin.ToString(), *WorldlVolumeMax.ToString());
	//UE_LOG(LogTemp, Display, TEXT("HoleCount : %d"), GPUHoles.Num());
	//for (FIVSmokeHoleGPU& Hole : GPUHoles)
	//{
	//	UE_LOG(LogTemp, Display, TEXT("pos : %s, endpos : %s"), *Hole.Position.ToString(), *Hole.EndPosition.ToString());
	//}

	const FIntVector Resolution = VoxelResolution;
	const int32 NumHoles = ActiveHoles.Num();
	FTextureRHIRef Texture = HoleTexture;

	// Full rebuild: entire volume
	const FIntVector RegionMin = FIntVector::ZeroValue;
	const FIntVector RegionMax = VoxelResolution - FIntVector(1, 1, 1);

	ENQUEUE_RENDER_COMMAND(IVSmokeHoleCarveFullRebuild)(
		[Texture, GPUHoles = MoveTemp(GPUHoles), WorldlVolumeMin, WorldlVolumeMax, Resolution,
		 RegionMin, RegionMax, NumHoles](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FRDGTextureRef RDGTexture = GraphBuilder.RegisterExternalTexture(
				CreateRenderTarget(Texture, TEXT("IVSmokeHoleTexture"))
			);

			FRDGBufferRef HoleBuffer = CreateStructuredBuffer(
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
			Parameters->VolumeMin = WorldlVolumeMin;
			Parameters->VolumeMax = WorldlVolumeMax;
			Parameters->Resolution = Resolution;
			Parameters->UpdateRegionMin = RegionMin;
			Parameters->UpdateRegionMax = RegionMax;
			Parameters->NumHoles = NumHoles;
			Parameters->bIsFullRebuild = 1;

			TShaderMapRef<FIVSmokeHoleCarveCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			const FIntVector DispatchSize = RegionMax - RegionMin + FIntVector(1, 1, 1);
			const uint32 ThreadGroupSize = FIVSmokeHoleCarveCS::ThreadGroupSize;
			FIntVector GroupCount(
				FMath::DivideAndRoundUp(DispatchSize.X, static_cast<int32>(ThreadGroupSize)),
				FMath::DivideAndRoundUp(DispatchSize.Y, static_cast<int32>(ThreadGroupSize)),
				FMath::DivideAndRoundUp(DispatchSize.Z, static_cast<int32>(ThreadGroupSize))
			);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("IVSmokeHoleCarveCS_FullRebuild"),
				ComputeShader,
				Parameters,
				GroupCount
			);

			GraphBuilder.Execute();
		}
	);
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
	const FIVSmokePenetrationRequest& Request, FVector& OutEntry, FVector& OutExit)
{
	const FVector Origin = Request.Origin;
	const FVector Direction = Request.Direction.GetSafeNormal();

	if (Direction.IsNearlyZero())
	{
		UE_LOG(LogTemp, Warning, TEXT("[CalculatePenetrationPoints] Direction is zero"));
		return false;
	}

	const float DistToCenter = FVector::Dist(Origin, GetComponentLocation());
	const float DiagonalLength = GetScaledBoxExtent().Size() * 2.0f;
	const float MaxDistance = DistToCenter + DiagonalLength;

	const FVector RayEnd = Origin + Direction * MaxDistance;

	FHitResult HitEntry, HitExit;
	FCollisionQueryParams QueryParams;
	QueryParams.bTraceComplex = false;

	if (!LineTraceComponent(HitEntry, Origin, RayEnd, QueryParams))
	{
		return false;
	}

	if (!LineTraceComponent(HitExit, RayEnd, Origin, QueryParams))
	{
		OutExit = HitEntry.Location;
	}
	else
	{
		OutExit = HitExit.Location;
	}

	OutEntry = HitEntry.Location;
	return true;
}

#if !UE_SERVER
void UIVSmokeHoleGeneratorComponent::InitializeHoleTexture()
{
	if (VoxelResolution.X <= 0 || VoxelResolution.Y <= 0 || VoxelResolution.Z <= 0)
	{
		return;
	}

	const int32 TotalVoxels = VoxelResolution.X * VoxelResolution.Y * VoxelResolution.Z;

	TArray<FFloat16> InitialData;
	InitialData.SetNumUninitialized(TotalVoxels);
	for (int32 i = 0; i < TotalVoxels; ++i)
	{
		InitialData[i] = FFloat16(1.0f);
	}

	FTextureRHIRef* TexturePtr = &HoleTexture;
	FIntVector Resolution = VoxelResolution;

	ENQUEUE_RENDER_COMMAND(CreateHoleTexture)(
		[TexturePtr, Resolution, InitialData = MoveTemp(InitialData)](FRHICommandListImmediate& RHICmdList)
		{
			const FRHITextureCreateDesc Desc =
				FRHITextureCreateDesc::Create3D(TEXT("IVSmokeHoleTexture"), Resolution.X, Resolution.Y, Resolution.Z, PF_R16F)
				.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::UAV)
				.SetInitialState(ERHIAccess::CopyDest);

			*TexturePtr = RHICreateTexture(Desc);

			if (TexturePtr->IsValid())
			{
				FUpdateTextureRegion3D Region(0, 0, 0, 0, 0, 0, Resolution.X, Resolution.Y, Resolution.Z);
				const uint32 SourceRowPitch = Resolution.X * sizeof(FFloat16);
				const uint32 SourceDepthPitch = Resolution.X * Resolution.Y * sizeof(FFloat16);

				RHIUpdateTexture3D(*TexturePtr, 0, Region, SourceRowPitch, SourceDepthPitch,
					reinterpret_cast<const uint8*>(InitialData.GetData()));
				RHICmdList.Transition(FRHITransitionInfo(*TexturePtr, ERHIAccess::CopyDest, ERHIAccess::SRVGraphics));
			}
		}
	);
}
#endif


#if !UE_SERVER
TArray<FIVSmokeHoleGPU> UIVSmokeHoleGeneratorComponent::BuildGPUHoleBuffer() const
{
	const FTransform Transform = GetComponentTransform();
	const float CurrentServerTime = GetSyncedTime();

	TArray<FIVSmokeHoleGPU> GPUBuffer;
	GPUBuffer.Reserve(FMath::Max(ActiveHoles.Num(), 1));

	for (const FIVSmokeHoleData& Hole : ActiveHoles)
	{
		FIVSmokeHoleGPU GPUHole;

		GPUHole.Position = FVector3f(Hole.Position);
		GPUHole.Radius = Hole.Radius;

		if (Hole.HoleType == IVSmokeHoleType::Penetration)
		{
			GPUHole.EndPosition = FVector3f(Hole.EndPosition);
			GPUHole.EndRadius = Hole.EndRadius;
		}
		else
		{
			GPUHole.EndPosition = GPUHole.Position;
			GPUHole.EndRadius = Hole.Radius;
		}

		GPUHole.EdgeSoftness = EdgeSoftness;
		GPUHole.DensityMultiplier = DensityMultiplier;
		GPUHole.NormalizedAge = Hole.GetNormalizedAge(CurrentServerTime);
		GPUHole.HoleType = Hole.HoleType;

		GPUBuffer.Add(GPUHole);
	}

	if (GPUBuffer.Num() == 0)
	{
		GPUBuffer.AddDefaulted(1);
	}

	return GPUBuffer;
}
#endif
#if !UE_SERVER
void UIVSmokeHoleGeneratorComponent::SetBoxToVoxelAABB()
{
	AIVSmokeVoxelVolume* VoxelVolume = Cast<AIVSmokeVoxelVolume>(GetOwner());
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
