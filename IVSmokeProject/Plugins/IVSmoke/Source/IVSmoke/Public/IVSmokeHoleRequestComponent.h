// Copyright (c) 2026, Team SDB. All rights reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "IVSmokeHoleRequestComponent.generated.h"

class UIVSmokeHoleGeneratorComponent;
class UIVSmokeHolePreset;

/**
 * @brief Handles network routing for hole requests.
 *        This component enables clients to request holes on VoxelVolumes
 *        by routing Server RPCs through the PlayerController's connection.
 */
UCLASS(ClassGroup = (IVSmoke), meta = (BlueprintSpawnableComponent))
class IVSMOKE_API UIVSmokeHoleRequestComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIVSmokeHoleRequestComponent();

	/** Find RequestComponent on Instigator's Pawn or PlayerController. */
	UFUNCTION(BlueprintCallable, Category = "IVSmoke | Hole | API")
	static UIVSmokeHoleRequestComponent* GetHoleRequester(const APawn* Instigator);

	/** Request a penetration hole. Always executed on server. */
	UFUNCTION(BlueprintCallable, Server, Reliable, Category = "IVSmoke | Hole | API")
	void RequestPenetrationHole(UIVSmokeHoleGeneratorComponent* IVSmokeHoleGeneratorComponent, const FVector3f& Origin, const FVector3f& Direction, UIVSmokeHolePreset* Preset);

	/** Request an explosion hole. Always executed on server. */
	UFUNCTION(BlueprintCallable, Server, Reliable, Category = "IVSmoke | Hole | API")
	void RequestExplosionHole(UIVSmokeHoleGeneratorComponent* IVSmokeHoleGeneratorComponent, const FVector3f& Origin, UIVSmokeHolePreset* Preset);

	/** Request a dynamic hole. Always executed on server. */
	UFUNCTION(BlueprintCallable, Server, Reliable, Category = "IVSmoke | Hole | API")
	void RequestDynamicHole(UIVSmokeHoleGeneratorComponent* IVSmokeHoleGeneratorComponent, AActor* TargetActor, UIVSmokeHolePreset* Preset);
};
