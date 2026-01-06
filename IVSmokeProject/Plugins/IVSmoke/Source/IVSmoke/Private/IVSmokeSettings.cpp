// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeSettings.h"

UIVSmokeSettings::UIVSmokeSettings()
{
	// Default noise settings are initialized in FIVSmokeNoiseSettings
}

const UIVSmokeSettings* UIVSmokeSettings::Get()
{
	return GetDefault<UIVSmokeSettings>();
}
