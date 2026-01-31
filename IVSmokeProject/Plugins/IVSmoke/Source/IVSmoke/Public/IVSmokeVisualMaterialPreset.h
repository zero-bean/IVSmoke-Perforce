// Copyright (c) 2026, Team SDB. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "IVSmokeVisualMaterialPreset.generated.h"

class UMaterialInterface;

/**
 * Alpha processing type in composite pass.
 */
UENUM(BlueprintType)
enum class EIVSmokeVisualAlphaType : uint8
{
	/** Use SmokeVisualMaterial alpha value */
	Alpha UMETA(DisplayName = "Use Alpha (0 ~ 1)"),

	/** Alpha <= AlphaThreshold ? 0 : 1 */
	CutOff UMETA(DisplayName = "CutOff (Alpha <= AlphaThreshold ? 0 : 1)"),
};

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

	/** Alpha processing type in composite pass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Rendering")
	EIVSmokeVisualAlphaType VisualAlphaType = EIVSmokeVisualAlphaType::Alpha;

	/** Minimum alpha threshold for rendering. Pixels with alpha below this value will be discarded. Only used when VisualAlphaType is CutOff. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Rendering", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "VisualAlphaType == EIVSmokeVisualAlphaType::CutOff", EditConditionHides))
	float AlphaThreshold = 0.0f;

	/** Upper bound threshold for low-opacity remapping to suppress HDR burn-through and low-density artifacts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Rendering", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float LowOpacityRemapThreshold = 0.02f;


};
