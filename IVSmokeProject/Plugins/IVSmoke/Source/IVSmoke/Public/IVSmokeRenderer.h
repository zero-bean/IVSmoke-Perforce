// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UIVSmokeVolumeComponent;

/**
 * Manages registered smoke volumes.
 * Rendering is handled internally by SceneViewExtension.
 */
class IVSMOKE_API FIVSmokeRenderer
{
public:
	static FIVSmokeRenderer& Get();

	void AddVolume(UIVSmokeVolumeComponent* Volume);
	void RemoveVolume(UIVSmokeVolumeComponent* Volume);

	bool HasVolumes() const;
	TArray<FBox> GatherVolumeBounds() const;

private:
	FIVSmokeRenderer() = default;

	TArray<TWeakObjectPtr<UIVSmokeVolumeComponent>> Volumes;
	mutable FCriticalSection VolumesMutex;
};
