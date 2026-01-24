// Copyright (c) 2026, Team SDB. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SceneViewExtension.h"
#include "IVSmokeSettings.generated.h"

/**
 * Noise generation settings for volumetric smoke.
 */
USTRUCT(BlueprintType)
struct IVSMOKE_API FIVSmokeNoiseSettings
{
	GENERATED_BODY()

	/** Random seed for noise generation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Noise")
	int32 Seed = 0;

	/** Texture resolution (TexSize x TexSize x TexSize). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Noise", meta = (ClampMin = "16", ClampMax = "512"))
	int32 TexSize = 128;

	/** Number of noise octaves for detail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Noise", meta = (ClampMin = "1", ClampMax = "8"))
	int32 Octaves = 6;

	/** Noise wrap factor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Noise", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Wrap = 0.76f;

	/** Noise amplitude. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Noise", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Amplitude = 0.62f;

	/** Number of cells per axis for Worley noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Noise", meta = (ClampMin = "1", ClampMax = "16"))
	int32 AxisCellCount = 4;

	/** Size of each cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke | Noise", meta = (ClampMin = "8", ClampMax = "128"))
	int32 CellSize = 32;
};

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
 * Quality level presets for volumetric smoke rendering.
 * Controls ray marching steps and minimum step size.
 */
UENUM(BlueprintType)
enum class EIVSmokeQualityLevel : uint8
{
	/** Fast performance, lower quality. MaxSteps=128, MinStepSize=50 */
	Low UMETA(DisplayName = "Low (Fast)"),

	/** Balanced quality and performance. MaxSteps=256, MinStepSize=25 */
	Medium UMETA(DisplayName = "Medium (Balanced)"),

	/** Best quality, higher cost. MaxSteps=512, MinStepSize=16 */
	High UMETA(DisplayName = "High (Quality)"),

	/** User-defined MaxSteps and MinStepSize. */
	Custom UMETA(DisplayName = "Custom")
};

/**
 * Global settings for IVSmoke plugin.
 * Accessible via Project Settings > Plugins > IVSmoke.
 *
 * These settings affect ALL smoke volumes globally.
 * For per-volume appearance (color, density), use UIVSmokeSmokePreset.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "IVSmoke"))
class IVSMOKE_API UIVSmokeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UIVSmokeSettings();

	/** Get the singleton settings instance. */
	static const UIVSmokeSettings* Get();

	//~==============================================================================
	// UDeveloperSettings Interface

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("IVSmoke"); }

#if WITH_EDITOR
	virtual FText GetSectionText() const override { return NSLOCTEXT("IVSmoke", "SettingsSection", "IVSmoke"); }
	virtual FText GetSectionDescription() const override { return NSLOCTEXT("IVSmoke", "SettingsDescription", "Configure IVSmoke volumetric smoke settings"); }
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	//~==============================================================================
	// Global

	/** Enable smoke rendering globally. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke")
	bool bEnableSmokeRendering = true;

	/** Show advanced options in all categories. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke")
	bool bShowAdvancedOptions = false;

	/** Quality preset for smoke rendering. Controls ray marching steps and step size. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke")
	EIVSmokeQualityLevel QualityLevel = EIVSmokeQualityLevel::Medium;

	/** Maximum ray marching steps (32-1024). Only used when QualityLevel is Custom. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke", meta = (ClampMin = "32", ClampMax = "1024", EditCondition = "QualityLevel==EIVSmokeQualityLevel::Custom", EditConditionHides))
	int32 CustomMaxSteps = 256;

	/** Minimum step size in world units (5-100). Only used when QualityLevel is Custom. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke", meta = (ClampMin = "5.0", ClampMax = "100.0", EditCondition = "QualityLevel==EIVSmokeQualityLevel::Custom", EditConditionHides))
	float CustomMinStepSize = 25.0f;

	/** Get effective MaxSteps based on QualityLevel. */
	int32 GetEffectiveMaxSteps() const;

	/** Get effective MinStepSize based on QualityLevel. */
	float GetEffectiveMinStepSize() const;

	//~==============================================================================
	// Noise

	/** Global noise settings for smoke texture generation. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Noise")
	FIVSmokeNoiseSettings NoiseSettings;

	/** Whether to regenerate noise texture on startup. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Noise")
	bool bRegenerateNoiseOnStartup = true;

	/** Noise UV multiplier for sampling. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Noise", meta = (ClampMin = "0.01", ClampMax = "10.0"))
	float NoiseUVMul = 0.42f;

	//~==============================================================================
	// Appearance

	/** Controls edge softness. Lower = softer edges. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Appearance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SmokeDensityFalloff = 0.2f;

	/** Scale for noise sampling. Affects smoke detail size. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Appearance", meta = (ClampMin = "1.0", ClampMax = "1000.0"))
	float SmokeSize = 128.0f;

	/** Wind direction and speed for smoke animation. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Appearance")
	FVector WindDirection = FVector(0.00f, 0.00f, 0.1f);

	/** Sharpening/blurring of the smoke composite.
	 *  Positive = sharpen, Zero = no filter, Negative = blur. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Appearance", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Sharpness = 0.0f;

	/** Volume edge range offset for density falloff. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Appearance", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bShowAdvancedOptions", EditConditionHides))
	float VolumeRangeOffset = 0.1;

	/** Noise-based edge fade offset. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Appearance", meta = (ClampMin = "-1.0", ClampMax = "1.0", EditCondition = "bShowAdvancedOptions", EditConditionHides))
	float VolumeEdgeNoiseFadeOffset = 0.04f;

	/** Edge fade sharpness factor. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Appearance", meta = (ClampMin = "0.1", ClampMax = "10.0", EditCondition = "bShowAdvancedOptions", EditConditionHides))
	float VolumeEdgeFadeSharpness = 3.5f;

	//~==============================================================================
	// Lighting

	/** Enable Rayleigh scattering for atmospheric light effects. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Lighting")
	bool bEnableScattering = true;

	/** Scattering intensity multiplier. Higher = more light scattered through smoke. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Lighting", meta = (ClampMin = "0.0", ClampMax = "10.0", EditCondition = "bEnableScattering", EditConditionHides))
	float ScatterScale = 0.5f;

	/** Anisotropy parameter for Henyey-Greenstein phase function.
	 *  0 = isotropic, positive = forward scattering, negative = backward scattering */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Lighting", meta = (ClampMin = "-0.99", ClampMax = "0.99", EditCondition = "bEnableScattering", EditConditionHides))
	float ScatteringAnisotropy = 0.5f;

	/** Override light direction instead of using scene directional light. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Lighting", meta = (EditCondition = "bShowAdvancedOptions", EditConditionHides))
	bool bOverrideLightDirection = false;

	/** Custom light direction (normalized). Used when bOverrideLightDirection is true. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Lighting", meta = (EditCondition = "bShowAdvancedOptions && bOverrideLightDirection", EditConditionHides))
	FVector LightDirectionOverride = FVector(0.0f, 0.0f, 1.0f);

	/** Override light color instead of using scene directional light. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Lighting", meta = (EditCondition = "bShowAdvancedOptions", EditConditionHides))
	bool bOverrideLightColor = false;

	/** Custom light color. Used when bOverrideLightColor is true. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Lighting", meta = (EditCondition = "bShowAdvancedOptions && bOverrideLightColor", EditConditionHides))
	FLinearColor LightColorOverride = FLinearColor::White;

	//~==============================================================================
	// Self-Shadowing (Light Marching)

	/** Enable self-shadowing (light marching) for more realistic smoke appearance. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | Self")
	bool bEnableSelfShadowing = true;

	/** Number of steps for light marching (1-16). Higher = better quality, lower performance. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | Self", meta = (ClampMin = "1", ClampMax = "16", EditCondition = "bEnableSelfShadowing", EditConditionHides))
	int32 LightMarchingSteps = 6;

	/** Minimum brightness in fully shadowed areas (0=dark, 1=no shadow). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | Self", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableSelfShadowing", EditConditionHides))
	float ShadowAmbient = 0.2f;

	/** Maximum distance to march toward light (0=no limit). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | Self", meta = (ClampMin = "0.0", ClampMax = "500.0", EditCondition = "bShowAdvancedOptions && bEnableSelfShadowing", EditConditionHides))
	float LightMarchingDistance = 0.0f;

	/** Exponential distribution factor for light marching steps (1=uniform, 2-3=recommended). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | Self", meta = (ClampMin = "1.0", ClampMax = "5.0", EditCondition = "bShowAdvancedOptions && bEnableSelfShadowing", EditConditionHides))
	float LightMarchingExpFactor = 2.0f;

	//~==============================================================================
	// External Shadows (Scene Capture)

	/** Enable external object shadows (trees, buildings) via scene capture. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | External")
	bool bEnableExternalShadowing = false;

	/** Number of shadow cascades (1-6). More cascades = smoother quality. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | External", meta = (ClampMin = "1", ClampMax = "6", EditCondition = "bEnableExternalShadowing", EditConditionHides))
	int32 NumShadowCascades = 4;

	/** Shadow map resolution per cascade. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | External", meta = (ClampMin = "256", ClampMax = "2048", EditCondition = "bEnableExternalShadowing", EditConditionHides))
	int32 CascadeResolution = 512;

	/** Maximum shadow distance in centimeters. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | External", meta = (ClampMin = "1000", ClampMax = "100000", EditCondition = "bEnableExternalShadowing", EditConditionHides))
	float ShadowMaxDistance = 50000.0f;

	/** Minimum brightness in externally shadowed areas (0=dark, 1=no shadow). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | External", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableExternalShadowing", EditConditionHides))
	float ExternalShadowAmbient = 0.3f;

	/** Enable Variance Shadow Maps for soft shadows. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | External", meta = (EditCondition = "bEnableExternalShadowing", EditConditionHides))
	bool bEnableVSM = true;

	/** VSM blur kernel radius (0=no blur). Higher = softer shadows. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | External", meta = (ClampMin = "0", ClampMax = "8", EditCondition = "bEnableExternalShadowing && bEnableVSM", EditConditionHides))
	int32 VSMBlurRadius = 2;

	/** Shadow depth bias to prevent shadow acne. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | External", meta = (ClampMin = "0.0", ClampMax = "100.0", EditCondition = "bShowAdvancedOptions && bEnableExternalShadowing", EditConditionHides))
	float ShadowDepthBias = 1.0f;

	/** Include skeletal meshes (characters) in shadow capture. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | External", meta = (EditCondition = "bShowAdvancedOptions && bEnableExternalShadowing", EditConditionHides))
	bool bCaptureSkeletalMeshes = false;

	/** Log/Linear cascade split blend (0=linear, 1=logarithmic). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | External", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bShowAdvancedOptions && bEnableExternalShadowing", EditConditionHides))
	float CascadeLogLinearBlend = 0.85f;

	/** Blend region at cascade boundaries (0-0.3). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | External", meta = (ClampMin = "0.0", ClampMax = "0.3", EditCondition = "bShowAdvancedOptions && bEnableExternalShadowing", EditConditionHides))
	float CascadeBlendRange = 0.1f;

	/** Minimum variance for VSM to prevent artifacts. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | External", meta = (ClampMin = "0.01", ClampMax = "100.0", EditCondition = "bShowAdvancedOptions && bEnableExternalShadowing && bEnableVSM", EditConditionHides))
	float VSMMinVariance = 1.0f;

	/** VSM light bleeding reduction (0=none). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | External", meta = (ClampMin = "0.0", ClampMax = "0.5", EditCondition = "bShowAdvancedOptions && bEnableExternalShadowing && bEnableVSM", EditConditionHides))
	float VSMLightBleedingReduction = 0.2f;

	/** Enable priority-based cascade updates for performance. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | External", meta = (EditCondition = "bShowAdvancedOptions && bEnableExternalShadowing", EditConditionHides))
	bool bEnablePriorityUpdate = true;

	/** Near cascade update interval (frames). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | External", meta = (ClampMin = "1", ClampMax = "4", EditCondition = "bShowAdvancedOptions && bEnableExternalShadowing && bEnablePriorityUpdate", EditConditionHides))
	int32 NearCascadeUpdateInterval = 1;

	/** Far cascade update interval (frames). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Shadows | External", meta = (ClampMin = "1", ClampMax = "16", EditCondition = "bShowAdvancedOptions && bEnableExternalShadowing && bEnablePriorityUpdate", EditConditionHides))
	int32 FarCascadeUpdateInterval = 4;

	//~==============================================================================
	// Post Processing (Voxel FXAA)

	/** FXAA maximum edge search distance. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | PostProcessing", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float FXAASpanMax = 4.0f;

	/** FXAA edge detection threshold range. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | PostProcessing", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float FXAARange = 1.2f;

	/** FXAA sharpness factor. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | PostProcessing", meta = (ClampMin = "0.1", ClampMax = "8.0"))
	float FXAASharpness = 1.7f;

	//~==============================================================================
	// Rendering

	/** Post-processing pass where smoke is rendered. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Rendering")
	EIVSmokeRenderPass RenderPass = EIVSmokeRenderPass::AfterDOF;

	/** Use CustomDepth for depth-based sorting with particles.
	 *  Only available when RenderPass = TranslucencyAfterDOF. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Rendering", meta = (DisplayName = "Use CustomDepth Sorting", EditCondition = "bShowAdvancedOptions && RenderPass==EIVSmokeRenderPass::TranslucencyAfterDOF", EditConditionHides))
	bool bUseCustomDepthBasedSorting = false;

	//~==============================================================================
	// Debug

	/** Show debug visualization for smoke volumes. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke | Debug")
	bool bShowDebugVolumes = false;
};
