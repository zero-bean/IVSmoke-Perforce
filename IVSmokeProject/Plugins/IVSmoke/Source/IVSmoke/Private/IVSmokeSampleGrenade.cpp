// Copyright Team SDB. All Rights Reserved.

#include "IVSmokeSampleGrenade.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "IVSmokeVoxelVolume.h"

AIVSmokeSampleGrenade::AIVSmokeSampleGrenade()
{
	PrimaryActorTick.bCanEverTick = false;

	// Create collision component as root
	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
	CollisionComponent->SetSphereRadius(CollisionRadius);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionComponent->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;

	// Create mesh component attached to collision
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(CollisionComponent);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Create projectile movement component
	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->InitialSpeed = InitialSpeed;
	ProjectileMovement->MaxSpeed = InitialSpeed;
	ProjectileMovement->bShouldBounce = true;
	ProjectileMovement->Bounciness = Bounciness;
	ProjectileMovement->Friction = Friction;
	ProjectileMovement->ProjectileGravityScale = GravityScale;

	RootComponent = CollisionComponent;
}

void AIVSmokeSampleGrenade::BeginPlay()
{
	Super::BeginPlay();

	// Apply configuration values to components
	if (CollisionComponent)
	{
		CollisionComponent->SetSphereRadius(CollisionRadius);

		// Ignore instigator to prevent self-collision
		if (APawn* InstigatorPawn = GetInstigator())
		{
			CollisionComponent->IgnoreActorWhenMoving(InstigatorPawn, true);
		}
	}

	if (ProjectileMovement)
	{
		ProjectileMovement->InitialSpeed = InitialSpeed;
		ProjectileMovement->MaxSpeed = InitialSpeed;
		ProjectileMovement->Bounciness = Bounciness;
		ProjectileMovement->Friction = Friction;
		ProjectileMovement->ProjectileGravityScale = GravityScale;
		ProjectileMovement->OnProjectileStop.AddDynamic(this, &AIVSmokeSampleGrenade::OnProjectileStopped);
	}
}

void AIVSmokeSampleGrenade::OnProjectileStopped(const FHitResult& HitResult)
{
	if (bHasDetonated)
	{
		return;
	}

	bHasDetonated = true;

	// Disable further collision
	if (CollisionComponent)
	{
		CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Spawn VoxelVolume at detonation location
	if (SmokeVoxelVolume)
	{
		GetWorld()->SpawnActor<AIVSmokeVoxelVolume>(
			SmokeVoxelVolume,
			GetActorLocation(),
			GetActorRotation()
		);
	}

	Destroy();
}
