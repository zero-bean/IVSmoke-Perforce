// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SceneViewExtension.h"
#include "IVSmokeSmokePreset.h"
#include "IVSmokeSettings.generated.h"

/**
 * Post-processing pass where smoke is rendered.
 * Affects interaction with particles, DOF, Bloom, and other effects.
 */
UENUM(BlueprintType)
enum class EIVSmokeRenderPass : uint8
{
	/** Before Depth of Field. Best quality but particles may render on top. */
	BeforeDOF UMETA(DisplayName = "Before DOF (Best Quality)"),

	/** After Depth of Field. DOF applied to smoke. Recommended for most cases. */
	AfterDOF UMETA(DisplayName = "After DOF (Recommended)"),

	/** Translucency After DOF. Smoke renders over AfterDOF particles. Experimental. */
	TranslucencyAfterDOF UMETA(DisplayName = "Translucency After DOF (Experimental)"),

	/** After Motion Blur. Most effects applied but may cause edge artifacts. */
	MotionBlur UMETA(DisplayName = "After Motion Blur"),

	/** After Tonemapping. All particles rendered below, but no Bloom/DOF/TAA on smoke. */
	Tonemap UMETA(DisplayName = "After Tonemap (No Post Effects)")
};

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
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
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
	// Rendering
	// ============================================================================

	/** Post-processing pass where smoke is rendered.
	 *  Affects interaction with particles and post-process effects. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Rendering")
	EIVSmokeRenderPass RenderPass = EIVSmokeRenderPass::AfterDOF;

	/** Use CustomDepth for depth-based sorting between smoke and particles.
	 *  Compares smoke depth (from ray marching) with particle CustomDepth.
	 *  Requires: RenderPass = TranslucencyAfterDOF, particles must write to CustomDepth. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Rendering", meta = (DisplayName = "Use CustomDepth-Based Sorting", EditCondition = "RenderPass == EIVSmokeRenderPass::TranslucencyAfterDOF"))
	bool bUseCustomDepthBasedSorting = false;

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
