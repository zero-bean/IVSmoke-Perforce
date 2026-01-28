// Copyright (c) 2026, Team SDB. All rights reserved.

#include "IVSmokeSettings.h"
#include "IVSmokeRenderer.h"

UIVSmokeSettings::UIVSmokeSettings()
{

}

const UIVSmokeSettings* UIVSmokeSettings::Get()
{
	return GetDefault<UIVSmokeSettings>();
}

int32 UIVSmokeSettings::GetEffectiveMaxSteps() const
{
	switch (QualityLevel)
	{
	case EIVSmokeQualityLevel::Low:
		return 128;
	case EIVSmokeQualityLevel::Medium:
		return 256;
	case EIVSmokeQualityLevel::High:
		return 512;
	case EIVSmokeQualityLevel::Custom:
	default:
		return CustomMaxSteps;
	}
}

float UIVSmokeSettings::GetEffectiveMinStepSize() const
{
	switch (QualityLevel)
	{
	case EIVSmokeQualityLevel::Low:
		return 50.0f;
	case EIVSmokeQualityLevel::Medium:
		return 25.0f;
	case EIVSmokeQualityLevel::High:
		return 16.0f;
	case EIVSmokeQualityLevel::Custom:
	default:
		return CustomMinStepSize;
	}
}
UMaterialInterface* UIVSmokeSettings::GetSmokeVisualMaterial() const
{
	if (CachedSmokeVisualMaterial != nullptr)
	{
		return CachedSmokeVisualMaterial;
	}
	return nullptr;
}

#if WITH_EDITOR
void UIVSmokeSettings::PostInitProperties()
{
	Super::PostInitProperties();

	CachedSmokeVisualMaterial = Cast<UMaterialInterface>(SmokeVisualMaterial.TryLoad());
	if (CachedSmokeVisualMaterial == nullptr)
	{
		UE_LOG(LogTemp, Display, TEXT("Smoke visual material is none wtf"));
	}
}
void UIVSmokeSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Global settings are read directly from UIVSmokeSettings::Get() each frame,
	// so no manual refresh is needed when properties change.

	CachedSmokeVisualMaterial = Cast<UMaterialInterface>(SmokeVisualMaterial.TryLoad());
	
	UE_LOG(LogTemp, Display, TEXT("Smoke visual material change"));
	if (CachedSmokeVisualMaterial == nullptr)
	{
		UE_LOG(LogTemp, Display, TEXT("WHY NULL WTF"));	
	}
	
}
#endif
