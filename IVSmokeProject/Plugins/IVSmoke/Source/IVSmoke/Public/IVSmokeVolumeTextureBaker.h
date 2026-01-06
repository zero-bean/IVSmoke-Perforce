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
	 * @param Resolution 3D texture resolution (X x Y x Z)
	 */
	void Initialize(UObject* Outer, const FIntVector& Resolution);

	FORCEINLINE bool IsInitialized() const
	{
		return TextureResolution.X > 0 &&
			   TextureResolution.Y > 0 &&
			   TextureResolution.Z > 0 &&
			   HoleDataTexture != nullptr;
	}

	/**
	 * @brief Bake curator data into texture
	 * @param Curator Source voxel data
	 */
	void Bake(FIVSmokeVoxelVolumeCurator& Curator) const;

	/** Get hole data texture (RGBA32F: R=Density, G=CreationTime, BA=Reserved) */
	FORCEINLINE UVolumeTexture* GetHoleDataTexture() const { return HoleDataTexture; }

	FORCEINLINE FIntVector GetResolution() const { return TextureResolution; }

private:
	/** Texture resolution (X x Y x Z) */
	FIntVector TextureResolution = FIntVector::ZeroValue;

	/** Combined hole data volume texture (RGBA32F) */
	UPROPERTY(Transient)
	TObjectPtr<UVolumeTexture> HoleDataTexture;
};
