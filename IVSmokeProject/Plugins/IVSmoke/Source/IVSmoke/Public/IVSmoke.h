// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

/** Log category for IVSmoke plugin */
DECLARE_LOG_CATEGORY_EXTERN(LogIVSmoke, Log, All);

DEFINE_LOG_CATEGORY_STATIC(LogIVSmokeVis, Log, All);

DECLARE_STATS_GROUP(TEXT("IVSmoke"), STATGROUP_IVSmoke, STATCAT_Advanced);

class FIVSmokeModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
