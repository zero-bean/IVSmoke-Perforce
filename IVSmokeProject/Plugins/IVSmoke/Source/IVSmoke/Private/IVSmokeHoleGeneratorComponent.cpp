// Copyright SDB. All Rights Reserved.

#include "IVSmokeHoleGeneratorComponent.h"
#include "IVSmokeDebugRenderer.h"
#include "IVSmokeHoleCarveCS.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "GlobalShader.h"

#if ENABLE_DRAW_DEBUG
#include "DrawDebugHelpers.h"
#endif

UIVSmokeHoleGeneratorComponent::UIVSmokeHoleGeneratorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UIVSmokeHoleGeneratorComponent::BeginPlay()
{
	Super::BeginPlay();

	ActiveHoles.Reserve(MaxHoles);
	PendingHoles.Reserve(MaxHoles);
	PendingAABB.Init();
	bHasPendingWork = false;

	FIVSmokeDebugRenderer::Get().Register(this);
}

void UIVSmokeHoleGeneratorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FIVSmokeDebugRenderer::Get().Unregister(this);
	Super::EndPlay(EndPlayReason);
}

void UIVSmokeHoleGeneratorComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const double CurrentTime = GetWorld()->GetTimeSeconds();

	// 1. Accumulate expired holes (AABB updated incrementally)
	CleanupExpiredHoles(CurrentTime);

	// 2. Check batch interval
	TimeSinceLastUpdate += DeltaTime;

	const bool bNeedUpdate = bHasPendingWork || (ActiveHoles.Num() > 0);
	if (TimeSinceLastUpdate >= BatchIntervalSeconds && bNeedUpdate)
	{
		// 3. Merge pending holes into active first
		ActiveHoles.Append(PendingHoles);
		PendingHoles.Empty();

		// 4. Calculate ActiveHoles AABB
		FIntVector ActiveMin, ActiveMax;
		CalculateUpdateRegion(ActiveHoles, ActiveMin, ActiveMax);

		// 5. Union with PendingAABB if exists
		FIntVector RegionMin = ActiveMin;
		FIntVector RegionMax = ActiveMax;

		if (bHasPendingWork)
		{
			const FIntVector PendingMin = LocalToVoxel(PendingAABB.Min);
			const FIntVector PendingMax = LocalToVoxelCeil(PendingAABB.Max);

			RegionMin.X = FMath::Min(RegionMin.X, PendingMin.X);
			RegionMin.Y = FMath::Min(RegionMin.Y, PendingMin.Y);
			RegionMin.Z = FMath::Min(RegionMin.Z, PendingMin.Z);
			RegionMax.X = FMath::Max(RegionMax.X, PendingMax.X);
			RegionMax.Y = FMath::Max(RegionMax.Y, PendingMax.Y);
			RegionMax.Z = FMath::Max(RegionMax.Z, PendingMax.Z);
		}

		// 6. Single GPU dispatch for Union AABB
		DispatchBatchUpdate(RegionMin, RegionMax, CurrentTime);

		// 7. Reset pending state
		PendingAABB.Init();
		bHasPendingWork = false;
		TimeSinceLastUpdate = 0.0f;
	}

#if ENABLE_DRAW_DEBUG
	if (bShowVolumeDebug)
	{
		FIVSmokeDebugRenderer::Get().UpdateRenderData(this);
	}
#endif
}

// ============================================================================
// Public API
// ============================================================================

void UIVSmokeHoleGeneratorComponent::RequestPenetrationHole(const FIVSmokePenetrationRequest& Request)
{
	FVector EntryPoint, ExitPoint;
	if (!CalculatePenetrationPoints(Request, EntryPoint, ExitPoint))
	{
		return; // Ray does not intersect volume
	}

	FIVSmokeHoleData HoleData;
	HoleData.HoleType = IVSmokeHoleType::Penetration;
	HoleData.Position = EntryPoint;
	HoleData.EndPosition = ExitPoint;
	HoleData.Radius = Request.StartRadius;
	HoleData.EndRadius = Request.EndRadius;
	HoleData.InitialLifetime = Request.LifeTime;

	CreateHole(HoleData);
}

void UIVSmokeHoleGeneratorComponent::RequestExplosionHole(const FIVSmokeExplosionRequest& Request)
{
	// Check if origin is within or near the volume
	const FBox VolumeBox = Bounds.GetBox();
	const FVector ExpandedMin = VolumeBox.Min - FVector(Request.Radius);
	const FVector ExpandedMax = VolumeBox.Max + FVector(Request.Radius);

	if (const FBox ExpandedBox(ExpandedMin, ExpandedMax); ExpandedBox.IsInside(Request.Origin) == false)
	{
		return; // Explosion is too far from volume
	}

	FIVSmokeHoleData HoleData;
	HoleData.HoleType = IVSmokeHoleType::Explosion;
	HoleData.Position = Request.Origin;
	HoleData.Radius = Request.Radius;
	HoleData.EndPosition = Request.Origin;  // Not used for explosion
	HoleData.EndRadius = Request.Radius;    // Not used for explosion
	HoleData.InitialLifetime = Request.LifeTime;

	CreateHole(HoleData);
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

	VoxelResolution = FIntVector(64, 64, 64);
	InitializeHoleTexture();
}

// ============================================================================
// Internal: Hole Creation
// ============================================================================

void UIVSmokeHoleGeneratorComponent::CreateHole(const FIVSmokeHoleData& HoleData)
{
	if (ActiveHoles.Num() + PendingHoles.Num() >= MaxHoles)
	{
		// Remove oldest active hole to make room
		if (ActiveHoles.Num() > 0)
		{
			AccumulateHoleAABB(ActiveHoles[0]);
			ActiveHoles.RemoveAt(0);
		}
	}

	FIVSmokeHoleData NewHole = HoleData;
	NewHole.LocalCreationTime = GetWorld()->GetTimeSeconds();

	AccumulateHoleAABB(NewHole);
	PendingHoles.Add(NewHole);
}

// ============================================================================
// Internal: Raycast
// ============================================================================

bool UIVSmokeHoleGeneratorComponent::CalculatePenetrationPoints(
	const FIVSmokePenetrationRequest& Request, FVector& OutEntry, FVector& OutExit)
{
	const FVector Origin = Request.Origin;
	const FVector Direction = Request.Direction.GetSafeNormal();

	if (Direction.IsNearlyZero())
	{
		return false;
	}

	// Calculate max ray distance: distance to center + box diagonal
	const float DistToCenter = FVector::Dist(Origin, GetComponentLocation());
	const float DiagonalLength = GetScaledBoxExtent().Size() * 2.0f;
	const float MaxDistance = DistToCenter + DiagonalLength;

	const FVector RayEnd = Origin + Direction * MaxDistance;

	FHitResult HitEntry, HitExit;
	FCollisionQueryParams QueryParams;
	QueryParams.bTraceComplex = false;

	// Forward ray: Entry point
	if (!LineTraceComponent(HitEntry, Origin, RayEnd, QueryParams))
	{
		return false;
	}

	// Backward ray: Exit point
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

// ============================================================================
// Internal: Hole Management
// ============================================================================

void UIVSmokeHoleGeneratorComponent::CleanupExpiredHoles(double CurrentTime)
{
	const FTransform Transform = GetComponentTransform();

	// Single pass: accumulate AABB and remove expired holes
	for (int32 i = ActiveHoles.Num() - 1; i >= 0; --i)
	{
		if (ActiveHoles[i].IsExpired(CurrentTime))
		{
			AccumulateHoleAABB(ActiveHoles[i], Transform);
			ActiveHoles.RemoveAtSwap(i);
		}
	}
}

void UIVSmokeHoleGeneratorComponent::AccumulateHoleAABB(const FIVSmokeHoleData& Hole)
{
	AccumulateHoleAABB(Hole, GetComponentTransform());
}

void UIVSmokeHoleGeneratorComponent::AccumulateHoleAABB(const FIVSmokeHoleData& Hole, const FTransform& Transform)
{
	const FBox LocalAABB = Hole.CalculateLocalAABB(Transform);

	if (bHasPendingWork)
	{
		PendingAABB += LocalAABB;
	}
	else
	{
		PendingAABB = LocalAABB;
		bHasPendingWork = true;
	}
}

// ============================================================================
// Internal: Coordinate Conversion
// ============================================================================

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

FIntVector UIVSmokeHoleGeneratorComponent::WorldToVoxel(const FVector& WorldPos) const
{
	const FVector LocalPos = GetComponentTransform().InverseTransformPosition(WorldPos);
	return LocalToVoxel(LocalPos);
}

void UIVSmokeHoleGeneratorComponent::CalculateUpdateRegion(const TArray<FIVSmokeHoleData>& Holes,
	FIntVector& OutMin, FIntVector& OutMax) const
{
	if (Holes.Num() == 0)
	{
		OutMin = FIntVector::ZeroValue;
		OutMax = FIntVector::ZeroValue;
		return;
	}

	const FTransform Transform = GetComponentTransform();

	FBox UnionBox(ForceInit);
	for (const FIVSmokeHoleData& Hole : Holes)
	{
		UnionBox += Hole.CalculateLocalAABB(Transform);
	}

	OutMin = LocalToVoxel(UnionBox.Min);
	OutMax = LocalToVoxelCeil(UnionBox.Max);
}

// ============================================================================
// Internal: GPU Buffer Building
// ============================================================================

TArray<FIVSmokeHoleGPU> UIVSmokeHoleGeneratorComponent::BuildGPUHoleBuffer(
	const TArray<FIVSmokeHoleData>& Holes, double CurrentTime) const
{
	const FTransform Transform = GetComponentTransform();

	TArray<FIVSmokeHoleGPU> GPUBuffer;
	GPUBuffer.Reserve(FMath::Max(Holes.Num(), 1));

	for (const FIVSmokeHoleData& Hole : Holes)
	{
		FIVSmokeHoleGPU GPUHole;

		// Convert to local space
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

		// EdgeSoftness and DensityMultiplier from component settings
		GPUHole.EdgeSoftness = EdgeSoftness;
		GPUHole.DensityMultiplier = DensityMultiplier;
		GPUHole.NormalizedAge = Hole.GetNormalizedAge(CurrentTime);
		GPUHole.HoleType = Hole.HoleType;

		GPUBuffer.Add(GPUHole);
	}

	// Ensure at least one element for shader
	if (GPUBuffer.Num() == 0)
	{
		GPUBuffer.AddDefaulted(1);
	}

	return GPUBuffer;
}

// ============================================================================
// Hole Texture Management
// ============================================================================

void UIVSmokeHoleGeneratorComponent::InitializeHoleTexture()
{
	if (VoxelResolution.X <= 0 || VoxelResolution.Y <= 0 || VoxelResolution.Z <= 0)
	{
		return;
	}

	const int32 TotalVoxels = VoxelResolution.X * VoxelResolution.Y * VoxelResolution.Z;

	// R16F: Single channel density (1.0 = full smoke)
	TArray<FFloat16> InitialData;
	InitialData.SetNumUninitialized(TotalVoxels);
	for (int32 i = 0; i < TotalVoxels; ++i)
	{
		InitialData[i] = FFloat16(1.0f);  // Density
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

	FlushRenderingCommands();
}

// ============================================================================
// GPU Compute Shader Dispatch
// ============================================================================

void UIVSmokeHoleGeneratorComponent::DispatchBatchUpdate(
	const FIntVector& RegionMin, const FIntVector& RegionMax, double CurrentTime)
{
	if (!HoleTexture.IsValid())
	{
		return;
	}

	// Build GPU buffer from active holes only (pending already merged)
	TArray<FIVSmokeHoleGPU> GPUHoles = BuildGPUHoleBuffer(ActiveHoles, CurrentTime);

	const FVector3f VolumeMin = FVector3f(-GetUnscaledBoxExtent());
	const FVector3f VolumeMax = FVector3f(GetUnscaledBoxExtent());
	const FIntVector Resolution = VoxelResolution;
	const int32 NumHoles = ActiveHoles.Num();
	FTextureRHIRef Texture = HoleTexture;

	ENQUEUE_RENDER_COMMAND(IVSmokeHoleCarveBatch)(
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
			Parameters->bIsFullRebuild = 0;

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
				RDG_EVENT_NAME("IVSmokeHoleCarveCS_Batch"),
				ComputeShader,
				Parameters,
				GroupCount
			);

			GraphBuilder.Execute();
		}
	);
}
