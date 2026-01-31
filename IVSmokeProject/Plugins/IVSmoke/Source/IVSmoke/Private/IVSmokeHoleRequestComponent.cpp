// Copyright (c) 2026, Team SDB. All rights reserved.

#include "IVSmokeHoleRequestComponent.h"

#include "IVSmoke.h"
#include "IVSmokeHoleGeneratorComponent.h"
#include "IVSmokeHolePreset.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

UIVSmokeHoleRequestComponent::UIVSmokeHoleRequestComponent()
{
	SetIsReplicatedByDefault(true);
}

UIVSmokeHoleRequestComponent* UIVSmokeHoleRequestComponent::GetHoleRequester(const APawn* Instigator)
{
	if (!Instigator)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[UIVSmokeHoleRequestComponent::GetHoleRequester] Instigator is null"));
		return nullptr;
	}

	UIVSmokeHoleRequestComponent* Comp = Instigator->FindComponentByClass<UIVSmokeHoleRequestComponent>();
	if (Comp)
	{
		return Comp;
	}

	APlayerController* PC = Cast<APlayerController>(Instigator->GetController());
	if (PC)
	{
		Comp = PC->FindComponentByClass<UIVSmokeHoleRequestComponent>();
		if (Comp)
		{
			return Comp;
		}
	}

	UE_LOG(LogIVSmoke, Warning, TEXT("[UIVSmokeHoleRequestComponent::GetHoleRequester] No RequestComponent found on Pawn or PlayerController"));
	return nullptr;
}

void UIVSmokeHoleRequestComponent::RequestPenetrationHole_Implementation(UIVSmokeHoleGeneratorComponent* IVSmokeHoleGeneratorComponent, const FVector3f& Origin, const FVector3f& Direction, UIVSmokeHolePreset* Preset)
{
	if (!IVSmokeHoleGeneratorComponent)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[UIVSmokeHoleRequestComponent::RequestPenetrationHole] IVSmokeHoleGeneratorComponent is null"));
		return;
	}

	if (!Preset)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[UIVSmokeHoleRequestComponent::RequestPenetrationHole] Preset is null"));
		return;
	}

	if (Preset->HoleType != EIVSmokeHoleType::Penetration)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[UIVSmokeHoleRequestComponent::RequestPenetrationHole] Preset type mismatch"));
		return;
	}

	IVSmokeHoleGeneratorComponent->CreatePenetrationHole(Origin, Direction, Preset->GetPresetID());
}

void UIVSmokeHoleRequestComponent::RequestExplosionHole_Implementation(UIVSmokeHoleGeneratorComponent* IVSmokeHoleGeneratorComponent, const FVector3f& Origin, UIVSmokeHolePreset* Preset)
{
	if (!IVSmokeHoleGeneratorComponent)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[UIVSmokeHoleRequestComponent::RequestExplosionHole] IVSmokeHoleGeneratorComponent is null"));
		return;
	}

	if (!Preset)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[UIVSmokeHoleRequestComponent::RequestExplosionHole] Preset is null"));
		return;
	}

	if (Preset->HoleType != EIVSmokeHoleType::Explosion)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[UIVSmokeHoleRequestComponent::RequestExplosionHole] Preset type mismatch"));
		return;
	}

	IVSmokeHoleGeneratorComponent->CreateExplosionHole(Origin, Preset->GetPresetID());
}

void UIVSmokeHoleRequestComponent::RequestDynamicHole_Implementation(UIVSmokeHoleGeneratorComponent* IVSmokeHoleGeneratorComponent, AActor* TargetActor, UIVSmokeHolePreset* Preset)
{
	if (!IVSmokeHoleGeneratorComponent)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[UIVSmokeHoleRequestComponent::RequestDynamicHole] IVSmokeHoleGeneratorComponent is null"));
		return;
	}

	if (!Preset)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[UIVSmokeHoleRequestComponent::RequestDynamicHole] Preset is null"));
		return;
	}

	if (Preset->HoleType != EIVSmokeHoleType::Dynamic)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[UIVSmokeHoleRequestComponent::RequestDynamicHole] Preset type mismatch"));
		return;
	}

	IVSmokeHoleGeneratorComponent->RegisterTrackDynamicHole(TargetActor, Preset->GetPresetID());
}
