// Copyright Epic Games, Inc. All Rights Reserved.

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Noise")
	int32 Seed = 0;

	/** Texture resolution (TexSize x TexSize x TexSize). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Noise", meta = (ClampMin = "16", ClampMax = "512"))
	int32 TexSize = 128;

	/** Number of noise octaves for detail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Noise", meta = (ClampMin = "1", ClampMax = "8"))
	int32 Octaves = 6;

	/** Noise wrap factor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Noise", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Wrap = 0.76f;

	/** Noise amplitude. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Noise", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Amplitude = 0.62f;

	/** Number of cells per axis for Worley noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Noise", meta = (ClampMin = "1", ClampMax = "16"))
	int32 AxisCellCount = 4;

	/** Size of each cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IVSmoke|Noise", meta = (ClampMin = "8", ClampMax = "128"))
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
	// Noise Generation
	// ============================================================================

	/** Global noise settings for smoke texture generation. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Noise")
	FIVSmokeNoiseSettings NoiseSettings;

	/** Whether to regenerate noise texture on startup. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Noise")
	bool bRegenerateNoiseOnStartup = true;

	/** Noise UV multiplier for sampling. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Noise", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float NoiseUVMul = 1.0f;

	// ============================================================================
	// Appearance (Global)
	// ============================================================================

	/** Controls edge softness. Lower = softer edges. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Appearance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SmokeDensityFalloff = 0.2f;

	/** Scale for noise sampling. Affects smoke detail size. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Appearance", meta = (ClampMin = "1.0", ClampMax = "1000.0"))
	float SmokeSize = 128.0f;

	// ============================================================================
	// Animation
	// ============================================================================

	/** Wind direction and speed for smoke animation. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Animation")
	FVector WindDirection = FVector(0.01f, 0.02f, 0.1f);

	// ============================================================================
	// Ray Marching
	// ============================================================================

	/** Maximum ray marching steps. Higher = better quality, lower performance. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|RayMarching", meta = (ClampMin = "16", ClampMax = "512"))
	int32 MaxSteps = 256;

	// ============================================================================
	// Post Processing
	// ============================================================================

	/**
	 * Controls sharpening/blurring of the smoke composite.
	 * Positive = sharpen (enhances detail but may show grain)
	 * Zero = no filter (default)
	 * Negative = blur (reduces grain but softens edges)
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|PostProcessing", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Sharpness = 0.0f;

	// ============================================================================
	// Rayleigh Scattering
	// ============================================================================

	/** Enable Rayleigh scattering for atmospheric light effects. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Scattering")
	bool bEnableScattering = true;

	/** Scattering intensity multiplier. Higher = more light scattered through smoke. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Scattering", meta = (ClampMin = "0.0", ClampMax = "10.0", EditCondition = "bEnableScattering"))
	float ScatterScale = 0.5f;

	/** Anisotropy parameter for Henyey-Greenstein phase function.
	 *  0 = isotropic, positive = forward scattering, negative = backward scattering */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Scattering", meta = (ClampMin = "-0.99", ClampMax = "0.99", EditCondition = "bEnableScattering"))
	float ScatteringAnisotropy = 0.5f;

	/** Override light direction instead of using scene directional light. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Scattering")
	bool bOverrideLightDirection = false;

	/** Custom light direction (normalized). Used when bOverrideLightDirection is true. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Scattering", meta = (EditCondition = "bOverrideLightDirection"))
	FVector LightDirectionOverride = FVector(0.0f, 0.0f, 1.0f);

	/** Override light color instead of using scene directional light. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Scattering")
	bool bOverrideLightColor = false;

	/** Custom light color. Used when bOverrideLightColor is true. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|Scattering", meta = (EditCondition = "bOverrideLightColor"))
	FLinearColor LightColorOverride = FLinearColor::White;

	// ============================================================================
	// Self-Shadowing (Light Marching)
	// ============================================================================

	/** Enable self-shadowing (light marching) for more realistic smoke appearance. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|SelfShadowing")
	bool bEnableSelfShadowing = true;

	/** Number of steps for light marching. Higher = better quality, lower performance.
	 *  Recommended: 4-8 for real-time rendering. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|SelfShadowing", meta = (ClampMin = "1", ClampMax = "16", EditCondition = "bEnableSelfShadowing"))
	int32 LightMarchingSteps = 6;

	/** Maximum distance to march toward light source (in world units).
	 *  Limits the light marching range for performance.
	 *  0 = No limit (march to volume boundary). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|SelfShadowing", meta = (ClampMin = "0.0", ClampMax = "500.0", EditCondition = "bEnableSelfShadowing"))
	float LightMarchingDistance = 0.0f;

	/** Exponential distribution factor for light marching steps.
	 *  Higher = more samples near the surface, fewer far away.
	 *  1.0 = uniform distribution, 2.0~3.0 = recommended for natural shadows. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|SelfShadowing", meta = (ClampMin = "1.0", ClampMax = "5.0", EditCondition = "bEnableSelfShadowing"))
	float LightMarchingExpFactor = 2.0f;

	/** Minimum brightness in fully shadowed areas.
	 *  0 = completely dark, 1 = no shadow effect. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|SelfShadowing", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableSelfShadowing"))
	float ShadowAmbient = 0.2f;

	// ============================================================================
	// Voxel FXAA
	// ============================================================================

	/** Maximum edge search distance for voxel anti-aliasing. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|VoxelFXAA", meta = (ClampMin = "1.0", ClampMax = "16.0"))
	float FXAASpanMax = 4.0f;

	/** Edge detection threshold range. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|VoxelFXAA", meta = (ClampMin = "0.5", ClampMax = "8.0"))
	float FXAARange = 3.5f;

	/** Sharpness factor for voxel anti-aliasing. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "IVSmoke|VoxelFXAA", meta = (ClampMin = "0.5", ClampMax = "8.0"))
	float FXAASharpness = 3.0f;

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
