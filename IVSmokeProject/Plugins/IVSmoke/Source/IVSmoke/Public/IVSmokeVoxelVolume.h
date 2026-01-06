// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IVSmokeVoxelVolume.generated.h"

class UIVSmokeSmokePreset;

/** @todo Documentation */
struct FIVSmokeVoxelNode
{
	// @todo Documentation
	int32 Index;

	// @todo Documentation
	float Cost;

	/** @todo Documentation */
	bool operator<(const FIVSmokeVoxelNode& Other) const { return Cost < Other.Cost; }
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

	// @todo Documentation
	Finished
};

/** @todo Documentation */
UENUM(BlueprintType)
enum class EIVSmokeDebugViewMode : uint8
{
	// @todo Documentation
	SolidColor,

	// @todo Documentation
	Heatmap
};

USTRUCT(BlueprintType)
struct FIVSmokeDebugSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug")
	bool bDebugEnabled = true;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug")
	bool bShowVolumeBounds = true;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug")
	bool bShowVoxelMesh = false;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug")
	bool bShowVoxelWireframe = true;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug")
	EIVSmokeDebugViewMode ViewMode = EIVSmokeDebugViewMode::SolidColor;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug")
	FColor DebugWireframeColor = FColor(20, 20, 20);

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug")
	float HeatmapMin = 0.0f;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug")
	float HeatmapMax = 50.0f;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug", meta = (UIMin=0.0, UIMax=1.0))
	float SliceHeight = 1.0f;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug", meta = (ClampMin=0, ClampMax=100))
	int32 VisibleStepCountPercent = 100;
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

	/** Returns the smoke preset override for this volume, or nullptr to use default. */
	const UIVSmokeSmokePreset* GetSmokePresetOverride() const { return SmokePresetOverride; }

	// ============================================================================
	// Smoke Appearance Override
	// ============================================================================

	/** Optional preset override for this volume. If null, uses default from IVSmoke Settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Appearance")
	TObjectPtr<UIVSmokeSmokePreset> SmokePresetOverride;

	virtual bool ShouldTickIfViewportsOnly() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

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

	// @todo Documentation (progress)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Curve")
	UCurveFloat* ExpansionCurve;

	// @todo Documentation (progress)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Curve")
	UCurveFloat* DissipationCurve;

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

	// @todo Documentation (seconds)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | FloodFill")
	float ExpansionDuration = 3.0f;

	// @todo Documentation (seconds)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | FloodFill")
	float SustainDuration = 3.0f;

	// @todo Documentation (seconds)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | FloodFill")
	float DissipationDuration = 1.0f;

	// --- public API (Flood Fill) ---
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Dissipation")
	float CostDissipationNoise = 3.0f;

	// --- public API (Debug) ---
public:
	/** @todo Documentation */
	UFUNCTION(CallInEditor, Category = "IVSmoke | Debug")
	void PreviewSimulation();

	/** @todo Documentation */
	UFUNCTION(CallInEditor, Category = "IVSmoke | Debug")
	void ResetSimulation();

	// @todo Documentation (seconds)
	UPROPERTY(EditDefaultsOnly, Category = "IVSmoke | Debug")
	UStaticMesh* DebugVoxelMesh;

	// @todo Documentation (seconds)
	UPROPERTY(EditDefaultsOnly, Category = "IVSmoke | Debug")
	UMaterialInterface* DebugVoxelMaterial;

	// @todo Documentation (seconds)
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug", meta=(ShowOnlyInnerProperties))
	FIVSmokeDebugSettings DebugSettings;

	// --- Internal Logic ---
private:
	/** @todo Documentation */
	void ProcessFloodFill(int32 SpawnNum);

	/** @todo Documentation */
	bool IsVoxelBlocked(const FVector& WorldPos) const;

	/** @todo Documentation */
	void PrepareDissipation(int32 VoxelNum);

	/** @todo Documentation */
	void ProcessDissipation(int32 RemoveNum);

	/** @todo Documentation */
	void DrawDebugVisualization();

	/** @todo Documentation */
	void DrawDebugBounds();

	/** @todo Documentation */
	void DrawDebugVoxelWireframes();

	/** @todo Documentation */
	void DrawDebugVoxelMeshes();

	/** @todo Documentation */
	void DrawDebugStatusText();

	// @todo Documentation
	EIVSmokeVoxelVolumeState CurrentState = EIVSmokeVoxelVolumeState::Idle;

	// @todo Documentation
	float ElapsedTime = 0.0f;

	// @todo Documentation
	FIntVector GridResolution = FIntVector::ZeroValue;

	// @todo Documentation (Same as MaxDepth)
	FIntVector CenterOffset = FIntVector::ZeroValue;

	// @todo Documentation
	TArray<FIVSmokeVoxelNode> MinHeap;

	// @todo Documentation
	TArray<int32> GeneratedVoxelIndices;

	// @todo Documentation
	TArray<float> VoxelCostArray;

	// @todo Documentation
	int32 ActiveVoxelCount = 0;

	// @todo Documentation
	TArray<int32> VoxelArray;

	/** Dirty flag for GPU buffer synchronization. */
	bool bVoxelDataDirty = false;

	// @todo Documentation
	bool bIsEditorPreviewing = false;

#if WITH_EDITORONLY_DATA
	// @todo Documentation
	UPROPERTY()
	UInstancedStaticMeshComponent* DebugMeshComponent;
#endif
};
