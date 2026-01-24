// Copyright SDB. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "IVSmokeHoleData.generated.h"

struct FIVSmokeHoleArray;
class UIVSmokeHoleGeneratorComponent;
class UIVSmokeHolePreset;
struct FIVSmokeHoleGPU;

USTRUCT()
struct IVSMOKE_API FIVSmokeHoleDynamicSubject
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> TargetActor;

	UPROPERTY(Transient)
	uint8 PresetID = 0;

	UPROPERTY(Transient)
	FVector3f LastWorldPosition = FVector3f::ZeroVector;

	UPROPERTY(Transient)
	FQuat LastWorldRotation = FQuat::Identity;

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

	void PostReplicatedAdd(const FIVSmokeHoleArray& InArray);
	void PostReplicatedChange(const FIVSmokeHoleArray& InArray);
	void PreReplicatedRemove(const FIVSmokeHoleArray& InArray);

public:
	// World position where the hole starts
	UPROPERTY(Transient)
	FVector3f Position = FVector3f::ZeroVector;

	// World position where the penetration exits (Penetration only)
	UPROPERTY(Transient)
	FVector3f EndPosition = FVector3f::ZeroVector;

	// Hole expiration time (server based)
	UPROPERTY(Transient)
	float ExpirationServerTime = 0.0f;

	// Preset ID
	UPROPERTY(Transient)
	uint8 PresetID = 0;

	// Check if this hole has expired
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

	UPROPERTY(Transient, VisibleAnywhere)
	TArray<FIVSmokeHoleData> Items;

	// Owner component reference for replication callbacks
	UPROPERTY(Transient, NotReplicated)
	TObjectPtr<UIVSmokeHoleGeneratorComponent> OwnerComponent;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FIVSmokeHoleData, FIVSmokeHoleArray>(
			Items, DeltaParms, *this
		);
	}

	void AddHole(const FIVSmokeHoleData& NewHole)
	{
		Items.Add(NewHole);
		MarkItemDirty(Items.Last());
	}

	void RemoveAtSwap(const int32 Index)
	{
		if (Items.IsValidIndex(Index))
		{
			Items.RemoveAtSwap(Index);
			MarkArrayDirty();
		}
	}

	FORCEINLINE int32 Num() const { return Items.Num(); }
	FORCEINLINE bool IsValidIndex(const int32 Index) const { return Items.IsValidIndex(Index); }
	FORCEINLINE FIVSmokeHoleData& operator[](const int32 Index) { return Items[Index]; }
	FORCEINLINE const FIVSmokeHoleData& operator[](const int32 Index) const { return Items[Index]; }
	FORCEINLINE void Reserve(const int32 Number) { Items.Reserve(Number); }

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
	FIVSmokeHoleGPU(const FIVSmokeHoleData& HoleData, const UIVSmokeHolePreset& Preset);

	// ============================================================================
	// Common

	FVector3f Position;
	int32 HoleType;
	float Radius;
	float Lifetime;
	float EdgeSoftness;
	float NormalizedAge;

	// ============================================================================
	// Dynamic

	FVector3f Extent;

	// ============================================================================
	// Explosion

	float ExtensionTime; //0.07f * 2.4f;
	float DensityExtDelayTime;

	// ============================================================================
	// Penetration

	FVector3f EndPosition;

	void SetNormalizedAge(const float RemainingTime);
};
