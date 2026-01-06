// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "IVSmokeSmokePreset.h"
#include "IVSmokeSettings.generated.h"

/**
 * Global settings for IVSmoke plugin.
 * Accessible via Project Settings > Plugins > IVSmoke.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "IVSmoke"))
class IVSMOKE_API UIVSmokeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UIVSmokeSettings();

	/** Get the singleton settings instance. */
	static const UIVSmokeSettings* Get();

	// ============================================================================
	// UDeveloperSettings Interface
	// ============================================================================

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("IVSmoke"); }

#if WITH_EDITOR
	virtual FText GetSectionText() const override { return NSLOCTEXT("IVSmoke", "SettingsSection", "IVSmoke"); }
	virtual FText GetSectionDescription() const override { return NSLOCTEXT("IVSmoke", "SettingsDescription", "Configure IVSmoke volumetric smoke settings"); }
#endif

	// ============================================================================
	// Default Preset
	// ============================================================================

	/** Default smoke preset used when no override is specified. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Defaults")
	TSoftObjectPtr<UIVSmokeSmokePreset> DefaultSmokePreset;

	// ============================================================================
	// Noise Generation
	// ============================================================================

	/** Global noise settings for smoke texture generation. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Noise")
	FIVSmokeNoiseSettings NoiseSettings;

	/** Whether to regenerate noise texture on startup. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Noise")
	bool bRegenerateNoiseOnStartup = true;

	// ============================================================================
	// Performance
	// ============================================================================

	/** Enable smoke rendering globally. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Performance")
	bool bEnableSmokeRendering = true;

	/** Quality level (0 = Low, 1 = Medium, 2 = High). Affects ray march steps. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Performance", meta = (ClampMin = "0", ClampMax = "2"))
	int32 QualityLevel = 2;

	// ============================================================================
	// Debug
	// ============================================================================

	/** Show debug visualization for smoke volumes. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Debug")
	bool bShowDebugVolumes = false;
};
