// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "IVSmokeGridLibrary.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectMacros.h"
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

USTRUCT(BlueprintType)
struct FIVSmokeServerState
{
	GENERATED_BODY()

	UPROPERTY()
	EIVSmokeVoxelVolumeState State = EIVSmokeVoxelVolumeState::Idle;

	UPROPERTY()
	float ExpansionStartTime = 0.0f;

	UPROPERTY()
	float SustainStartTime = 0.0f;

	UPROPERTY()
	float DissipationStartTime = 0.0f;

	UPROPERTY()
	int32 RandomSeed = 0;

	UPROPERTY()
	uint8 Generation = 0;
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
	TObjectPtr<UIVSmokeHoleGeneratorComponent> GetHoleGeneratorComponent();

	// @todo Documentation
	TObjectPtr<UIVSmokeCollisionComponent> GetCollisionComponent();

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

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config")
	bool bIsInfinite = false;

	// @todo Documentation
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Config")
	bool bAutoStart = false;

	// @todo Documentation
	UPROPERTY(EditAnywhere, Category = "IVSmoke | Config")
	bool bDestroyOnFinish = false;

#pragma endregion

	//~==============================================================================
	// Flood Fill Simulation
#pragma region Simulation
public:
	// @todo Documentation
	UFUNCTION(BlueprintCallable, Category = "IVSmoke")
	void Initialize();

	// @todo Documentation
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IVSmoke")
	void StartSimulation();

	// @todo Documentation
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IVSmoke")
	void StopSimulation(bool bImmediate = false);

	// @todo Documentation
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "IVSmoke")
	void ResetSimulation();

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Simulation")
	float ExpansionDuration = 3.0f;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Simulation")
	float FadeInDuration = 2.0f;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Simulation")
	float SustainDuration = 5.0f;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Simulation")
	float DissipationDuration = 2.0f;

	// @todo Documentation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Config | Simulation")
	float FadeOutDuration = 2.0f;

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
		bool operator<(const FIVSmokeVoxelNode& Other) const
		{
			if (FMath::IsNearlyEqual(Cost, Other.Cost))
			{
				return Index < Other.Index;
			}
			return Cost < Other.Cost;
		}
	};

	FORCEINLINE static float GetCurveValue(float ElapsedTime, float Duration, const UCurveFloat* Curve)
	{
		if (Duration <= KINDA_SMALL_NUMBER)
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
	UFUNCTION()
	void OnRep_ServerState();

	// @todo Documentation
	void HandleStateTransition(EIVSmokeVoxelVolumeState NewState);

	// @todo Documentation
	UFUNCTION(BlueprintCallable, Category = "IVSmoke")
	void ClearSimulationData();

	// @todo Documentation
	bool IsVoxelBlocked(const UWorld* World, const FVector& WorldPos) const;

	// @todo Documentation
	bool IsConnectionBlocked(const UWorld* World, const FVector& BeginPos, const FVector& EndPos) const;

	// @todo Documentation
	void FastForwardSimulation();

	// @todo Documentation
	void UpdateExpansion();

	// @todo Documentation
	void UpdateSustain();

	// @todo Documentation
	void UpdateDissipation();

	// @todo Documentation
	void ProcessExpansion(int32 SpawnNum, float StartSimTime, float EndSimTime);

	// @todo Documentation
	void ProcessDissipation(int32 RemoveNum, float StartSimTime, float EndSimTime);

	// @todo Documentation
	float SimTime = 0.0f;

	// @todo Documentation
	TArray<float> VoxelBirthTimes;

	// @todo Documentation
	TArray<float> VoxelDeathTimes;

	// @todo Documentation
	TArray<float> VoxelCosts;

	// @todo Documentation
	TArray<uint64> VoxelBits;

	// @todo Documentation
	TArray<FIVSmokeVoxelNode> ExpansionHeap;

	// @todo Documentation
	TArray<FIVSmokeVoxelNode> DissipationHeap;

	// @todo Documentation
	TArray<int32> GeneratedVoxelIndices;

	// @todo Documentation
	bool bIsInitialized = false;

	// @todo Documentation
	bool bIsFastForwarding = false;

	// @todo Documentation
	FVector VoxelWorldAABBMin;

	// @todo Documentation
	FVector VoxelWorldAABBMax;

	UPROPERTY(ReplicatedUsing = OnRep_ServerState)
	FIVSmokeServerState ServerState;

	// @todo Documentation
	FRandomStream RandomStream;

	// @todo Documentation
	uint8 LocalGeneration = 0;

#pragma endregion

	//~==============================================================================
	// Collision
#pragma region Collision
	// @todo Documentation
	void TryUpdateCollision(bool bForce = false);
#pragma endregion

	//~==============================================================================
	// Data Access
#pragma region DataAccess
public:
	/** @todo Documentation */
	UFUNCTION(BlueprintPure, Category = "IVSmoke")
	FORCEINLINE EIVSmokeVoxelVolumeState GetCurrentState() const { return ServerState.State; }

	/** @todo Documentation */
	FORCEINLINE const TArray<float>& GetVoxelBirthTimes() const { return VoxelBirthTimes; }

	/** @todo Documentation */
	FORCEINLINE const TArray<float>& GetVoxelDeathTimes() const { return VoxelDeathTimes; }

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
	FORCEINLINE int32 GetVoxelBufferSize() const { return VoxelBirthTimes.Num(); }

	/** Returns the number of active (non-zero density) voxels. */
	FORCEINLINE int32 GetActiveVoxelNum() const { return ActiveVoxelNum; }

	/** Returns the smoke preset override for this volume, or nullptr to use default. */
	FORCEINLINE const UIVSmokeSmokePreset* GetSmokePresetOverride() const { return SmokePresetOverride; }

	/** Returns the AABBMin of voxels. */
	FORCEINLINE FVector GetVoxelWorldAABBMin() const { return VoxelWorldAABBMin - VoxelSize; }

	/** Returns the AABBMax of voxels. */
	FORCEINLINE FVector GetVoxelWorldAABBMax() const { return VoxelWorldAABBMax + VoxelSize; }

	/** @todo Documentation */
	FORCEINLINE bool IsVoxelActive(int32 Index) const
	{
		FIntVector GridPos = UIVSmokeGridLibrary::IndexToGrid(Index, GridResolution);
		return IsVoxelActive(GridPos);
	}

	/** @todo Documentation */
	FORCEINLINE bool IsVoxelActive(FIntVector GridPos) const
	{
		return UIVSmokeGridLibrary::IsVoxelBitSet(VoxelBits, GridPos, GridResolution);
	}

	/** @todo Documentation */
	FTextureRHIRef GetHoleTexture() const;

	// @todo Documentation
	float GetSyncWorldTimeSeconds() const;

private:
	// @todo Documentation
	void SetVoxelBirthTime(int32 Index, float BirthTime);

	// @todo Documentation
	void SetVoxelDeathTime(int32 Index, float DeathTime);

	// @todo Documentation
	FIntVector GridResolution = FIntVector::ZeroValue;

	// @todo Documentation
	FIntVector CenterOffset = FIntVector::ZeroValue;

	// @todo Documentation
	int32 ActiveVoxelNum = 0;

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

	/** @todo Documentation */
	void UpdateVisualLogger() const;

	/** @todo Documentation */
	uint32 CalculateSimulationChecksum() const;

	// @todo Documentation
	bool bIsEditorPreviewing = false;

	// @todo Documentation
	EIVSmokeVoxelVolumeState LocalState = EIVSmokeVoxelVolumeState::Idle;
#pragma endregion
};
