// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeVolumeComponent.h"
#include "IVSmokeRenderer.h"

UIVSmokeVolumeComponent::UIVSmokeVolumeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FBox UIVSmokeVolumeComponent::GetWorldBounds() const
{
	// TODO: Handle rotation properly (currently assumes axis-aligned)
	const FTransform& T = GetComponentTransform();
	return FBox(T.TransformPosition(BoundsMin), T.TransformPosition(BoundsMax));
}

void UIVSmokeVolumeComponent::OnRegister()
{
	Super::OnRegister();

	UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		FIVSmokeRenderer::Get().AddVolume(this);
	}
}

void UIVSmokeVolumeComponent::OnUnregister()
{
	FIVSmokeRenderer::Get().RemoveVolume(this);
	Super::OnUnregister();
}
