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

	// Global settings are read directly from UIVSmokeSettings::Get() each frame,
	// so no manual refresh is needed when properties change.
}
#endif
