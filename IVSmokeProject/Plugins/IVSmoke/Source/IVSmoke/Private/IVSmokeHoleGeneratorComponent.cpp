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

	FIVSmokeDebugRenderer::Get().Register(this);
}

void UIVSmokeHoleGeneratorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FIVSmokeDebugRenderer::Get().Unregister(this);
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

	// Client / Standalone: Rebuild texture if holes exist
	if (GetNetMode() != NM_DedicatedServer && ActiveHoles.Num() > 0)
	{
		Local_RebuildHoleTexture();
	}

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

void UIVSmokeHoleGeneratorComponent::SyncWithVoxelVolume(FIntVector VolumeExtent, float InVoxelSize)
{
	FIntVector GridRes;
	GridRes.X = (VolumeExtent.X * 2) - 1;
	GridRes.Y = (VolumeExtent.Y * 2) - 1;
	GridRes.Z = (VolumeExtent.Z * 2) - 1;

	GridRes.X = FMath::Max(1, GridRes.X);
	GridRes.Y = FMath::Max(1, GridRes.Y);
	GridRes.Z = FMath::Max(1, GridRes.Z);

	FVector HalfExtent = FVector(GridRes) * InVoxelSize * 0.5f;
	SetBoxExtent(HalfExtent);

	VoxelResolution = FIntVector(128, 128, 128);
	InitializeHoleTexture();
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

void UIVSmokeHoleGeneratorComponent::Local_RebuildHoleTexture()
{
	if (!HoleTexture.IsValid())
	{
		return;
	}

	TArray<FIVSmokeHoleGPU> GPUHoles = BuildGPUHoleBuffer();

	const FVector3f VolumeMin = FVector3f(-GetUnscaledBoxExtent());
	const FVector3f VolumeMax = FVector3f(GetUnscaledBoxExtent());
	const FIntVector Resolution = VoxelResolution;
	const int32 NumHoles = ActiveHoles.Num();
	FTextureRHIRef Texture = HoleTexture;

	// Full rebuild: entire volume
	const FIntVector RegionMin = FIntVector::ZeroValue;
	const FIntVector RegionMax = VoxelResolution - FIntVector(1, 1, 1);

	ENQUEUE_RENDER_COMMAND(IVSmokeHoleCarveFullRebuild)(
		[Texture, GPUHoles = MoveTemp(GPUHoles), VolumeMin, VolumeMax, Resolution,
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
			Parameters->VolumeMin = VolumeMin;
			Parameters->VolumeMax = VolumeMax;
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

FIntVector UIVSmokeHoleGeneratorComponent::LocalToVoxel(const FVector& LocalPos) const
{
	const FVector VolumeExtent = GetUnscaledBoxExtent();
	const FVector Normalized = (LocalPos + VolumeExtent) / (VolumeExtent * 2.0);

	return FIntVector(
		FMath::Clamp(FMath::FloorToInt(Normalized.X * VoxelResolution.X), 0, VoxelResolution.X - 1),
		FMath::Clamp(FMath::FloorToInt(Normalized.Y * VoxelResolution.Y), 0, VoxelResolution.Y - 1),
		FMath::Clamp(FMath::FloorToInt(Normalized.Z * VoxelResolution.Z), 0, VoxelResolution.Z - 1)
	);
}

FIntVector UIVSmokeHoleGeneratorComponent::LocalToVoxelCeil(const FVector& LocalPos) const
{
	const FVector VolumeExtent = GetUnscaledBoxExtent();
	const FVector Normalized = (LocalPos + VolumeExtent) / (VolumeExtent * 2.0);

	return FIntVector(
		FMath::Clamp(FMath::CeilToInt(Normalized.X * VoxelResolution.X), 0, VoxelResolution.X - 1),
		FMath::Clamp(FMath::CeilToInt(Normalized.Y * VoxelResolution.Y), 0, VoxelResolution.Y - 1),
		FMath::Clamp(FMath::CeilToInt(Normalized.Z * VoxelResolution.Z), 0, VoxelResolution.Z - 1)
	);
}

TArray<FIVSmokeHoleGPU> UIVSmokeHoleGeneratorComponent::BuildGPUHoleBuffer() const
{
	const FTransform Transform = GetComponentTransform();
	const float CurrentServerTime = GetSyncedTime();

	TArray<FIVSmokeHoleGPU> GPUBuffer;
	GPUBuffer.Reserve(FMath::Max(ActiveHoles.Num(), 1));

	for (const FIVSmokeHoleData& Hole : ActiveHoles)
	{
		FIVSmokeHoleGPU GPUHole;

		GPUHole.Position = FVector3f(Transform.InverseTransformPosition(Hole.Position));
		GPUHole.Radius = Hole.Radius;

		if (Hole.HoleType == IVSmokeHoleType::Penetration)
		{
			GPUHole.EndPosition = FVector3f(Transform.InverseTransformPosition(Hole.EndPosition));
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
