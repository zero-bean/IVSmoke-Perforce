// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "IVSmokeHoleData.h"
#include "IVSmokeVoxelVolumeCurator.h"
#include "IVSmokeVolumeTextureBaker.h"
#include "IVSmokeCollisionComponent.generated.h"

/**
 * @class UIVSmokeCollisionComponent
 * @brief Component that defines an interactive volumetric smoke volume.
 *        Detects projectile penetration and collects hole data for GPU rendering.
 *
 * @usage
 * - Attach to any smoke grenade actor
 * - Projectiles: Automatically detected via OnComponentBeginOverlap
 * - Hitscan: Call CreateHole() manually from weapon code
 */
UCLASS(ClassGroup = (IVSmoke), meta = (BlueprintSpawnableComponent))
class IVSMOKE_API UIVSmokeCollisionComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
	UIVSmokeCollisionComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ============================================================================
	// Public API
	// ============================================================================

	/** @brief Create a hole data (Only support for projectiles based on HITSCAN) */
	UFUNCTION(BlueprintCallable, Category = "IVSmoke")
	void CreateHole(const FVector& Position, const FVector& Direction, const double Radius);

	/** Returns the array of currently active holes (for GPU buffer) */
	UFUNCTION(BlueprintPure, Category = "IVSmoke")
	const TArray<FIVSmokeHoleData>& GetActiveHoles() const { return ActiveHoles; }

	/** Returns the number of active holes */
	UFUNCTION(BlueprintPure, Category = "IVSmoke")
	int32 GetActiveHoleCount() const { return ActiveHoles.Num(); }

protected:
	// ============================================================================
	// Collision Detection
	// ============================================================================

	UFUNCTION()
	void OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	// ============================================================================
	// Detection
	// ============================================================================

	/** Enable detection of projectiles via overlap */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Detection")
	bool bAutoDetectProjectiles = true;

	/** Default radius for auto-detected projectile holes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Detection",
		meta = (EditCondition = "bAutoDetectProjectiles", ClampMin = "0.1", ClampMax = "10.0"))
	double DefaultHoleRadius = 1.0;

	/** Ratio of exit radius to entry radius (0.0 = point, 1.0 = cylinder) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Detection",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EndRadiusRatio = 0.2f;

	// ============================================================================
	// Hole Data Configuration
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
	// Voxel Volume Configuration
	// ============================================================================

	/** Voxel grid resolution (X x Y x Z). Higher = more detail, more memory */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Voxel")
	FIntVector VoxelResolution = FIntVector(64, 64, 64);

	// ============================================================================
	// Voxel Data Access (for rendering)
	// ============================================================================
public:
	/** Get voxel curator for texture data access */
	UFUNCTION(BlueprintPure, Category = "IVSmoke|Voxel")
	const FIVSmokeVoxelVolumeCurator& GetVoxelCurator() const { return VoxelCurator; }

	/** Check if voxel texture needs update */
	UFUNCTION(BlueprintPure, Category = "IVSmoke|Voxel")
	bool IsVoxelTextureDirty() const { return VoxelCurator.IsTextureDirty(); }

	/** Get the hole data volume texture (RGBA32F: R=Density, G=CreationTime, BA=Reserved) */
	UFUNCTION(BlueprintPure, Category = "IVSmoke|Voxel")
	UVolumeTexture* GetHoleDataTexture() const { return TextureBaker.GetHoleDataTexture(); }

	/** Manually bake textures if dirty (called automatically in Tick) */
	UFUNCTION(BlueprintCallable, Category = "IVSmoke|Voxel")
	void BakeTexturesIfDirty();

	/** Get voxel grid resolution */
	UFUNCTION(BlueprintPure, Category = "IVSmoke|Voxel")
	FIntVector GetVoxelResolution() const { return VoxelResolution; }

	/** Get hole lifetime in seconds */
	UFUNCTION(BlueprintPure, Category = "IVSmoke|Holes")
	float GetHoleLifeTime() const { return HoleLifeTime; }

	// ============================================================================
	// Debug - Volume Texture Visualization
	// ============================================================================

	/** Enable volume texture slice debug visualization */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Debug")
	bool bShowVolumeSlice = false;

	/** Z-slice index to display (0 to Resolution-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Debug",
		meta = (EditCondition = "bShowVolumeSlice", ClampMin = "0"))
	int32 DebugSliceIndex = 0;

	/** Debug visualization mode: 0=Density, 1=Time, 2=Combined */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Debug",
		meta = (EditCondition = "bShowVolumeSlice", ClampMin = "0", ClampMax = "2"))
	int32 DebugMode = 0;

	// ============================================================================
	// Debug - Voxel Box Visualization
	// ============================================================================
protected:
#if ENABLE_DRAW_DEBUG
	/** Draws debug boxes for voxels with holes */
	void DrawDebugVoxels() const;
#endif

	/** Enable voxel grid debug visualization (shows voxels with holes) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Debug")
	bool bDebugDrawVoxels = true;

	// ============================================================================
	// Hole Management
	// ============================================================================
private:
	/** Removes expired holes based on lifetime */
	void CleanupExpiredHoles(double CurrentTime);

	/**
	 * Apply hole to voxel volume using Capped Cone SDF
	 * @param WorldPosition Entry point in world space
	 * @param Direction Normalized direction of the projectile
	 * @param StartRadius Radius at entry point (larger, covers snapping error)
	 * @param EndRadius Radius at exit point (smaller)
	 */
	void ApplyHoleToVoxelVolume(
		const FVector& WorldPosition,
		const FVector& Direction,
		float StartRadius,
		float EndRadius
	);

	/** Array of currently active holes (for debug visualization) */
	UPROPERTY(VisibleAnywhere, Category = "IVSmoke|Debug")
	TArray<FIVSmokeHoleData> ActiveHoles;

	/** Voxel volume curator for density management */
	UPROPERTY()
	FIVSmokeVoxelVolumeCurator VoxelCurator;

	/** Texture baker for GPU rendering */
	UPROPERTY()
	FIVSmokeVolumeTextureBaker TextureBaker;
};
