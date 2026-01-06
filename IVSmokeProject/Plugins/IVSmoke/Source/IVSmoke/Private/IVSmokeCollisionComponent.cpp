// Copyright SDB. All Rights Reserved.

#include "IVSmokeCollisionComponent.h"
#include "IVSmokeDebugSceneViewExtension.h"
#include "IVSmokeVolumeDebugRenderer.h"
#include "IVSmokeVolumeTextureBaker.h"
#include "IVSmokeVoxelVolumeTracer.h"

#if ENABLE_DRAW_DEBUG
#include "DrawDebugHelpers.h"
#endif

UIVSmokeCollisionComponent::UIVSmokeCollisionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Collision setup for projectile detection
	SetGenerateOverlapEvents(true);

	// Note: BoxExtent is set via InitBoxExtent in header or in editor
	// Cannot call SetBoxExtent() in constructor due to NewObject restriction
	InitBoxExtent(FVector(200.0, 200.0, 200.0));
}

void UIVSmokeCollisionComponent::BeginPlay()
{
	Super::BeginPlay();

	ActiveHoles.Reserve(MaxHoles);

	// Initialize voxel volume (BoxExtent * 2 = full volume size)
	VoxelCurator.Initialize(VoxelResolution, GetScaledBoxExtent() * 2.0f);

	// Initialize volume textures
	TextureBaker.Initialize(this, VoxelResolution);

	// Bind overlap event for projectile detection
	if (bAutoDetectProjectiles)
	{
		OnComponentBeginOverlap.AddDynamic(this, &UIVSmokeCollisionComponent::OnVolumeBeginOverlap);
	}

	// Register to debug renderer and initialize extension
	// todo: below code must migrate after another renderer class is unlocked by homing
	FIVSmokeVolumeDebugRenderer::Get().Register(this);
	FIVSmokeDebugSceneViewExtension::Initialize();
}

void UIVSmokeCollisionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unregister from debug renderer
	FIVSmokeVolumeDebugRenderer::Get().Unregister(this);

	Super::EndPlay(EndPlayReason);
}

void UIVSmokeCollisionComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const float CurrentTime = GetWorld()->GetTimeSeconds();

	CleanupExpiredHoles(CurrentTime);
	VoxelCurator.RemoveExpiredHoles(CurrentTime, HoleLifeTime);

	BakeTexturesIfDirty();

	// Update debug render data (game thread -> render thread)
	FIVSmokeVolumeDebugRenderer::Get().UpdateRenderData(this);

#if ENABLE_DRAW_DEBUG
	if (bDebugDrawVoxels)
	{
		DrawDebugVoxels();
	}
#endif
}

void UIVSmokeCollisionComponent::CreateHole(const FVector& Position, const FVector& Direction, const double Radius)
{
	// Remove oldest hole
	if (ActiveHoles.Num() >= MaxHoles)
	{
		ActiveHoles.RemoveAt(0);
	}

	const double CurrentTime = GetWorld()->GetTimeSeconds();
	ActiveHoles.Emplace(Position, Direction, Radius, CurrentTime);

	// Apply to voxel volume using cone shape
	// StartRadius (entry) is larger to cover snapping error
	// EndRadius (exit) is smaller for natural cone shape
	const float StartRadius = static_cast<float>(Radius);
	const float EndRadius = StartRadius * EndRadiusRatio;

	ApplyHoleToVoxelVolume(Position, Direction, StartRadius, EndRadius);
}

void UIVSmokeCollisionComponent::ApplyHoleToVoxelVolume(
	const FVector& WorldPosition,
	const FVector& Direction,
	float StartRadius,
	float EndRadius)
{
	if (!VoxelCurator.IsInitialized())
	{
		return;
	}

	// Convert world position to local (relative to volume center)
	const FVector StartLocalPos = WorldPosition - GetComponentLocation();

	// Calculate end position by tracing through volume
	const FVector VolumeExtent = VoxelCurator.GetVolumeExtent();
	const float MaxTraceDistance = VolumeExtent.Size();
	const FVector EndLocalPos = StartLocalPos + Direction.GetSafeNormal() * MaxTraceDistance;

	// Collect voxels within cone using SDF
	TArray<FIntVector> VoxelIndices;
	FIVSmokeVoxelVolumeTracer::CollectVoxelsInCone(
		StartLocalPos,
		EndLocalPos,
		StartRadius,
		EndRadius,
		VoxelCurator.GetVoxelSize(),
		VoxelCurator.GetResolution(),
		VoxelIndices
	);

	// Apply holes to voxel volume
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	VoxelCurator.ApplyHole(VoxelIndices, CurrentTime);
}

void UIVSmokeCollisionComponent::OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bAutoDetectProjectiles || !OtherActor)
	{
		return;
	}

	if (const FVector Velocity = OtherActor->GetVelocity(); Velocity.SizeSquared() > KINDA_SMALL_NUMBER)
	{
		const FVector HitPosition = bFromSweep ? FVector(SweepResult.Location) : OtherActor->GetActorLocation();
		CreateHole(HitPosition, Velocity.GetSafeNormal(), DefaultHoleRadius);
	}
}

void UIVSmokeCollisionComponent::CleanupExpiredHoles(double CurrentTime)
{
	ActiveHoles.RemoveAll([this, CurrentTime](const FIVSmokeHoleData& Hole)
	{
		return (CurrentTime - Hole.CreationTime) > HoleLifeTime;
	});
}

// ============================================================================
// Volume Texture Management
// ============================================================================

void UIVSmokeCollisionComponent::BakeTexturesIfDirty()
{
	if (VoxelCurator.IsTextureDirty())
	{
		TextureBaker.Bake(VoxelCurator);
	}
}

#if ENABLE_DRAW_DEBUG
void UIVSmokeCollisionComponent::DrawDebugVoxels() const
{
	const UWorld* World = GetWorld();
	if (!World || !VoxelCurator.IsInitialized())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DrawDebugVoxels] Early return: World=%d, Initialized=%d"),
			World != nullptr, VoxelCurator.IsInitialized());
		return;
	}

	const FIntVector Resolution = VoxelCurator.GetResolution();
	const FVector VoxelSize = VoxelCurator.GetVoxelSize();
	const FVector VoxelHalfSize = VoxelSize * 0.5f;
	const FVector ComponentLocation = GetComponentLocation();
	const float CurrentTime = World->GetTimeSeconds();

	// Iterate all voxels and draw boxes for holes
	for (int32 Z = 0; Z < Resolution.Z; ++Z)
	{
		for (int32 Y = 0; Y < Resolution.Y; ++Y)
		{
			for (int32 X = 0; X < Resolution.X; ++X)
			{
				const FIntVector VoxelIndex(X, Y, Z);

				if (VoxelCurator.HasHole(VoxelIndex))
				{
					// Check if hole is still within lifetime (GPU restoration simulation)
					const float CreationTime = VoxelCurator.GetCreationTime(VoxelIndex);
					const float Age = CurrentTime - CreationTime;
					if (Age > HoleLifeTime)
					{
						continue;
					}

					// Calculate world position of voxel center
					const FVector LocalPos = VoxelCurator.VoxelToLocal(VoxelIndex);
					const FVector WorldPos = ComponentLocation + LocalPos;

					// Color based on remaining lifetime (newer = more red, older = more green)
					const float LifeRatio = FMath::Clamp(1.0f - (Age / static_cast<float>(HoleLifeTime)), 0.0f, 1.0f);
					const FColor DebugColor = FColor::MakeRedToGreenColorFromScalar(LifeRatio);

					DrawDebugBox(World, WorldPos, VoxelHalfSize, DebugColor, false, -1.0f, 0, 3.0f);
				}
			}
		}
	}

	// Draw volume box outline
	DrawDebugBox(World, ComponentLocation, GetScaledBoxExtent(), FColor::Cyan, false, -1.0f, 0, 2.0f);
}
#endif
