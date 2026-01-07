// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "RHIResources.h"
#include "IVSmokeHoleData.h"
#include "IVSmokeHoleGeneratorComponent.generated.h"

/**
 * @class UIVSmokeHoleGeneratorComponent
 * @brief Component that generates hole texture for volumetric smoke.
 *        Detects projectile penetration and creates GPU-rendered holes.
 *
 * @usage
 * - Attach to any smoke grenade actor
 * - Projectiles: Automatically detected via OnComponentBeginOverlap
 * - Hitscan: Call CreateHole() manually from weapon code
 * - Access hole texture via GetHoleTexture() for smoke rendering
 */
UCLASS(ClassGroup = (IVSmoke), meta = (BlueprintSpawnableComponent))
class IVSMOKE_API UIVSmokeHoleGeneratorComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
	UIVSmokeHoleGeneratorComponent();
	virtual ~UIVSmokeHoleGeneratorComponent() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ============================================================================
	// Public API
	// ============================================================================

	/** @brief Create a hole at specified position and direction */
	UFUNCTION(BlueprintCallable, Category = "IVSmoke")
	void CreateHole(const FVector& Position, const FVector& Direction, const double Radius);

	/** Returns the array of currently active holes */
	UFUNCTION(BlueprintPure, Category = "IVSmoke")
	const TArray<FIVSmokeHoleData>& GetActiveHoles() const { return ActiveHoles; }

	/** Returns the number of active holes */
	UFUNCTION(BlueprintPure, Category = "IVSmoke")
	int32 GetActiveHoleCount() const { return ActiveHoles.Num(); }

	// ============================================================================
	// Hole Texture Access
	// ============================================================================

	/** Get hole volume texture for smoke rendering (SRV access) */
	FTextureRHIRef GetHoleTexture() const { return HoleTexture; }

	/** Check if hole texture is ready */
	bool IsHoleTextureValid() const { return HoleTexture.IsValid(); }

	/** Get voxel grid resolution */
	UFUNCTION(BlueprintPure, Category = "IVSmoke|Voxel")
	FIntVector GetVoxelResolution() const { return VoxelResolution; }

	/** Get hole lifetime in seconds */
	UFUNCTION(BlueprintPure, Category = "IVSmoke|Holes")
	float GetHoleLifeTime() const { return HoleLifeTime; }

protected:
	// ============================================================================
	// Collision Detection
	// ============================================================================

	// @todo this function will be removed after changed into a detection based on hit event
	UFUNCTION()
	void OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	// ============================================================================
	// Detection Settings
	// ============================================================================

	/** Enable detection of projectiles via overlap */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Detection")
	bool bAutoDetectProjectiles = true;

	/** Default radius for auto-detected projectile holes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Detection",
		meta = (EditCondition = "bAutoDetectProjectiles", ClampMin = "1.0", ClampMax = "100.0"))
	double DefaultHoleRadius = 50.0;

	/** Ratio of exit radius to entry radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Detection",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EndRadiusRatio = 0.5f;

	// ============================================================================
	// Hole Configuration
	// ============================================================================

	/** Lifetime of each hole in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Holes",
		meta = (ClampMin = "0.1", ClampMax = "30.0"))
	double HoleLifeTime = 3.0;

	/** Maximum number of holes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Holes",
		meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxHoles = 128;

	// ============================================================================
	// Texture Configuration
	// ============================================================================

	/** Voxel grid resolution (X x Y x Z) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Texture")
	FIntVector VoxelResolution = FIntVector(64, 64, 64);

	/** Number of spheres to place along each trajectory */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Texture",
		meta = (ClampMin = "2", ClampMax = "100"))
	int32 NumSpheresPerTrajectory = 10;

	// ============================================================================
	// Debug
	// ============================================================================
public:
	/** Enable volume texture debug visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Debug")
	bool bShowVolumeDebug = false;

private:
	// ============================================================================
	// Internal Methods
	// ============================================================================

	/** Initialize hole texture (UAV-capable 3D texture) */
	void InitializeHoleTexture();

	/** Release hole texture */
	void ReleaseHoleTexture();

	/** Removes expired holes based on lifetime */
	void CleanupExpiredHoles(double CurrentTime);

	/** Dispatch GPU Compute Shader to carve holes into texture */
	void DispatchHoleCarveCS(double CurrentTime);

	// ============================================================================
	// Internal Data
	// ============================================================================

	/** Array of currently active holes */
	UPROPERTY(VisibleAnywhere, Category = "IVSmoke|Debug")
	TArray<FIVSmokeHoleData> ActiveHoles;

	/** UAV-capable 3D texture for hole data (R=Density, G=CreationTime) */
	FTextureRHIRef HoleTexture;

	/** Track if needing to clear texture when all holes expire */
	bool bNeedsClear = false;
};
