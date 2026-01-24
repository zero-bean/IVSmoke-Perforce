// Copyright SDB. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "IVSmokeHolePreset.generated.h"

UENUM(BlueprintType)
enum class EIVSmokeHoleType : uint8
{
	Penetration,
	Explosion,
	Dynamic,
};

/**
 * @brief Data asset containing hole configuration preset.
 *        Automatically registered to global registry on load.
 */
UCLASS(BlueprintType)
class IVSMOKE_API UIVSmokeHolePreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

protected:
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;

public:
	// ============================================================================
	// Common

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke")
	EIVSmokeHoleType HoleType = EIVSmokeHoleType::Penetration;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke",
		meta = (ClampMin = "0.1", ClampMax = "500.0", EditConditionHides,
			EditCondition = "HoleType != EIVSmokeHoleType::Dynamic"))
	float Radius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke",
		meta = (ClampMin = "0.01", ClampMax = "60.0",
			Tooltip = "Define how long the hole will last within the smoke"))
	float Lifetime = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke",
		meta = (ClampMin = "0.0", ClampMax = "1.0",
			Tooltip = "0 = hard edge, 1 = soft gradient"))
	float EdgeSoftness = 0.3f;

	// ============================================================================
	// Explosion

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke",
		meta = (ClampMin = "0.01", ClampMax = "10.0", EditConditionHides,
			EditCondition = "HoleType == EIVSmokeHoleType::Explosion"))
	float ExtensionTime;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke",
		meta = (ClampMin = "0.0", ClampMax = "1.0", EditConditionHides,
			EditCondition = "HoleType == EIVSmokeHoleType::Explosion"))
	float DensityExtDelayTime;

	// ============================================================================
	// Dynamic

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke",
		meta = (EditConditionHides, EditCondition = "HoleType == EIVSmokeHoleType::Dynamic"))
	FVector3f Extent = FVector3f(50.0f, 50.0f, 50.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke",
		meta = (ClampMin = "10.0", ClampMax = "500.0", EditConditionHides,
			EditCondition = "HoleType == EIVSmokeHoleType::Dynamic"))
	float DistanceThreshold = 50.0f;

	// ============================================================================

	static TObjectPtr<UIVSmokeHolePreset> FindByID(const uint8 InPresetID);
	FORCEINLINE uint8 GetPresetID() const {return CachedID;}

private:
	uint8 CachedID = 0;

	// Register this preset to global registry
	void RegisterToGlobalRegistry();

	// Unregister this preset from global registry
	void UnregisterFromGlobalRegistry();
};
