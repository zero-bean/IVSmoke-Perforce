// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IVSmokeVoxelVolume.generated.h"

/** @todo Documentation */
struct FVoxelNode
{
	// @todo Documentation
	int32 Index;

	// @todo Documentation
	float Cost;

	/** @todo Documentation */
	bool operator<(const FVoxelNode& Other) const { return Cost < Other.Cost; }
};

/** @todo Documentation */
UENUM(BlueprintType)
enum class EIVSmokeVoxelVolumeState : uint8
{
	// @todo Documentation
	Idle,

	// @todo Documentation
	Expansion,

	// @todo Documentation
	Sustain,

	// @todo Documentation
	Dissipation,
};

/** @todo Documentation */
UCLASS()
class IVSMOKE_API AIVSmokeVoxelVolume : public AActor
{
	GENERATED_BODY()

public:
	AIVSmokeVoxelVolume();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	// ============================================================================
	// Rendering Data Access
	// ============================================================================

	/** Returns the voxel occupancy array for GPU upload. */
	const TArray<int32>& GetVoxelArray() const { return VoxelArray; }

	/** Returns the grid resolution (dimensions of the voxel grid). */
	FIntVector GetGridResolution() const { return GridResolution; }

	/** Returns the center offset for grid-to-local coordinate conversion. */
	FIntVector GetCenterOffset() const { return CenterOffset; }

	/** Returns the world-space size of each voxel. */
	float GetVoxelSize() const { return VoxelSize; }

	/** Returns true if voxel data has been modified since last GPU upload. */
	bool IsVoxelDataDirty() const { return bVoxelDataDirty; }

	/** Clears the dirty flag after GPU upload. Called by renderer. */
	void ClearVoxelDataDirty() { bVoxelDataDirty = false; }

	/** Returns the current buffer size (for detecting resize). */
	int32 GetVoxelBufferSize() const { return VoxelArray.Num(); }

	// --- public API ---
public:
	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config")
	FIntVector VolumeExtent = FIntVector(16, 16, 16);

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config")
	float VoxelSize = 50.0f;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config")
	int32 MaxVoxelNum = 1000;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Collision")
	TEnumAsByte<ECollisionChannel> VoxelCollisionChannel = ECC_WorldStatic;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Collision")
	float CollisionExtentScale = 0.9f;

	// --- public API (Flood Fill) ---
public:
	/** @todo Documentation */
	UFUNCTION(BlueprintCallable, Category = "IVSmoke")
	void StartFloodFill();

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | FloodFill")
	float CostBase = 1.0f;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | FloodFill")
	float CostUpModifier = 2.5f;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | FloodFill")
	float CostDownModifier = 0.5f;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | FloodFill")
	float CostDistanceModifier = 0.1f;

	// @todo Documentation (progress)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | FloodFill")
	UCurveFloat* ExpansionCurve;

	// @todo Documentation (seconds)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | FloodFill")
	float ExpansionDuration = 3.0f;

	// @todo Documentation (seconds)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | FloodFill")
	float SustainDuration = 3.0f;

	// @todo Documentation (seconds)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | FloodFill")
	float DissipationDuration = 1.0f;

	// --- public API (Debug) ---
public:
	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Debug")
	bool bDebugEnabled = false;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Debug")
	bool bShowVolume = false;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Debug")
	bool bShowVoxel = false;

	// --- Internal Logic ---
private:
	/** @todo Documentation */
	void ProcessFloodFill(int32 SpawnNum);

	/** @todo Documentation */
	bool IsVoxelBlocked(const FVector& WorldPos) const;

	/** @todo Documentation */
	void DrawDebugVisualization();

	// @todo Documentation
	EIVSmokeVoxelVolumeState CurrentState = EIVSmokeVoxelVolumeState::Idle;

	// @todo Documentation
	float ElapsedTime = 0.0f;

	// @todo Documentation
	FIntVector GridResolution = FIntVector::ZeroValue;

	// @todo Documentation (Same as MaxDepth)
	FIntVector CenterOffset = FIntVector::ZeroValue;

	// @todo Documentation
	TArray<FVoxelNode> PriorityQueue;

	// @todo Documentation
	TArray<int32> ActiveVoxelIndices;

	// @todo Documentation
	TArray<float> VoxelCostArray;

	// @todo Documentation
	TArray<int32> VoxelArray;

	/** Dirty flag for GPU buffer synchronization. */
	bool bVoxelDataDirty = false;
};
