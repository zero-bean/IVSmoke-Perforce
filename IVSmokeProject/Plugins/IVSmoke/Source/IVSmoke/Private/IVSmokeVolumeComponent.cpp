// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeVolumeComponent.h"

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
	// Note: Renderer registration moved to AIVSmokeVoxelVolume
}

void UIVSmokeVolumeComponent::OnUnregister()
{
	Super::OnUnregister();
	// Note: Renderer registration moved to AIVSmokeVoxelVolume
}
