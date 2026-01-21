// Copyright SDB. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "IVSmokeHolePreset.generated.h"

UENUM(BlueprintType)
enum class EIVSmokeHoleType : uint8
{
	Penetration,
	Explosion,
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
	// ============================================================================
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke")
	EIVSmokeHoleType HoleType = EIVSmokeHoleType::Penetration;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke",
		meta = (ClampMin = "0.1", ClampMax = "500.0"))
	float StartRadius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke",
		meta = (ClampMin = "0.01", ClampMax = "60.0",
			Tooltip = "Define how long the hole will last within the smoke"))
	float Lifetime = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke",
		meta = (ClampMin = "0.0", ClampMax = "1.0",
			Tooltip = "0 = hard edge, 1 = soft gradient"))
	float EdgeSoftness = 0.3f;



	// ============================================================================
	// Only Grenade
	// ============================================================================
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke",
		meta = (ClampMin = "0.01", ClampMax = "10.0", EditConditionHides,
			EditCondition = "HoleType == EIVSmokeHoleType::Explosion"))
	float ExtensionTime;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke",
		meta = (ClampMin = "0.0", ClampMax = "1.0", EditConditionHides,
			EditCondition = "HoleType == EIVSmokeHoleType::Explosion"))
	float DensityExtDelayTime;

	// ============================================================================
	// Only Bullet
	// ============================================================================
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke",
		meta = (ClampMin = "0.0", ClampMax = "500.0", EditConditionHides,
			EditCondition = "HoleType == EIVSmokeHoleType::Penetration"))
	float EndRadius = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IVSmoke",
		meta = (ClampMin = "0.0", ClampMax = "1.0", EditConditionHides,
			EditCondition = "HoleType == EIVSmokeHoleType::Penetration",
			Tooltip = "0 = transparent, 1 = full hole"))
	float DensityMultiplier = 1.0f;











	FORCEINLINE uint8 GetPresetID() const {return CachedID;}

	/**
	 * @brief Find preset by ID from global registry.
	 * @return Preset if found, nullptr otherwise.
	 */
	static TObjectPtr<UIVSmokeHolePreset> FindByID(const uint8 InPresetID);

private:
	uint8 CachedID = 0;

	// Register this preset to global registry
	void RegisterToGlobalRegistry();

	// Unregister this preset from global registry
	void UnregisterFromGlobalRegistry();
};
