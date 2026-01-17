// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IVSmokeVoxelVolume.generated.h"

class UIVSmokeCollisionComponent;
class UIVSmokeSmokePreset;
class UIVSmokeHoleGeneratorComponent;

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

/**
 * Dirty level for GPU texture synchronization.
 */
UENUM(BlueprintType)
enum class EIVSmokeDirtyLevel : uint8
{
	/** No changes since last GPU upload. Texture can be reused. */
	Clean,

	/** Voxel data changed. Full texture upload required. */
	Dirty
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
	EIVSmokeDebugViewMode ViewMode = EIVSmokeDebugViewMode::SolidColor;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug")
	bool bShowVolumeBounds = true;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug")
	bool bShowVoxelMesh = false;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug")
	bool bShowVoxelWireframe = true;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug")
	bool bShowStatusText = true;

	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug")
	FColor DebugWireframeColor = FColor(20, 20, 20);

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

	//~==============================================================================
	// Actor Lifecycle
#pragma region Lifecycle
public:
	AIVSmokeVoxelVolume();

	virtual void Tick(float DeltaTime) override;
	virtual bool ShouldTickIfViewportsOnly() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
#pragma endregion

	//~==============================================================================
	// Actor Components
#pragma region Components
public:
	// @todo Documentation
	FORCEINLINE UIVSmokeHoleGeneratorComponent* GetHoleGeneratorComponent() { return HoleGeneratorComponent; }

	// @todo Documentation
	FORCEINLINE UIVSmokeCollisionComponent* GetCollisionComponent() { return CollisionComponent; }

private:
	// @todo Documentation
	UPROPERTY(EditAnywhere, Category = "IVSmoke")
	TObjectPtr<UIVSmokeHoleGeneratorComponent> HoleGeneratorComponent;

	// @todo Documentation
	UPROPERTY(EditAnywhere, Category = "IVSmoke")
	TObjectPtr<UIVSmokeCollisionComponent> CollisionComponent;

#if WITH_EDITORONLY_DATA
	// @todo Documentation
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> DebugMeshComponent;
#endif
#pragma endregion

	//~==============================================================================
	// Actor Configuration
#pragma region Configuation
public:
	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config")
	FIntVector VolumeExtent = FIntVector(16, 16, 16);

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config")
	FVector Radii = FVector(1.0f, 1.0f, 1.0f);

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config")
	float VoxelSize = 50.0f;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config")
	int32 MaxVoxelNum = 1000;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Appearance")
	TObjectPtr<UIVSmokeSmokePreset> SmokePresetOverride;
#pragma endregion

	//~==============================================================================
	// Flood Fill Simulation
#pragma region Simulation
public:
	// @todo Documentation
	UFUNCTION(BlueprintCallable, Category = "IVSmoke")
	void Initialize();

	// request it to server and server will broadcast.
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IVSmoke")
	void RequestStartSimulation();

	// Starts the smoke simulation called by server via Multicast after RequestStartSimulation.
	UFUNCTION(NetMulticast, Reliable)
	void StartSimulation(int32 InRandomSeed);

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Simulation")
	float ExpansionDuration = 3.0f;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Simulation")
	float SustainDuration = 5.0f;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Simulation")
	float DissipationDuration = 2.0f;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Simulation")
	float ExpansionNoise = 100.0f;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Simulation")
	float DissipationNoise = 100.0f;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Simulation")
	TObjectPtr<UCurveFloat> ExpansionCurve;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Simulation")
	TObjectPtr<UCurveFloat> DissipationCurve;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Simulation")
	TEnumAsByte<ECollisionChannel> VoxelCollisionChannel = ECC_WorldStatic;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Simulation")
	float CollisionExtentScale = 0.3f;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Simulation", meta = (ClampMin = "0"))
	int32 RandomSeed = 32687;

private:
	// @todo Documentation
	struct FIVSmokeVoxelNode
	{
		int32 Index;
		int32 ParentIndex;
		float Cost;
		bool operator<(const FIVSmokeVoxelNode& Other) const { return Cost < Other.Cost; }
	};

	FORCEINLINE static float GetCurveValue(float ElapsedTime, float Duration, const UCurveFloat* Curve)
	{
		if (ElapsedTime <= KINDA_SMALL_NUMBER)
		{
			return 1.0f;
		}

		float Alpha = FMath::Clamp(ElapsedTime / Duration, 0.0f, 1.0f);
		if (Curve)
		{
			return FMath::Clamp(Curve->GetFloatValue(Alpha), 0.0f, 1.0f);
		}
		return Alpha;
	}

	// @todo Documentation
	bool IsVoxelBlocked(const UWorld* World, const FVector& WorldPos) const;

	// @todo Documentation
	bool IsConnectionBlocked(const UWorld* World, const FVector& BeginPos, const FVector& EndPos) const;

	// @todo Documentation
	void UpdateExpansion(float DeltaTime);

	// @todo Documentation
	void UpdateSustain(float DeltaTime);

	// @todo Documentation
	void UpdateDissipation(float DeltaTime);

	// @todo Documentation
	void ProcessExpansion(int32 VoxelNum);

	// @todo Documentation
	void PrepareDissipation(int32 VoxelNum);

	// @todo Documentation
	void ProcessDissipation(int32 VoxelNum);

	// @todo Documentation
	EIVSmokeVoxelVolumeState CurrentState = EIVSmokeVoxelVolumeState::Idle;

	// @todo Documentation
	float ElapsedTime = 0.0f;

	// @todo Documentation
	TArray<float> VoxelArray;

	// @todo Documentation
	TArray<float> VoxelCostArray;

	// @todo Documentation
	TArray<FIVSmokeVoxelNode> MinHeap;

	// @todo Documentation
	TArray<int32> GeneratedVoxelIndices;

	// @todo Documentation
	bool bIsInitialized = false;

	// @todo Documentation
	FRandomStream RandomStream;

	// @todo Documentation
	FVector VoxelWorldAABBMin;

	// @todo Documentation
	FVector VoxelWorldAABBMax;

#pragma endregion

	//~==============================================================================
	// Collision
#pragma region Collision
public:
	// @todo Documentation
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Collision")
	float MinCollisionUpdateTimeInterval = 0.5f;

	// @todo Documentation
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Collision", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float MinCollisionUpdateProgressInterval = 0.25f;

private:
	/** @todo Documentation */
	void TryUpdateCollision(float CurrentProgress, bool bForceUpdate = false);

	// @todo Documentation
	float LastCollisionUpdateTime = 0.0f;

	// @todo Documentation
	float LastCollisionUpdateProgress = 0.0f;

#pragma endregion

	//~==============================================================================
	// Data Access
#pragma region DataAccess
public:
	/** Returns the voxel density array for GPU upload. Values are continuous (0.0~N). */
	FORCEINLINE const TArray<float>& GetVoxelArray() const { return VoxelArray; }

	/** Returns the grid resolution (dimensions of the voxel grid). */
	FORCEINLINE FIntVector GetGridResolution() const { return GridResolution; }

	/** Returns the center offset for grid-to-local coordinate conversion. */
	FORCEINLINE FIntVector GetCenterOffset() const { return CenterOffset; }

	/** Returns the world-space size of each voxel. */
	FORCEINLINE float GetVoxelSize() const { return VoxelSize; }

	/** Returns the current dirty level for GPU buffer synchronization. */
	FORCEINLINE EIVSmokeDirtyLevel GetDirtyLevel() const { return DirtyLevel; }

	/** Returns true if voxel data has been modified since last GPU upload. */
	FORCEINLINE bool IsVoxelDataDirty() const { return DirtyLevel != EIVSmokeDirtyLevel::Clean; }

	/** Clears the dirty flag after GPU upload. Called by renderer. */
	FORCEINLINE void ClearVoxelDataDirty() { DirtyLevel = EIVSmokeDirtyLevel::Clean; }

	/** Returns the current buffer size (for detecting resize). */
	FORCEINLINE int32 GetVoxelBufferSize() const { return VoxelArray.Num(); }

	/** Returns the number of active (non-zero density) voxels. */
	FORCEINLINE int32 GetActiveVoxelCount() const { return ActiveVoxelCount; }

	/** Returns the smoke preset override for this volume, or nullptr to use default. */
	FORCEINLINE const UIVSmokeSmokePreset* GetSmokePresetOverride() const { return SmokePresetOverride; }

	/** Returns the AABBMin of voxels. */
	FORCEINLINE const FVector GetVoxelWorldAABBMin() const { return VoxelWorldAABBMin - VoxelSize; }

	/** Returns the AABBMax of voxels. */
	FORCEINLINE const FVector GetVoxelWorldAABBMax() const { return VoxelWorldAABBMax + VoxelSize; }

	FTextureRHIRef GetHoleTexture() const;

private:
	/**
	 * Sets voxel density at the given grid position.
	 * @param GridPos   Grid position to set
	 * @param Density   Density value (0.0 = remove, >0 = active)
	 */
	void SetVoxelDensity(const FIntVector& GridPos, float Density);

	/**
	 * Sets voxel density at the given linear index.
	 * @param LinearIndex   Linear index in dense array
	 * @param Density       Density value (0.0 = remove, >0 = active)
	 */
	void SetVoxelDensityByIndex(int32 LinearIndex, float Density);

	// @todo Documentation
	FIntVector GridResolution = FIntVector::ZeroValue;

	// @todo Documentation
	FIntVector CenterOffset = FIntVector::ZeroValue;

	// @todo Documentation
	int32 ActiveVoxelCount = 0;

	// @todo Documentation
	EIVSmokeDirtyLevel DirtyLevel = EIVSmokeDirtyLevel::Clean;
#pragma endregion

	//~==============================================================================
	// Debug
#pragma region Debug
public:
	/** @todo Documentation */
	UFUNCTION(CallInEditor, Category = "IVSmoke | Debug")
	void PreviewSimulation();

	/** @todo Documentation */
	UFUNCTION(CallInEditor, Category = "IVSmoke | Debug")
	void ResetSimulation();

	// @todo Documentation (seconds)
	UPROPERTY(EditDefaultsOnly, Category = "IVSmoke | Debug")
	TObjectPtr<UStaticMesh> DebugVoxelMesh;

	// @todo Documentation (seconds)
	UPROPERTY(EditDefaultsOnly, Category = "IVSmoke | Debug")
	TObjectPtr<UMaterialInterface> DebugVoxelMaterial;

	// @todo Documentation (seconds)
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Debug", meta=(ShowOnlyInnerProperties))
	FIVSmokeDebugSettings DebugSettings;

private:
	/** @todo Documentation */
	void DrawDebugVisualization() const;

	/** @todo Documentation */
	void DrawDebugBounds() const;

	/** @todo Documentation */
	void DrawDebugVoxelWireframes() const;

	/** @todo Documentation */
	void DrawDebugVoxelMeshes() const;

	/** @todo Documentation */
	void DrawDebugStatusText() const;

	// @todo Documentation
	bool bIsEditorPreviewing = false;
#pragma endregion
};
