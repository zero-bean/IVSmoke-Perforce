// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeSettings.h"
#include "IVSmokeRenderer.h"

UIVSmokeSettings::UIVSmokeSettings()
{
	// Default noise settings are initialized in FIVSmokeNoiseSettings
}

const UIVSmokeSettings* UIVSmokeSettings::Get()
{
	return GetDefault<UIVSmokeSettings>();
}

#if WITH_EDITOR
void UIVSmokeSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	// Refresh renderer's cached preset when DefaultSmokePreset changes
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UIVSmokeSettings, DefaultSmokePreset))
	{
		if (FIVSmokeRenderer::Get().IsInitialized())
		{
			FIVSmokeRenderer::Get().RefreshCachedPreset();
		}
	}
}
#endif
