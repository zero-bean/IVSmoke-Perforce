// Copyright SDB. All Rights Reserved.

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

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	// ============================================================================
	// Public API (Server RPC)
	// ============================================================================

	/** @brief Request a penetration hole (bullet, projectile, hitscan) */
	UFUNCTION(BlueprintCallable, Category = "IVSmoke | Hole")
	void RequestPenetrationHole(FVector Origin, FVector Direction, UIVSmokeHolePreset* Preset);

	/** @brief Request an explosion hole at the specified origin. */
	UFUNCTION(BlueprintCallable, Category = "IVSmoke | Hole")
	void RequestExplosionHole(FVector Origin, UIVSmokeHolePreset* Preset);

private:
	// ============================================================================
	// Internal Server RPC
	// ============================================================================

	UFUNCTION(Server, Reliable)
	void Internal_RequestPenetrationHole(FVector Origin, FVector Direction, uint8 PresetID);

	UFUNCTION(Server, Reliable)
	void Internal_RequestExplosionHole(FVector Origin, uint8 PresetID);

public:
	// ============================================================================
	// Configuration
	// ============================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Hole | Optimization",
		meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxHoles = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Hole | Configuration",
		meta = (ToolTip = "Select the type of obstacle that will block the penetration hole"))
	TArray<TEnumAsByte<EObjectTypeQuery>> ObstacleObjectTypes;

	// ============================================================================
	// Texture Access
	// ============================================================================

	// Get hole texture RHI for shader binding
	FTextureRHIRef GetHoleTextureRHI() const;

	UFUNCTION(BlueprintPure, Category = "IVSmoke | Voxel")
	FORCEINLINE FIntVector GetVoxelResolution() const { return VoxelResolution; }

protected:
	// ============================================================================
	// Replicated State
	// ============================================================================

	// Active holes array - replicated to all clients
	UPROPERTY(Replicated, VisibleAnywhere, Category = "IVSmoke | Hole | Debug")
	TArray<FIVSmokeHoleData> ActiveHoles;

private:
	// ============================================================================
	// Authority Only (Server / Standalone)
	// ============================================================================

	// Create a hole and add to ActiveHoles (Authority only)
	void Authority_CreateHole(const FIVSmokeHoleData& HoleData);

	// Remove expired holes from ActiveHoles (Authority only)
	void Authority_CleanupExpiredHoles();

	// ============================================================================
	// Local Only (Client / Standalone)
	// ============================================================================

	// Rebuild entire hole texture from ActiveHoles
	void Local_RebuildHoleTexture();

	// ============================================================================
	// Helper
	// ============================================================================

	// Get synchronized server time
	float GetSyncedTime() const;

	// Calculate penetration entry/exit points via raycast
	bool CalculatePenetrationPoints(FVector Origin, FVector Direction, float EndRadius, FVector& OutEntry, FVector& OutExit);

	// Initialize 3D texture for hole data
	void InitializeHoleTexture();

	// Build GPU buffer from ActiveHoles
	TArray<FIVSmokeHoleGPU> BuildGPUHoleBuffer() const;

	// ============================================================================
	// Local State (Not Replicated)
	// ============================================================================

	// Set BoxExtent and Component Position to VoxelAABB Center
	void SetBoxToVoxelAABB();

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Hole | Optimization")
	FIntVector VoxelResolution = FIntVector(64, 64, 64);

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTargetVolume> HoleTexture = nullptr;
};
