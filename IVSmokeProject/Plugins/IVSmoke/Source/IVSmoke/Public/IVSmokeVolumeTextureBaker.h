// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/VolumeTexture.h"
#include "IVSmokeVolumeTextureBaker.generated.h"

struct FIVSmokeVoxelVolumeCurator;

/**
 * @struct FIVSmokeVolumeTextureBaker
 * @brief Bakes voxel data into a single RGBA32F volume texture for GPU rendering.
 *        R=Density, G=CreationTime, BA=Reserved for future use.
 */
USTRUCT(BlueprintType)
struct IVSMOKE_API FIVSmokeVolumeTextureBaker
{
	GENERATED_BODY()

	/**
	 * @brief Initialize volume texture
	 * @param Outer UObject owner for texture lifetime management
	 * @param Resolution 3D texture resolution (N x N x N)
	 */
	void Initialize(UObject* Outer, int32 Resolution);

	FORCEINLINE bool IsInitialized() const { return TextureResolution > 0 && HoleDataTexture != nullptr; }

	/**
	 * @brief Bake curator data into texture
	 * @param Curator Source voxel data
	 */
	void Bake(FIVSmokeVoxelVolumeCurator& Curator) const;

	/** Get hole data texture (RGBA32F: R=Density, G=CreationTime, BA=Reserved) */
	FORCEINLINE UVolumeTexture* GetHoleDataTexture() const { return HoleDataTexture; }

	FORCEINLINE int32 GetResolution() const { return TextureResolution; }

private:
	/** Texture resolution (N x N x N) */
	int32 TextureResolution = 0;

	/** Combined hole data volume texture (RGBA32F) */
	UPROPERTY(Transient)
	TObjectPtr<UVolumeTexture> HoleDataTexture;
};
