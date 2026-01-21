// Copyright SDB. All Rights Reserved.

#include "IVSmokeHoleData.h"
#include "IVSmokeHolePreset.h"
#include "IVSmokeHoleGeneratorComponent.h"

void FIVSmokeHoleData::PostReplicatedAdd(const FIVSmokeHoleArray& InArray)
{
	if (InArray.OwnerComponent)
	{
		InArray.OwnerComponent->MarkHoleTextureDirty();
	}
}

void FIVSmokeHoleData::PostReplicatedChange(const FIVSmokeHoleArray& InArray)
{
	if (InArray.OwnerComponent)
	{
		InArray.OwnerComponent->MarkHoleTextureDirty();
	}
}

void FIVSmokeHoleData::PreReplicatedRemove(const FIVSmokeHoleArray& InArray)
{
	if (InArray.OwnerComponent)
	{
		InArray.OwnerComponent->MarkHoleTextureDirty();
	}
}

TArray<FIVSmokeHoleGPU> FIVSmokeHoleArray::GetHoleGPUDatas(const float CurrentServerTime) const
{
	TArray<FIVSmokeHoleGPU> GPUBuffer;
	TArray<FIVSmokeHoleGPU> BulletBuffer;
	TArray<FIVSmokeHoleGPU> GrenadeBuffer;

	BulletBuffer.Reserve(FMath::Max(Num(), 1));
	GrenadeBuffer.Reserve(FMath::Max(Num(), 1));
	GPUBuffer.Reserve(FMath::Max(Num(), 1));

	for (const FIVSmokeHoleData& Hole : Items)
	{
		TObjectPtr<UIVSmokeHolePreset> Preset = UIVSmokeHolePreset::FindByID(Hole.PresetID);
		if (!Preset)
		{
			continue;
		}

		const float RemainingTime = Hole.ExpirationServerTime - CurrentServerTime;

		FIVSmokeHoleGPU GPUHole = FIVSmokeHoleGPU(Hole, *Preset.Get());
		GPUHole.SetNormalizedAge(RemainingTime);
		// Calculate normalized age (with division by zero protection)

		if (Preset->HoleType == EIVSmokeHoleType::Penetration)
		{
			BulletBuffer.Add(GPUHole);
		}
		else if (Preset->HoleType == EIVSmokeHoleType::Explosion)
		{
			GrenadeBuffer.Add(GPUHole);
		}
	}

	GPUBuffer.Append(GrenadeBuffer);
	GPUBuffer.Append(BulletBuffer);

	if (GPUBuffer.Num() == 0)
	{
		GPUBuffer.AddDefaulted(1);
	}

	return GPUBuffer;
}

FIVSmokeHoleGPU::FIVSmokeHoleGPU(const FIVSmokeHoleData& DynamicHoleData, const UIVSmokeHolePreset& Preset)
{
	Position = FVector3f(DynamicHoleData.Position);
	EndPosition = FVector3f(DynamicHoleData.EndPosition);

	HoleType = static_cast<int32>(Preset.HoleType);
	Radius = Preset.StartRadius;
	Lifetime = Preset.Lifetime;
	EdgeSoftness = Preset.EdgeSoftness;

	switch (Preset.HoleType)
	{
	case EIVSmokeHoleType::Explosion:
		ExtensionTime = Preset.ExtensionTime;
		DensityExtDelayTime = Preset.DensityExtDelayTime;
		break;
	case EIVSmokeHoleType::Penetration:
		EndRadius = Preset.EndRadius;
		DensityMultiplier = Preset.DensityMultiplier;
		break;
	}
}

void FIVSmokeHoleGPU::SetNormalizedAge(const float RemainingTime)
{
	const float ElapsedTime = Lifetime - RemainingTime;
	NormalizedAge = FMath::Clamp(ElapsedTime / Lifetime, 0.0f, 1.0f);
}
