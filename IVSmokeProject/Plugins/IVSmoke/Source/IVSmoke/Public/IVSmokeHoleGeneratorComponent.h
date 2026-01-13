// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "RHIResources.h"
#include "Net/UnrealNetwork.h"
#include "IVSmokeHoleData.h"
#include "IVSmokeHoleGeneratorComponent.generated.h"

/**
 * @brief Component that generates hole texture for volumetric smoke.
 *        Provides public API for penetration and explosion holes.
 *        Supports network replication (Dedicated Server and Standalone).
 */
UCLASS(ClassGroup = (IVSmoke), meta = (BlueprintSpawnableComponent))
class IVSMOKE_API UIVSmokeHoleGeneratorComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
	UIVSmokeHoleGeneratorComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	// ============================================================================
	// Public API (Server RPC)
	// ============================================================================

	/**
	 * @brief Request a penetration hole (bullet, projectile, hitscan).
	 * @note Server RPC - callable from any machine, executed on Authority.
	 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IVSmoke|Hole")
	void RequestPenetrationHole(const FIVSmokePenetrationRequest& Request);

	/**
	 * @brief Request to create a spherical hole at the specified origin.
	 * @note Server RPC - callable from any machine, executed on Authority.
	 */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IVSmoke|Hole")
	void RequestExplosionHole(const FIVSmokeExplosionRequest& Request);

public:
	// ============================================================================
	// Configuration
	// ============================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Hole|Optimization",
		meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxHoles = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Hole|Configuration",
		meta = (ClampMin = "0.0", ClampMax = "1.0", Tooltip = "0=hard edge, 1=soft gradient"))
	float EdgeSoftness = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Hole|Configuration",
		meta = (ClampMin = "0.0", ClampMax = "1.0", Tooltip = "0=transparent, 1=full hole"))
	float DensityMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Hole|Debug")
	bool bShowVolumeDebug = true;

	// ============================================================================
	// Texture Access
	// ============================================================================

	UFUNCTION(BlueprintCallable, Category = "IVSmoke|Sync")
	void SyncWithVoxelVolume(FIntVector VolumeExtent, float InVoxelSize);

	FTextureRHIRef GetHoleTexture() const { return HoleTexture; }

	UFUNCTION(BlueprintPure, Category = "IVSmoke|Voxel")
	FORCEINLINE FIntVector GetVoxelResolution() const { return VoxelResolution; }

protected:
	// ============================================================================
	// Replicated State
	// ============================================================================

	/** Active holes array - replicated to all clients */
	UPROPERTY(Replicated, VisibleAnywhere, Category = "IVSmoke|Debug")
	TArray<FIVSmokeHoleData> ActiveHoles;

private:
	// ============================================================================
	// Authority Only (Server / Standalone)
	// ============================================================================

	/** Create a hole and add to ActiveHoles (Authority only) */
	void Authority_CreateHole(const FIVSmokeHoleData& HoleData);

	/** Remove expired holes from ActiveHoles (Authority only) */
	void Authority_CleanupExpiredHoles();

	// ============================================================================
	// Local Only (Client / Standalone)
	// ============================================================================

	/** Rebuild entire hole texture from ActiveHoles */
	void Local_RebuildHoleTexture();

	// ============================================================================
	// Helper
	// ============================================================================

	/** Get synchronized server time */
	float GetSyncedTime() const;

	/** Calculate penetration entry/exit points via raycast */
	bool CalculatePenetrationPoints(const FIVSmokePenetrationRequest& Request, FVector& OutEntry, FVector& OutExit);

	/** Initialize 3D texture for hole data */
	void InitializeHoleTexture();

	/** Convert local position to voxel coordinate (floor) */
	FIntVector LocalToVoxel(const FVector& LocalPos) const;

	/** Convert local position to voxel coordinate (ceil) */
	FIntVector LocalToVoxelCeil(const FVector& LocalPos) const;

	/** Build GPU buffer from ActiveHoles */
	TArray<FIVSmokeHoleGPU> BuildGPUHoleBuffer() const;

	// ============================================================================
	// Local State (Not Replicated)
	// ============================================================================

	FIntVector VoxelResolution = FIntVector(128, 128, 128);
	FTextureRHIRef HoleTexture;
};
