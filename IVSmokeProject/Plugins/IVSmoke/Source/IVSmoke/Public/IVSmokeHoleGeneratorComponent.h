// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "RHIResources.h"
#include "IVSmokeHoleData.h"
#include "IVSmokeHoleGeneratorComponent.generated.h"

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

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// ============================================================================
	// Public API
	// ============================================================================

	/** @brief Request a penetration hole (bullet, projectile, hitscan). */
	UFUNCTION(BlueprintCallable, Category = "IVSmoke")
	void RequestPenetrationHole(const FIVSmokePenetrationRequest& Request);

	/** @brief Request to create a spherical hole at the specified origin. */
	UFUNCTION(BlueprintCallable, Category = "IVSmoke")
	void RequestExplosionHole(const FIVSmokeExplosionRequest& Request);

public:
	// ============================================================================
	// Hole Configuration
	// ============================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Holes|Optimization",
		meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxHoles = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Holes|Optimization",
	meta = (ClampMin = "0.016", ClampMax = "0.5", Tooltip = "Recommended: 0.05~0.1 seconds"))
	float BatchIntervalSeconds = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Holes|Configuration",
		meta = (ClampMin = "0.0", ClampMax = "1.0", Tooltip = "0=hard edge, 1=soft gradient"))
	float EdgeSoftness = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Holes|Configuration",
		meta = (ClampMin = "0.0", ClampMax = "1.0", Tooltip = "0=transparent, 1=full hole"))
	float DensityMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Holes|Debug")
	bool bShowVolumeDebug = true;

	// ============================================================================
	// Hole Texture Access
	// ============================================================================

	// Sync box extent and texture resolution with IVSmokeVoxelVolume
	UFUNCTION(BlueprintCallable, Category = "IVSmoke|Sync")
	void SyncWithVoxelVolume(FIntVector VolumeExtent, float InVoxelSize);

	// Get hole volume texture for smoke rendering (SRV access)
	FTextureRHIRef GetHoleTexture() const { return HoleTexture; }

	// Get voxel grid resolution
	UFUNCTION(BlueprintPure, Category = "IVSmoke|Voxel")
	FORCEINLINE FIntVector GetVoxelResolution() const { return VoxelResolution; }

private:
	// ============================================================================
	// Data
	// ============================================================================

	// Array of currently active holes
	UPROPERTY(VisibleAnywhere, Category = "IVSmoke|Debug")
	TArray<FIVSmokeHoleData> ActiveHoles;

	// Array of new holes waiting for batch processing
	TArray<FIVSmokeHoleData> PendingHoles;

	// Time accumulator for batch update
	float TimeSinceLastUpdate = 0.0f;

	// Accumulated AABB for pending work
	FBox PendingAABB;

	// Flag indicating pending AABB has valid data
	bool bHasPendingWork = false;

	// Voxel grid resolution
	FIntVector VoxelResolution = FIntVector(64, 64, 64);

	// R16 3D texture for hole density
	FTextureRHIRef HoleTexture;

	// ============================================================================
	// Hole Creation
	// ============================================================================

	void CreateHole(const FIVSmokeHoleData& HoleData);

	bool CalculatePenetrationPoints(const FIVSmokePenetrationRequest& Request, FVector& OutEntry, FVector& OutExit);

	// ============================================================================
	// Texture Management
	// ============================================================================

	void InitializeHoleTexture();

	void CleanupExpiredHoles(double CurrentTime);

	/** Accumulate hole AABB into pending region */
	void AccumulateHoleAABB(const FIVSmokeHoleData& Hole);
	void AccumulateHoleAABB(const FIVSmokeHoleData& Hole, const FTransform& Transform);

	/** Dispatch batch update for Union AABB region */
	void DispatchBatchUpdate(const FIntVector& RegionMin, const FIntVector& RegionMax, double CurrentTime);

	/** Calculate union AABB of multiple holes in voxel coordinates */
	void CalculateUpdateRegion(const TArray<FIVSmokeHoleData>& Holes, FIntVector& OutMin, FIntVector& OutMax) const;

	/** Convert world position to voxel coordinate */
	FIntVector WorldToVoxel(const FVector& WorldPos) const;

	/** Convert local position to voxel coordinate (floor) */
	FIntVector LocalToVoxel(const FVector& LocalPos) const;

	/** Convert local position to voxel coordinate (ceil, for AABB max) */
	FIntVector LocalToVoxelCeil(const FVector& LocalPos) const;

	/** Build GPU buffer from hole data */
	TArray<FIVSmokeHoleGPU> BuildGPUHoleBuffer(const TArray<FIVSmokeHoleData>& Holes, double CurrentTime) const;
};
