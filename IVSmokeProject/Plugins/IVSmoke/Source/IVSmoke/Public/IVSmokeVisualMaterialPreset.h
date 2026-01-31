// Copyright (c) 2026, Team SDB. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "IVSmokeVisualMaterialPreset.generated.h"

class UMaterialInterface;

/**
 * Data asset containing visual material and alpha process configuration preset.
 */
UCLASS(BlueprintType)
class IVSMOKE_API UIVSmokeVisualMaterialPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()
protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
public:
	/** It is used in Visual Pass, which is called after upsample filter pass */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Rendering")
	TObjectPtr<UMaterialInterface> SmokeVisualMaterial;
};
