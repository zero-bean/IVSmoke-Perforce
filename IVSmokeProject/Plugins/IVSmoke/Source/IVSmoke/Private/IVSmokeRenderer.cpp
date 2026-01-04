// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeRenderer.h"
#include "IVSmokeVolumeComponent.h"

FIVSmokeRenderer& FIVSmokeRenderer::Get()
{
	static FIVSmokeRenderer Instance;
	return Instance;
}

void FIVSmokeRenderer::AddVolume(UIVSmokeVolumeComponent* Volume)
{
	FScopeLock Lock(&VolumesMutex);
	Volumes.AddUnique(Volume);
}

void FIVSmokeRenderer::RemoveVolume(UIVSmokeVolumeComponent* Volume)
{
	FScopeLock Lock(&VolumesMutex);
	Volumes.Remove(Volume);
}

bool FIVSmokeRenderer::HasVolumes() const
{
	FScopeLock Lock(&VolumesMutex);
	return Volumes.Num() > 0;
}

TArray<FBox> FIVSmokeRenderer::GatherVolumeBounds() const
{
	FScopeLock Lock(&VolumesMutex);

	TArray<FBox> Result;
	for (const auto& WeakVolume : Volumes)
	{
		if (auto* Volume = WeakVolume.Get())
		{
			if (Volume->IsVisible())
			{
				Result.Add(Volume->GetWorldBounds());
			}
		}
	}
	return Result;
}
