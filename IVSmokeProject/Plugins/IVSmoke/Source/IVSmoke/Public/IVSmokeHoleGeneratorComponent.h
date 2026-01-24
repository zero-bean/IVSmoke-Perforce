// Copyright (c) 2026, Team SDB. All rights reserved.

#pragma once

#include "Components/BoxComponent.h"
#include "IVSmokeHoleData.h"
#include "IVSmokeHoleGeneratorComponent.generated.h"

class UTextureRenderTargetVolume;
class UIVSmokeHolePreset;

/**
 * @brief Component that generates hole texture for volumetric smoke.
 *        Provides public API for penetration and explosion holes.
 */
UCLASS(ClassGroup = (IVSmoke), meta = (BlueprintSpawnableComponent))
class IVSMOKE_API UIVSmokeHoleGeneratorComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
	UIVSmokeHoleGeneratorComponent();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	//~============================================================================
	// Public API (Blueprint & C++)
#pragma region API
public:
	/** Request a penetration hole such as bullet, projectile and hitscan */
	UFUNCTION(BlueprintCallable, Category = "IVSmoke | Hole | API")
	void RequestPenetrationHole(const FVector3f Origin, const FVector3f Direction, UIVSmokeHolePreset* Preset);

	/** Request an explosion hole at the specified origin. such as grenade (todo: must be refactored) */
	UFUNCTION(BlueprintCallable, Category = "IVSmoke | Hole | API")
	void RequestExplosionHole(const FVector3f Origin, UIVSmokeHolePreset* Preset);

	/** Request registration of tracking dynamic object such as human and vehicle */
	UFUNCTION(BlueprintCallable, Category = "IVSmoke | Hole | API")
	void RequestTrackDynamicObject(AActor* TargetActor, UIVSmokeHolePreset* Preset);
#pragma endregion

	//~============================================================================
	// Internal Server RPC
#pragma region Server RPC
private:
	UFUNCTION(Server, Reliable)
	void Internal_RequestPenetrationHole(const FVector3f& Origin, const FVector3f& Direction, const uint8 PresetID);

	UFUNCTION(Server, Reliable)
	void Internal_RequestExplosionHole(const FVector3f& Origin, const uint8 PresetID);

	UFUNCTION(Server, Reliable)
	void Internal_RequestDynamicHole(AActor* TargetActor, const uint8 PresetID);
#pragma endregion

	//~============================================================================
	// Authority Only (Server & Standalone)
#pragma region Authority Only
private:

	UPROPERTY(Transient)
	TArray<FIVSmokeHoleDynamicSubject> DynamicSubjectList;

	/** Create hole data to be rendered by GPU. (todo: must be refactored) */
	void Authority_CreateHole(const FIVSmokeHoleData& HoleData);

	/** Clean up expired hole data and notify GPU to be updated. */
	void Authority_CleanupExpiredHoles();

	/** Calculate penetration entry & exit points via raycast. */
	bool Authority_CalculatePenetrationPoints(const FVector3f& Origin, const FVector3f& Direction, const float Radius, FVector3f& OutEntry, FVector3f& OutExit);

	/** Manage the dynamic object's life cycle and update dynamic hole. */
	void Authority_UpdateDynamicSubjectList();
#pragma endregion

	//~============================================================================
	// Local Only (Client & Standalone)
#pragma region Local Only
private:
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTargetVolume> HoleTexture = nullptr;

	/** Initialize 3D texture for hole data. */
	void Local_InitializeHoleTexture();

	/** Rebuild entire hole texture from ActiveHoles. (todo: must be refactored) */
	void Local_RebuildHoleTexture();
#pragma endregion

	//~============================================================================
	// Common
#pragma region Common
public:

	/** Maximum number of holes that can be activated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Hole | Optimization", meta = (ClampMin = "1", ClampMax = "512"))
	int32 MaxHoles = 128;

	/** Hole voxel volume resolution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Hole | Optimization", meta = (ClampMin = "16", ClampMax = "128"))
	FIntVector VoxelResolution = FIntVector(64, 64, 64);

	/** Maximum number of holes that can be activated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Hole | Configuration",
		meta = (ToolTip = "Select the type of obstacle that will block the penetration hole"))
	TArray<TEnumAsByte<EObjectTypeQuery>> ObstacleObjectTypes;

	/** Get synchronized server time. */
	float GetSyncedTime() const;

	/** Get Texture as a UTextureRenderTargetVolume to write by. */
	FTextureRHIRef GetHoleTextureRHI() const;

	/** Set BoxExtent and Component Position to VoxelAABB Center. */
	void SetBoxToVoxelAABB();

	/** Set Dirty flag whether GPU updates the texure. */
	FORCEINLINE void MarkHoleTextureDirty(const bool bIsDirty = true) { bHoleTextureDirty = bIsDirty; }

private:

	/** These are the holes activated in this smoke volume.*/
	UPROPERTY(Transient, Replicated, VisibleAnywhere, Category = "IVSmoke | Hole | Debug")
	FIVSmokeHoleArray ActiveHoles;

	/** HoleTexture dirty flag. */
	uint8 bHoleTextureDirty : 1;
#pragma endregion
};
