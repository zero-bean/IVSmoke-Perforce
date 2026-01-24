// Copyright (c) 2026, Team SDB. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "IVSmokeHoleCarveCS.h"
#include "IVSmokeHoleData.generated.h"

struct FIVSmokeHoleArray;
class UIVSmokeHoleGeneratorComponent;
class UIVSmokeHolePreset;
struct FIVSmokeHoleGPU;

/**
 * @struct FIVSmokeHoleDynamicSubject
 * @brief Dynamic hole generated type data structure
 */
USTRUCT()
struct IVSMOKE_API FIVSmokeHoleDynamicSubject
{
	GENERATED_BODY()

	/** Dynamic actors to create holes */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> TargetActor;

	/** Preset ID. */
	UPROPERTY(Transient)
	uint8 PresetID = 0;

	/** Target last world position. */
	UPROPERTY(Transient)
	FVector3f LastWorldPosition = FVector3f::ZeroVector;

	/** Target last world rotation. */
	UPROPERTY(Transient)
	FQuat LastWorldRotation = FQuat::Identity;

	/** Check valid. */
	FORCEINLINE bool IsValid() const { return TargetActor.IsValid(); }
};

/**
 * @struct FIVSmokeHoleData
 * @brief Network-optimized hole data structure.
 */
USTRUCT(BlueprintType)
struct IVSMOKE_API FIVSmokeHoleData : public FFastArraySerializerItem
{
	GENERATED_BODY()

	FIVSmokeHoleData() = default;

	/** This function turns on the dirty flag. */
	void PostReplicatedAdd(const FIVSmokeHoleArray& InArray);

	/** This function turns on the dirty flag. */
	void PostReplicatedChange(const FIVSmokeHoleArray& InArray);

	/** This function turns on the dirty flag. */
	void PreReplicatedRemove(const FIVSmokeHoleArray& InArray);

public:

	/** World position where the hole starts. */
	UPROPERTY(Transient)
	FVector3f Position = FVector3f::ZeroVector;

	/** World position where the penetration exits. (Penetration only) */
	UPROPERTY(Transient)
	FVector3f EndPosition = FVector3f::ZeroVector;

	/** Hole expiration time (server based). */
	UPROPERTY(Transient)
	float ExpirationServerTime = 0.0f;

	/** Preset ID. */
	UPROPERTY(Transient)
	uint8 PresetID = 0;

	/** Check if this hole has expired. */
	FORCEINLINE bool IsExpired(const float CurrentServerTime) const { return CurrentServerTime >= ExpirationServerTime; }
};

/**
 * @struct FIVSmokeHoleArray
 * @brief Fast TArray container for delta replication of hole data.
 */
USTRUCT()
struct IVSMOKE_API FIVSmokeHoleArray : public FFastArraySerializer
{
	GENERATED_BODY()

	FIVSmokeHoleArray() : OwnerComponent(nullptr) {}

private:
	/** Hole data array. */
	UPROPERTY(Transient, VisibleAnywhere, Category = "IVSmoke | Hole")
	TArray<FIVSmokeHoleData> Items;

public:
	/** Owner component reference for replication callbacks. */
	UPROPERTY(Transient, NotReplicated)
	TObjectPtr<UIVSmokeHoleGeneratorComponent> OwnerComponent;

	/** FastArray delta replication entry point. */
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FIVSmokeHoleData, FIVSmokeHoleArray>(
			Items, DeltaParms, *this
		);
	}

	/** Add new hole and mark dirty. */
	void AddHole(const FIVSmokeHoleData& NewHole)
	{
		Items.Add(NewHole);
		MarkItemDirty(Items.Last());
	}

	/** Remove hole by swap and mark dirty. */
	void RemoveAtSwap(const int32 Index)
	{
		if (Items.IsValidIndex(Index))
		{
			Items.RemoveAtSwap(Index);
			MarkArrayDirty();
		}
	}

	/** Returns the hole num */
	FORCEINLINE int32 Num() const { return Items.Num(); }

	/** Returns the index item is valid. */
	FORCEINLINE bool IsValidIndex(const int32 Index) const { return Items.IsValidIndex(Index); }

	/** Returns the hole data at index. */
	FORCEINLINE FIVSmokeHoleData& operator[](const int32 Index) { return Items[Index]; }

	/** Returns the hole data at index. */
	FORCEINLINE const FIVSmokeHoleData& operator[](const int32 Index) const { return Items[Index]; }

	/** Reserve size items array */
	FORCEINLINE void Reserve(const int32 Number) { Items.Reserve(Number); }

	/** Converts items array into an array of GPU-compatible hole data structures. */
	TArray<FIVSmokeHoleGPU> GetHoleGPUData(const float CurrentServerTime) const;
};

// Enable delta serialization for FIVSmokeHoleArray
template<>
struct TStructOpsTypeTraits<FIVSmokeHoleArray> : public TStructOpsTypeTraitsBase2<FIVSmokeHoleArray>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};

/**
 * @struct FIVSmokeHoleGPU
 * @brief Built from FIVSmokeHoleData + UIVSmokeHolePreset at render time.
 */
struct alignas(16) FIVSmokeHoleGPU
{
	FIVSmokeHoleGPU() = default;

	/**
	 * You can Constructs a FIVSmokeHoleGPU using DynamicHoleData, Preset, and server time.
	 * @param DynamicHoleData		Dynamic hole data.
	 * @param Preset				HolePreset defined as DataAsset. 
	 * @param CurrentServerTime		The CurrentServerTime is obtained through the GetSyncedTime function.
	 */
	FIVSmokeHoleGPU(const FIVSmokeHoleData& DynamicHoleData, const UIVSmokeHolePreset& Preset, const float CurrentServerTime);

	//~============================================================================
	// Common

	/** The central point of hole creation. */
	FVector3f Position;

	/** Time after hole is called creation. */
	float CurLifeTime;

	/** 0 = Penetration, 1 = Explosion, 2 = Dynamic */
	int HoleType;

	/** Radius value used to calculate values related to the range. */
	float Radius;

	/** Total duration. */
	float Duration;

	/** Edge smooth range. */
	float Softness;

	//~============================================================================
	// Dynamic

	/** the size of a hole. */
	FVector3f Extent;

	float DynamicPadding;

	//~============================================================================
	// Explosion

	/** Expansion time used only for Explosion. */
	float ExpansionDuration;

	/** Current fadeRange extracted from ExpansionFadeRangeCurveOverTime with values normalized to expansion time. */
	float CurExpansionFadeRangeOverTime;

	/** Current fadeRange extracted from ShrinkFadeRangeCurveOverTime with values normalized to shrink time. */
	float CurShrinkFadeRangeOverTime;

	/** Current density extracted from ShrinkDensityMulCurveOverTime with values normalized to shrink time. */
	float CurShrinkDensityMulOverTime;

	/** Current distortion extracted from DistortionCurveOverTime with values normalized to expansion time. */
	float CurDistortionOverTime;

	/** Distortion degree max value. */
	float DistortionDistance;

	FVector2f PresetExplosionPadding;

	/** These values sampled from DistanceCurveOverDistance. */
	float DistortionCurveOverDistance[FIVSmokeHoleCarveCS::CurveSampleCount];

	//~============================================================================
	// Penetration

	/** The point at which the trajectory of the penetration ends. */
	FVector3f EndPosition;

	/** Radius at the end position. */
	float EndRadius;
};
