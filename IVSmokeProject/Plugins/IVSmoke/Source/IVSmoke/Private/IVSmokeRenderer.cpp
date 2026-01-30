// Copyright (c) 2026, Team SDB. All rights reserved.

#include "IVSmokeRenderer.h"
#include "IVSmoke.h"
#include "IVSmokePostProcessPass.h"
#include "IVSmokeSettings.h"
#include "IVSmokeShaders.h"
#include "IVSmokeSmokePreset.h"
#include "IVSmokeVoxelVolume.h"
#include "IVSmokeCSMRenderer.h"
#include "IVSmokeVSMProcessor.h"
#include "IVSmokeRayMarchPipeline.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "SceneRenderTargetParameters.h"
#include "IVSmokeHoleGeneratorComponent.h"
#include "RenderGraphUtils.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Components/SceneComponent.h"
#include "RHI.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialRenderProxy.h"
#include "MaterialShader.h"
#include "MaterialShaderType.h"
#include "IVSmokeVisualMaterialPreset.h"

#if !UE_SERVER
FIVSmokeRenderer& FIVSmokeRenderer::Get()
{
	static FIVSmokeRenderer Instance;
	return Instance;
}

FIVSmokeRenderer::FIVSmokeRenderer() = default;

FIVSmokeRenderer::~FIVSmokeRenderer()
{
	// Destructor defined here where FIVSmokeCSMRenderer and FIVSmokeVSMProcessor are complete types
	Shutdown();
}

//~==============================================================================
// Lifecycle

void FIVSmokeRenderer::Initialize()
{
	if (NoiseVolume)
	{
		return; // Already initialized
	}

	CreateNoiseVolume();

	UE_LOG(LogIVSmoke, Log, TEXT("[FIVSmokeRenderer::Initialize] Renderer initialized. Global settings loaded from UIVSmokeSettings."));
}

void FIVSmokeRenderer::Shutdown()
{
	if (NoiseVolume)
	{
		NoiseVolume->RemoveFromRoot();
		NoiseVolume = nullptr;
	}
	ServerTimeOffset = 0;
	bServerTimeSynced = false;

	CleanupCSM();
}
FIntVector FIVSmokeRenderer::GetAtlasTexCount(const FIntVector& TexSize, const int32 TexCount, const int32 TexturePackInterval, const int32 TexturePackMaxSize) const
{
	int QuotientX = TexturePackMaxSize / (TexSize.X + TexturePackInterval);
	int QuotientY = TexturePackMaxSize / (TexSize.Y + TexturePackInterval);
	int QuotientZ = TexturePackMaxSize / (TexSize.Z + TexturePackInterval);

	FIntVector AtlasTexCount = FIntVector(1, 1, 1);
	if (QuotientX < TexCount)
	{
		AtlasTexCount.X = QuotientX;
	}
	else
	{
		AtlasTexCount.X = TexCount;
	}

	int CurTexCount = TexCount / QuotientX + (TexCount % QuotientX == 0 ? 0 : 1);
	if (QuotientY < CurTexCount)
	{
		AtlasTexCount.Y = QuotientY;
	}
	else
	{
		AtlasTexCount.Y = CurTexCount;
	}

	CurTexCount = CurTexCount / QuotientY + (CurTexCount % QuotientY == 0 ? 0 : 1);
	if (QuotientZ < CurTexCount)
	{
		// Warning: atlas size full
		AtlasTexCount.Z = QuotientZ;
	}
	else
	{
		AtlasTexCount.Z = CurTexCount;
	}
	return AtlasTexCount;
}

void FIVSmokeRenderer::InitializeCSM(UWorld* World)
{
	if (!World)
	{
		return;
	}

	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();
	if (!Settings || !Settings->IsExternalShadowingEnabled())
	{
		return;
	}

	// Create CSM renderer if not exists
	if (!CSMRenderer)
	{
		CSMRenderer = MakeUnique<FIVSmokeCSMRenderer>();
	}

	// Initialize with settings
	if (!CSMRenderer->IsInitialized())
	{
		CSMRenderer->Initialize(
			World,
			Settings->GetEffectiveNumCascades(),
			Settings->GetEffectiveCascadeResolution(),
			Settings->GetEffectiveShadowMaxDistance()
		);
	}

	// Create VSM processor if VSM is enabled
	if (Settings->bEnableVSM && !VSMProcessor)
	{
		VSMProcessor = MakeUnique<FIVSmokeVSMProcessor>();
	}
}

void FIVSmokeRenderer::CleanupCSM()
{
	if (CSMRenderer)
	{
		CSMRenderer->Shutdown();
		CSMRenderer.Reset();
	}

	VSMProcessor.Reset();
	LastCSMUpdateFrameNumber = 0;
	LastVSMProcessFrameNumber = 0;

	UE_LOG(LogIVSmoke, Log, TEXT("[FIVSmokeRenderer::CleanupCSM] CSM cleaned up"));
}

bool FIVSmokeRenderer::GetMainDirectionalLight(UWorld* World, FVector& OutDirection, FLinearColor& OutColor, float& OutIntensity)
{
	if (!World)
	{
		return false;
	}

	UDirectionalLightComponent* BestLight = nullptr;
	int32 BestIndex = INT_MAX;

	// Find the atmosphere sun light with lowest index (0 = sun, 1 = moon)
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		UDirectionalLightComponent* LightComp = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		if (LightComp && LightComp->IsUsedAsAtmosphereSunLight())
		{
			int32 Index = LightComp->GetAtmosphereSunLightIndex();
			if (Index < BestIndex)
			{
				BestIndex = Index;
				BestLight = LightComp;
			}
		}
	}

	// Fallback: first DirectionalLight found
	if (!BestLight)
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			BestLight = Cast<UDirectionalLightComponent>(It->GetLightComponent());
			if (BestLight)
			{
				break;
			}
		}
	}

	if (BestLight)
	{
		// Negate: Shader expects direction TOWARD the light, not FROM the light
		OutDirection = -BestLight->GetComponentRotation().Vector();
		OutColor = BestLight->GetLightColor();
		OutIntensity = BestLight->Intensity;
		return true;
	}

	return false;
}

void FIVSmokeRenderer::CreateNoiseVolume()
{
	constexpr int32 TexSize = FIVSmokeNoiseConfig::TexSize;

	// Create volume texture
	NoiseVolume = NewObject<UTextureRenderTargetVolume>();
	NoiseVolume->AddToRoot(); // Prevent GC
	NoiseVolume->Init(TexSize, TexSize, TexSize, EPixelFormat::PF_R16F);
	NoiseVolume->bCanCreateUAV = true;
	NoiseVolume->ClearColor = FLinearColor::Black;
	NoiseVolume->SRGB = false;
	NoiseVolume->UpdateResourceImmediate(true);

	// Cache noise volume size for stats
	CachedNoiseVolumeSize = CalculateImageBytes(TexSize, TexSize, TexSize, PF_R16F);

	// Run compute shader to generate noise
	FTextureRenderTargetResource* RenderTargetResource = NoiseVolume->GameThread_GetRenderTargetResource();
	if (!RenderTargetResource)
	{
		UE_LOG(LogIVSmoke, Error, TEXT("[FIVSmokeRenderer::CreateNoiseVolume] Failed to get render target resource"));
		return;
	}

	ENQUEUE_RENDER_COMMAND(IVSmokeGenerateNoise)(
		[RenderTargetResource](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef NoiseTexture = GraphBuilder.RegisterExternalTexture(
				CreateRenderTarget(RenderTargetResource->TextureRHI, TEXT("IVSmokeNoiseVolume"))
			);

			FRDGTextureUAVRef OutputUAV = GraphBuilder.CreateUAV(NoiseTexture);

			auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeNoiseGeneratorGlobalCS::FParameters>();
			Parameters->RWNoiseTex = OutputUAV;
			Parameters->TexSize = FUintVector3(FIVSmokeNoiseConfig::TexSize, FIVSmokeNoiseConfig::TexSize, FIVSmokeNoiseConfig::TexSize);
			Parameters->Octaves = FIVSmokeNoiseConfig::Octaves;
			Parameters->Wrap = FIVSmokeNoiseConfig::Wrap;
			Parameters->AxisCellCount = FIVSmokeNoiseConfig::AxisCellCount;
			Parameters->Amplitude = FIVSmokeNoiseConfig::Amplitude;
			Parameters->CellSize = FIVSmokeNoiseConfig::CellSize;
			Parameters->Seed = FIVSmokeNoiseConfig::Seed;

			TShaderMapRef<FIVSmokeNoiseGeneratorGlobalCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			FIntVector GroupCount(
				FMath::DivideAndRoundUp(FIVSmokeNoiseConfig::TexSize, 8),
				FMath::DivideAndRoundUp(FIVSmokeNoiseConfig::TexSize, 8),
				FMath::DivideAndRoundUp(FIVSmokeNoiseConfig::TexSize, 8)
			);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("IVSmokeNoiseGeneration"),
				Parameters,
				ERDGPassFlags::Compute,
				[Parameters, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
				{
					FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *Parameters, GroupCount);
				}
			);
			GraphBuilder.Execute();
		}
	);
}

const UIVSmokeSmokePreset* FIVSmokeRenderer::GetEffectivePreset(const AIVSmokeVoxelVolume* Volume) const
{
	// Check for volume-specific override first
	if (Volume)
	{
		const UIVSmokeSmokePreset* Override = Volume->GetSmokePresetOverride();
		if (Override)
		{
			return Override;
		}
	}

	// Fall back to CDO (Class Default Object) for default appearance values
	return GetDefault<UIVSmokeSmokePreset>();
}

//~==============================================================================
// Thread-Safe Render Data Preparation

FIVSmokePackedRenderData FIVSmokeRenderer::PrepareRenderData(const TArray<AIVSmokeVoxelVolume*>& InVolumes, const FVector& CameraPosition)
{
	// Must be called on Game Thread
	check(IsInGameThread());

	FIVSmokePackedRenderData Result;

	if (InVolumes.Num() == 0)
	{
		return Result;
	}

	// Lazy initialization on first render
	if (!IsInitialized())
	{
		Initialize();
	}

	// Detect world change (Editor ↔ PIE transition)
	// CSM captures are bound to a specific world, so we must cleanup when world changes
	UWorld* CurrentWorld = InVolumes[0] ? InVolumes[0]->GetWorld() : nullptr;
	if (CurrentWorld && LastRenderedWorld.Get() != CurrentWorld)
	{
		UE_LOG(LogIVSmoke, Log, TEXT("[FIVSmokeRenderer::PrepareRenderData] World changed. Cleaning up CSM and cached data."));
		CleanupCSM();
		CachedRenderData.Reset();
		bServerTimeSynced = false;
		LastRenderedWorld = CurrentWorld;
	}

	// Filter volumes if exceeding maximum supported count
	TArray<AIVSmokeVoxelVolume*> FilteredVolumes;
	if (InVolumes.Num() > MaxSupportedVolumes)
	{
		UE_LOG(LogIVSmoke, Warning,
			TEXT("[FIVSmokeRenderer::PrepareRenderData] Volume count (%d) exceeds maximum (%d). "
				 "Farthest volumes from camera will be excluded."),
			InVolumes.Num(), MaxSupportedVolumes
		);

		// Copy and sort by distance from camera (closest first)
		FilteredVolumes = InVolumes;
		FilteredVolumes.Sort([&CameraPosition](const AIVSmokeVoxelVolume& A, const AIVSmokeVoxelVolume& B)
		{
			const float DistA = FVector::DistSquared(CameraPosition, A.GetActorLocation());
			const float DistB = FVector::DistSquared(CameraPosition, B.GetActorLocation());
			return DistA < DistB;
		});

		// Keep only the closest MaxSupportedVolumes
		FilteredVolumes.SetNum(MaxSupportedVolumes);
	}

	const TArray<AIVSmokeVoxelVolume*>& VolumesToProcess = (FilteredVolumes.Num() > 0) ? FilteredVolumes : InVolumes;

	Result.VolumeCount = VolumesToProcess.Num();
	Result.VolumeDataArray.Reserve(Result.VolumeCount);
	Result.HoleTextures.Reserve(Result.VolumeCount);
	Result.HoleTextureSizes.Reserve(Result.VolumeCount);

	// Get resolution info from first valid volume
	for (AIVSmokeVoxelVolume* Volume : VolumesToProcess)
	{
		if (Volume)
		{
			Result.VoxelResolution = Volume->GetGridResolution();
			if (UIVSmokeHoleGeneratorComponent* HoleComp = Volume->GetHoleGeneratorComponent())
			{
				if (FTextureRHIRef HoleTex = HoleComp->GetHoleTextureRHI())
				{
					Result.HoleResolution = HoleTex->GetSizeXYZ();
				}
			}
			break;
		}
	}

	// Fallback for hole resolution
	if (Result.HoleResolution == FIntVector::ZeroValue)
	{
		Result.HoleResolution = FIntVector(64, 64, 64);
	}

	// Calculate packed buffer sizes
	const int32 TexturePackInterval = 4;
	TArray<float> VoxelIntervalData;
	VoxelIntervalData.Init(0, Result.VoxelResolution.X * Result.VoxelResolution.Y * TexturePackInterval);

	FIntVector VoxelAtlasResolution = FIntVector(
		Result.VoxelResolution.X,
		Result.VoxelResolution.Y,
		Result.VoxelResolution.Z * Result.VolumeCount + TexturePackInterval * (Result.VolumeCount - 1)
	);
	int32 TotalVoxelSize = VoxelAtlasResolution.X * VoxelAtlasResolution.Y * VoxelAtlasResolution.Z;
	Result.PackedVoxelBirthTimes.Reserve(TotalVoxelSize);
	Result.PackedVoxelDeathTimes.Reserve(TotalVoxelSize);

	// Collect data from all volumes (Game Thread - safe to access)
	for (int32 i = 0; i < VolumesToProcess.Num(); ++i)
	{
		AIVSmokeVoxelVolume* Volume = VolumesToProcess[i];
		if (!Volume)
		{
			continue;
		}

		//~==========================================================================
		// Copy VoxelArray data (Game Thread safe)
		const TArray<float>& VoxelBirthTimes = Volume->GetVoxelBirthTimes();
		Result.PackedVoxelBirthTimes.Append(VoxelBirthTimes);

		const TArray<float>& VoxelDeathTimes = Volume->GetVoxelDeathTimes();
		Result.PackedVoxelDeathTimes.Append(VoxelDeathTimes);

		if (i < VolumesToProcess.Num() - 1)
		{
			Result.PackedVoxelBirthTimes.Append(VoxelIntervalData);
			Result.PackedVoxelDeathTimes.Append(VoxelIntervalData);
		}

		//~==========================================================================
		// Hole Texture reference (RHI resources are thread-safe)
		if (UIVSmokeHoleGeneratorComponent* HoleComp = Volume->GetHoleGeneratorComponent())
		{
			FTextureRHIRef HoleTex = HoleComp->GetHoleTextureRHI();
			Result.HoleTextures.Add(HoleTex);
			if (HoleTex)
			{
				Result.HoleTextureSizes.Add(HoleTex->GetSizeXYZ());
			}
			else
			{
				Result.HoleTextureSizes.Add(FIntVector::ZeroValue);
			}
		}
		else
		{
			Result.HoleTextures.Add(nullptr);
			Result.HoleTextureSizes.Add(FIntVector::ZeroValue);
		}

		//~==========================================================================
		// Build GPU metadata
		const FIntVector GridRes = Volume->GetGridResolution();
		const FIntVector CenterOff = Volume->GetCenterOffset();
		const float VoxelSz = Volume->GetVoxelSize();
		const FTransform VolumeTransform = Volume->GetActorTransform();

		// Calculate AABB
		FVector HalfExtent = FVector(CenterOff) * VoxelSz;
		FVector LocalMin = -HalfExtent;
		FVector LocalMax = FVector(GridRes - CenterOff - FIntVector(1, 1, 1)) * VoxelSz;
		FBox LocalBox(LocalMin, LocalMax);
		FBox WorldBox = LocalBox.TransformBy(VolumeTransform);

		// Get preset data
		const UIVSmokeSmokePreset* Preset = GetEffectivePreset(Volume);

		// Build GPU data struct
		FIVSmokeVolumeGPUData GPUData;
		FMemory::Memzero(&GPUData, sizeof(GPUData));

		GPUData.VoxelSize = VoxelSz;
		GPUData.VoxelBufferOffset = Result.VoxelResolution.X * Result.VoxelResolution.Y *
			(Result.VoxelResolution.Z + TexturePackInterval) * i;
		GPUData.GridResolution = FIntVector3(GridRes.X, GridRes.Y, GridRes.Z);
		GPUData.VoxelCount = VoxelBirthTimes.Num();
		GPUData.CenterOffset = FVector3f(CenterOff.X, CenterOff.Y, CenterOff.Z);
		GPUData.VolumeWorldAABBMin = FVector3f(WorldBox.Min);
		GPUData.VolumeWorldAABBMax = FVector3f(WorldBox.Max);
		GPUData.VoxelWorldAABBMin = FVector3f(Volume->GetVoxelWorldAABBMin());
		GPUData.VoxelWorldAABBMax = FVector3f(Volume->GetVoxelWorldAABBMax());
		GPUData.FadeInDuration = Volume->FadeInDuration;
		GPUData.FadeOutDuration = Volume->FadeOutDuration;

		if (Preset)
		{
			GPUData.SmokeColor = FVector3f(Preset->SmokeColor.R, Preset->SmokeColor.G, Preset->SmokeColor.B);
			GPUData.Absorption = Preset->SmokeAbsorption;
			GPUData.DensityScale = Preset->VolumeDensity;
		}
		else
		{
			GPUData.SmokeColor = FVector3f(0.8f, 0.8f, 0.8f);
			GPUData.Absorption = 0.1f;
			GPUData.DensityScale = 1.0f;
		}

		Result.VolumeDataArray.Add(GPUData);
	}

	//~==========================================================================
	// Copy global settings parameters
	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();

	if (Settings)
	{
		// Post processing
		Result.Sharpness = Settings->Sharpness;

		// Ray marching
		Result.MaxSteps = Settings->GetEffectiveMaxSteps();

		// Appearance
		Result.GlobalAbsorption = 0.1f;  // Default, per-volume absorption from preset
		Result.SmokeSize = Settings->SmokeSize;
		Result.SmokeDensityFalloff = Settings->SmokeDensityFalloff;
		Result.WindDirection = Settings->WindDirection;
		Result.VolumeRangeOffset = Settings->VolumeRangeOffset;
		Result.VolumeEdgeNoiseFadeOffset = Settings->VolumeEdgeNoiseFadeOffset;
		Result.VolumeEdgeFadeSharpness = Settings->VolumeEdgeFadeSharpness;

		// Scattering
		Result.bEnableScattering = Settings->bEnableScattering;
		Result.ScatterScale = Settings->ScatterScale;
		Result.ScatteringAnisotropy = Settings->ScatteringAnisotropy;

		//Rendering
		if (Settings->GetVisualMaterialPreset())
		{
			Result.SmokeVisualMaterial = Settings->GetVisualMaterialPreset()->SmokeVisualMaterial.Get();
			Result.VisualAlphaType = Settings->GetVisualMaterialPreset()->VisualAlphaType;
			Result.AlphaThreshold = Settings->GetVisualMaterialPreset()->AlphaThreshold;
			Result.LowOpacityRemapThreshold = Settings->GetVisualMaterialPreset()->LowOpacityRemapThreshold;
		}

		// Get world from first volume (single lookup, reused for light detection and shadow capture)
		UWorld* World = (VolumesToProcess.Num() > 0 && VolumesToProcess[0]) ? VolumesToProcess[0]->GetWorld() : nullptr;

		// Light Direction and Color
		// Priority: Settings Override > World DirectionalLight > Default
		if (Settings->bOverrideLightDirection)
		{
			Result.LightDirection = Settings->LightDirectionOverride.GetSafeNormal();
			Result.LightIntensity = 1.0f;  // Override assumes full intensity
		}
		else
		{
			FVector AutoLightDir;
			FLinearColor AutoLightColor;
			float AutoLightIntensity;

			if (GetMainDirectionalLight(World, AutoLightDir, AutoLightColor, AutoLightIntensity))
			{
				Result.LightDirection = AutoLightDir;
				Result.LightIntensity = AutoLightIntensity;

				// Also use auto light color if not overridden
				if (!Settings->bOverrideLightColor)
				{
					Result.LightColor = AutoLightColor;
				}
			}
			else
			{
				// No directional light found - dark environment
				Result.LightDirection = FVector(0.0f, 0.0f, -1.0f);
				Result.LightIntensity = 0.0f;
				Result.LightColor = FLinearColor::Black;
			}
		}

		if (Settings->bOverrideLightColor)
		{
			Result.LightColor = Settings->LightColorOverride;
		}

		// Self-shadowing
		Result.bEnableSelfShadowing = Settings->IsSelfShadowingEnabled();
		Result.LightMarchingSteps = Settings->GetEffectiveLightMarchingSteps();
		Result.LightMarchingDistance = Settings->LightMarchingDistance;
		Result.LightMarchingExpFactor = Settings->LightMarchingExpFactor;
		Result.ShadowAmbient = Settings->ShadowAmbient;

		// External shadowing (CSM - Cascaded Shadow Maps)
		// Note: CSM is always used when external shadowing is enabled. NumCascades > 0 indicates active.
		Result.ShadowDepthBias = Settings->ShadowDepthBias;
		Result.ExternalShadowAmbient = Settings->ExternalShadowAmbient;

		// VSM settings
		Result.bEnableVSM = Settings->bEnableVSM;
		Result.VSMMinVariance = Settings->VSMMinVariance;
		Result.VSMLightBleedingReduction = Settings->VSMLightBleedingReduction;
		Result.CascadeBlendRange = Settings->CascadeBlendRange;

		// Skip shadow capture if we're already inside a shadow capture render pass (prevents infinite recursion)
		if (Settings->IsExternalShadowingEnabled() && VolumesToProcess.Num() > 0 && !bIsCapturingShadow)
		{
			// Per-frame guard: Only update once per actual engine frame
			// PrepareRenderData can be called multiple times per frame (multiple views)
			const uint32 CurrentFrameNumber = GFrameNumber;
			const bool bAlreadyUpdatedThisFrame = (LastCSMUpdateFrameNumber == CurrentFrameNumber);

			if (!bAlreadyUpdatedThisFrame && World)
			{
				LastCSMUpdateFrameNumber = CurrentFrameNumber;

				// Initialize CSM if needed
				InitializeCSM(World);

				if (CSMRenderer && CSMRenderer->IsInitialized())
				{
					// Set re-entry guard (safety measure)
					bIsCapturingShadow = true;

					// Get camera position from first volume's world (or use centroid of volumes)
					FVector CSMCameraPosition = FVector::ZeroVector;
					FVector CSMCameraForward = FVector(1.0f, 0.0f, 0.0f);

					// Try to get player camera position
					if (APlayerController* PC = World->GetFirstPlayerController())
					{
						if (APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
						{
							CSMCameraPosition = CameraManager->GetCameraLocation();
							CSMCameraForward = CameraManager->GetCameraRotation().Vector();
						}
					}

					// Update CSM with current frame
					CSMRenderer->Update(
						CSMCameraPosition,
						CSMCameraForward,
						Result.LightDirection,
						CurrentFrameNumber
					);

					bIsCapturingShadow = false;
				}
			}

			// Populate CSM data for shader (even if not updated this frame)
			if (CSMRenderer && CSMRenderer->IsInitialized() && CSMRenderer->HasValidShadowData())
			{
				Result.NumCascades = CSMRenderer->GetNumCascades();

				// Get split distances
				Result.CSMSplitDistances = CSMRenderer->GetSplitDistances();

				// Get textures, matrices, and light camera data for each cascade
				Result.CSMDepthTextures.SetNum(Result.NumCascades);
				Result.CSMVSMTextures.SetNum(Result.NumCascades);
				Result.CSMViewProjectionMatrices.SetNum(Result.NumCascades);
				Result.CSMLightCameraPositions.SetNum(Result.NumCascades);
				Result.CSMLightCameraForwards.SetNum(Result.NumCascades);

				for (int32 i = 0; i < Result.NumCascades; i++)
				{
					const FIVSmokeCascadeData& Cascade = CSMRenderer->GetCascade(i);
					// Single-buffer: VP matrix and texture are from the SAME frame
					Result.CSMViewProjectionMatrices[i] = Cascade.ViewProjectionMatrix;
					Result.CSMDepthTextures[i] = CSMRenderer->GetDepthTexture(i);
					Result.CSMVSMTextures[i] = CSMRenderer->GetVSMTexture(i);
					Result.CSMLightCameraPositions[i] = Cascade.LightCameraPosition;
					Result.CSMLightCameraForwards[i] = Cascade.LightCameraForward;
				}

				// Store the main camera position for consistent use in shader
				Result.CSMMainCameraPosition = CSMRenderer->GetMainCameraPosition();
			}
		}
	}

	Result.bIsValid = Result.VolumeDataArray.Num() && Result.PackedVoxelBirthTimes.Num() > 0 && Result.PackedVoxelDeathTimes.Num() > 0;

	if (VolumesToProcess.Num() > 0 && VolumesToProcess[0])
	{
		Result.GameTime = VolumesToProcess[0]->GetSyncWorldTimeSeconds();
	}
	else
	{
		Result.GameTime = 0.0f;
	}

	return Result;
}

//~==============================================================================
// Rendering

FScreenPassTexture FIVSmokeRenderer::Render(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	// Get scene color from inputs FIRST - needed for passthrough
	FScreenPassTextureSlice SceneColorSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
	if (!SceneColorSlice.IsValid())
	{
		return FScreenPassTexture();
	}

	FScreenPassTexture SceneColor(SceneColorSlice);

	// Get settings with null check
	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();
	if (!Settings)
	{
		return SceneColor;
	}

	// Helper lambda for TranslucencyAfterDOF passthrough
	// In this mode, we must return SeparateTranslucency (not SceneColor) to preserve translucent objects
	auto GetPassthroughTexture = [&]() -> FScreenPassTexture
		{
			if (Settings->RenderPass == EIVSmokeRenderPass::TranslucencyAfterDOF)
			{
				FScreenPassTextureSlice SeparateTranslucencySlice = Inputs.GetInput(EPostProcessMaterialInput::SeparateTranslucency);
				if (SeparateTranslucencySlice.IsValid())
				{
					return FScreenPassTexture(SeparateTranslucencySlice);
				}
			}
			return SceneColor;
		};

	// Check if rendering is enabled - passthrough if disabled
	if (!Settings->bEnableSmokeRendering)
	{
		return GetPassthroughTexture();
	}

	// Get cached render data (prepared on Game Thread via BeginRenderViewFamily)
	// Use copy instead of MoveTemp - multiple views in same frame share the same data
	FIVSmokePackedRenderData RenderData;
	{
		FScopeLock Lock(&RenderDataMutex);
		RenderData = CachedRenderData;  // Copy - don't consume, other views may need it
	}

	// Early out if no valid render data - avoid unnecessary texture allocations
	if (!RenderData.bIsValid)
	{
		return GetPassthroughTexture();
	}

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;

	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget(
			SceneColor.Texture,
			SceneColor.ViewRect,
			ERenderTargetLoadAction::ELoad
		);
	}

	// Use ViewRect size consistently for all passes
	const FIntPoint ViewportSize = SceneColor.ViewRect.Size();
	const FIntPoint ViewRectMin = SceneColor.ViewRect.Min;

	//~==========================================================================
	// Upscaling Pipeline (1/2 to Full)
	//
	// Ray March at 1/2 resolution for quality/performance balance.
	// Single-step upscaling with bilinear filtering smooths IGN grain.
	// Note: 1/4 resolution causes excessive grain when camera is inside smoke.
	//
	const FIntPoint HalfSize = FIntPoint(
		FMath::Max(1, ViewportSize.X / 2),
		FMath::Max(1, ViewportSize.Y / 2)
	);

	// Create Render Target textures at 1/2 resolution
	FRDGTextureRef SmokeAlbedoTex = FIVSmokePostProcessPass::CreateOutputTexture(GraphBuilder, SceneColor.Texture, TEXT("IVSmokeAlbedoTex_Half"), PF_FloatRGBA, HalfSize);
	FRDGTextureRef SmokeLocalPosAlphaTex = FIVSmokePostProcessPass::CreateOutputTexture(GraphBuilder, SceneColor.Texture, TEXT("IVSmokeLocalPosAlphaTex_Half"), PF_FloatRGBA, HalfSize);
	FRDGTextureRef SmokeWorldPosDepthTex = FIVSmokePostProcessPass::CreateOutputTexture(GraphBuilder, SceneColor.Texture, TEXT("IVSmokeWorldPosDepthTex_Half"), PF_FloatRGBA, HalfSize);

	// Update stats (1-second interval)
	UpdateStatsIfNeeded(RenderData, ViewportSize);

	//~==========================================================================
	// Ray March Pass (1/2 Resolution)
	// Multi-Volume Ray Marching with Occupancy Optimization (Three-Pass Pipeline).
	// Uses tile-based occupancy grid for efficient empty space skipping.
	AddMultiVolumeRayMarchPass(GraphBuilder, View, RenderData, SmokeAlbedoTex, SmokeLocalPosAlphaTex, SmokeWorldPosDepthTex, HalfSize, ViewportSize, ViewRectMin);

	//~==========================================================================
	// Upscaling (1/2 to Full)
	// Single-step bilinear upscaling smooths IGN grain patterns.
	FRDGTextureRef SmokeAlbedoFull = AddCopyPass(GraphBuilder, View, SmokeAlbedoTex, ViewportSize, TEXT("IVSmokeAlbedoTex_Full"));
	FRDGTextureRef SmokeLocalPosAlphaFull = AddCopyPass(GraphBuilder, View, SmokeLocalPosAlphaTex, ViewportSize, TEXT("IVSmokeLocalPosAlphaTex_Full"));
	FRDGTextureRef SmokeWorldPosDepthFull = AddCopyPass(GraphBuilder, View, SmokeWorldPosDepthTex, ViewportSize, TEXT("IVSmokeWorldPosDepthTex_Full"));

	//~==========================================================================
	// Upsample Filter Pass
	FRDGTextureRef SmokeTex = AddUpsampleFilterPass(GraphBuilder, RenderData, View, SceneColor.Texture, SmokeAlbedoFull, SmokeLocalPosAlphaFull, ViewportSize);

	//~==========================================================================
	// Visual Pass
	FRDGTextureRef SmokeVisualTex = AddSmokeVisualPass(GraphBuilder, RenderData, View, SmokeTex, SmokeLocalPosAlphaFull, SmokeWorldPosDepthFull, SceneColor.Texture, ViewportSize);

	//~==========================================================================
	// Composite Pass

	const bool bUseCustomDepthBasedSorting = Settings->bUseCustomDepthBasedSorting;
	const bool bTranslucencyMode = (Settings->RenderPass == EIVSmokeRenderPass::TranslucencyAfterDOF);
	FScreenPassTextureSlice SeparateTranslucencySlice = Inputs.GetInput(EPostProcessMaterialInput::SeparateTranslucency);

	if (bUseCustomDepthBasedSorting && bTranslucencyMode && SeparateTranslucencySlice.IsValid())
	{
		FScreenPassTexture ParticlesTex(SeparateTranslucencySlice);

		// Create output texture based on ParticlesTex (same as TranslucencyComposite)
		// TranslucencyAfterDOF mode expects output in SeparateTranslucency format
		FRDGTextureRef OutputTexture = FIVSmokePostProcessPass::CreateOutputTexture(GraphBuilder, ParticlesTex.Texture, TEXT("IVSmokeDepthSortedOutput"), PF_FloatRGBA);

		FScreenPassRenderTarget SortedOutput(OutputTexture, ParticlesTex.ViewRect, ERenderTargetLoadAction::ENoAction);

		// Pass texture extents for UV calculation (UV = SvPosition / TexExtent)
		const FIntPoint SmokeExtent = ViewportSize;

		AddDepthSortedCompositePass(GraphBuilder, RenderData, View, SmokeVisualTex,
			SmokeLocalPosAlphaFull, SmokeWorldPosDepthFull, ParticlesTex.Texture, SortedOutput, ViewportSize);

		return FScreenPassTexture(SortedOutput);
	}
	else if (bTranslucencyMode && SeparateTranslucencySlice.IsValid())
	{
		// TranslucencyAfterDOF mode: Composite smoke OVER particles
		FScreenPassTexture ParticlesTex(SeparateTranslucencySlice);

		// Smoke textures are rendered at SceneColor.ViewRect
		// Particles texture is at its own ViewRect (SeparateTranslucency)
		// These can differ! Shader handles separate UV calculation for each.

		// Create output texture with SAME SIZE as ParticlesTex
		FRDGTextureRef OutputTexture = FIVSmokePostProcessPass::CreateOutputTexture(GraphBuilder, ParticlesTex.Texture, TEXT("IVSmokeTranslucencyOutput"), PF_FloatRGBA);

		FScreenPassRenderTarget TranslucencyOutput(OutputTexture, ParticlesTex.ViewRect, ERenderTargetLoadAction::ENoAction);

		// Pass texture extents for UV calculation (UV = SvPosition / TexExtent)
		const FIntPoint ParticlesExtent(ParticlesTex.Texture->Desc.Extent.X, ParticlesTex.Texture->Desc.Extent.Y);


		AddTranslucencyCompositePass(GraphBuilder, RenderData, View, SmokeVisualTex, SmokeLocalPosAlphaFull,
			ParticlesTex.Texture, TranslucencyOutput, ParticlesExtent, ViewportSize);

		return FScreenPassTexture(TranslucencyOutput);
	}
	else
	{
		AddCompositePass(GraphBuilder, RenderData, View, SceneColor.Texture, SmokeVisualTex, SmokeLocalPosAlphaFull, Output, ViewportSize);
		return FScreenPassTexture(Output);
	}
}

//~==============================================================================
// Composite Pass Functions

void FIVSmokeRenderer::AddCompositePass(
	FRDGBuilder& GraphBuilder,
	const FIVSmokePackedRenderData& RenderData,
	const FSceneView& View,
	FRDGTextureRef SceneTex,
	FRDGTextureRef SmokeVisualTex,
	FRDGTextureRef SmokeLocalPosAlphaTex,
	const FScreenPassRenderTarget& Output,
	const FIntPoint& ViewportSize)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeCompositePS> PixelShader(ShaderMap);

	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeCompositePS::FParameters>();
	Parameters->SceneTex = SceneTex;
	Parameters->SmokeTex = SmokeVisualTex;
	Parameters->SmokeLocalPosAlphaTex = SmokeLocalPosAlphaTex;
	Parameters->LinearClamp_Sampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->ViewportSize = FVector2f(ViewportSize);
	Parameters->ViewRectMin = FVector2f(Output.ViewRect.Min);
	Parameters->AlphaType = (int)RenderData.VisualAlphaType;
	Parameters->AlphaThreshold = RenderData.AlphaThreshold;
	Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FIVSmokePostProcessPass::AddPixelShaderPass<FIVSmokeCompositePS>(GraphBuilder, ShaderMap, PixelShader, Parameters, Output);
}


void FIVSmokeRenderer::AddTranslucencyCompositePass(
	FRDGBuilder& GraphBuilder,
	const FIVSmokePackedRenderData& RenderData,
	const FSceneView& View,
	FRDGTextureRef SmokeVisualTex,
	FRDGTextureRef SmokeLocalPosAlphaTex,
	FRDGTextureRef ParticlesTex,
	const FScreenPassRenderTarget& Output,
	const FIntPoint& ParticlesTexExtent,
	const FIntPoint& ViewportSize)
{ 
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
		TShaderMapRef<FIVSmokeTranslucencyCompositePS> PixelShader(ShaderMap);
		
		auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeTranslucencyCompositePS::FParameters>();
		Parameters->SmokeVisualTex = SmokeVisualTex;
		Parameters->SmokeLocalPosAlphaTex = SmokeLocalPosAlphaTex;
		Parameters->ParticleSceneTex = ParticlesTex;
		Parameters->LinearClamp_Sampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->ParticlesTexExtent = FVector2f(ParticlesTexExtent);
		Parameters->ViewportSize = FVector2f(ViewportSize);
		Parameters->ViewRectMin = FVector2f(Output.ViewRect.Min);
		Parameters->AlphaType = (int)RenderData.VisualAlphaType;
		Parameters->AlphaThreshold = RenderData.AlphaThreshold;
		Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();
		
		FIVSmokePostProcessPass::AddPixelShaderPass<FIVSmokeTranslucencyCompositePS>(GraphBuilder, ShaderMap, PixelShader, Parameters, Output);
}

void FIVSmokeRenderer::AddDepthSortedCompositePass(
	FRDGBuilder& GraphBuilder,
	const FIVSmokePackedRenderData& RenderData,
	const FSceneView& View,
	FRDGTextureRef SmokeVisualTex,
	FRDGTextureRef SmokeLocalPosAlphaTex,
	FRDGTextureRef SmokeWorldPosDepthTex,
	FRDGTextureRef SeparateTranslucencyTex,
	const FScreenPassRenderTarget& Output,
	const FIntPoint& ViewportSize)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeDepthSortedCompositePS> PixelShader(ShaderMap);

	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeDepthSortedCompositePS::FParameters>();

	// Smoke layer (from ray marching CS)
	Parameters->SmokeVisualTex = SmokeVisualTex;
	Parameters->SmokeLocalPosAlphaTex = SmokeLocalPosAlphaTex;
	Parameters->SmokeWorldPosDepthTex = SmokeWorldPosDepthTex;

	// Particle layer (from Separate Translucency)
	Parameters->SeparateTranslucencyTex = SeparateTranslucencyTex;

	// Scene Textures (provides CustomDepth and SceneDepth via uniform buffer)
	Parameters->SceneTexturesStruct = GetSceneTextureShaderParameters(View).SceneTextures;

	// Samplers
	Parameters->PointClamp_Sampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->LinearClamp_Sampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	// Texture Extents for UV calculation (UV = SvPosition / TexExtent)
	Parameters->ViewportSize = FVector2f(ViewportSize);
	Parameters->ViewRectMin = Output.ViewRect.Min;
	Parameters->InvDeviceZToWorldZTransform = FVector4f(View.InvDeviceZToWorldZTransform);
	Parameters->AlphaType = (int)RenderData.VisualAlphaType;
	Parameters->AlphaThreshold = RenderData.AlphaThreshold;

	// Render target
	Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FIVSmokePostProcessPass::AddPixelShaderPass<FIVSmokeDepthSortedCompositePS>(GraphBuilder, ShaderMap, PixelShader, Parameters, Output);
}





//~==============================================================================
// Copy Pass (Progressive Upscaling)

FRDGTextureRef FIVSmokeRenderer::AddCopyPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef SourceTex,
	const FIntPoint& DestSize,
	const TCHAR* TexName)
{
	// Create destination texture at specified size
	FRDGTextureRef DestTex = FIVSmokePostProcessPass::CreateOutputTexture(
		GraphBuilder,
		SourceTex,
		TexName,
		PF_FloatRGBA,
		DestSize,
		TexCreate_RenderTargetable | TexCreate_ShaderResource
	);

	// Perform copy
	AddCopyPass(GraphBuilder, View, SourceTex, DestTex);

	return DestTex;
}

void FIVSmokeRenderer::AddCopyPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef SourceTex,
	FRDGTextureRef DestTex)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeCopyPS> CopyShader(ShaderMap);

	const FIntPoint DestSize = DestTex->Desc.Extent;

	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeCopyPS::FParameters>();
	Parameters->MainTex = SourceTex;
	Parameters->LinearRepeat_Sampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->ViewportSize = FVector2f(DestSize);
	Parameters->RenderTargets[0] = FRenderTargetBinding(DestTex, ERenderTargetLoadAction::ENoAction);

	FScreenPassRenderTarget Output(
		DestTex,
		FIntRect(0, 0, DestSize.X, DestSize.Y),
		ERenderTargetLoadAction::ENoAction
	);

	FIVSmokePostProcessPass::AddPixelShaderPass<FIVSmokeCopyPS>(GraphBuilder, ShaderMap, CopyShader, Parameters, Output);
}

//~==============================================================================
// Upsample Filtering Pass
FRDGTextureRef FIVSmokeRenderer::AddUpsampleFilterPass(FRDGBuilder& GraphBuilder, const FIVSmokePackedRenderData& RenderData, const FSceneView& View,
	FRDGTextureRef SceneTex, FRDGTextureRef SmokeAlbedo, FRDGTextureRef SmokeLocalPosAlpha, const FIntPoint& TexSize)
{
	FRDGTextureRef SmokeTex = FIVSmokePostProcessPass::CreateOutputTexture(
		GraphBuilder,
		SmokeAlbedo,
		TEXT("IVSmokeUpsampleFilterTex"),
		PF_FloatRGBA,
		TexSize
	);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeUpsampleFilterPS> PixelShader(ShaderMap);
	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeUpsampleFilterPS::FParameters>();
	Parameters->SceneTex = SceneTex;
	Parameters->SmokeAlbedoTex = SmokeAlbedo;
	Parameters->SmokeLocalPosAlphaTex = SmokeLocalPosAlpha;
	Parameters->LinearClamp_Sampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->Sharpness = RenderData.Sharpness;
	Parameters->ViewportSize = TexSize;
	Parameters->ViewRectMin = FVector2f(0, 0);
	Parameters->LowOpacityRemapThreshold = RenderData.LowOpacityRemapThreshold;
	Parameters->RenderTargets[0] = FRenderTargetBinding(SmokeTex, ERenderTargetLoadAction::ENoAction);
	FScreenPassRenderTarget Output(
		SmokeTex,
		FIntRect(0, 0, TexSize.X, TexSize.Y),
		ERenderTargetLoadAction::ENoAction
	);
	FIVSmokePostProcessPass::AddPixelShaderPass<FIVSmokeUpsampleFilterPS>(GraphBuilder, ShaderMap, PixelShader, Parameters, Output);

	return SmokeTex;
}

//~==============================================================================
// Smoke Visual Pass
FRDGTextureRef FIVSmokeRenderer::AddSmokeVisualPass(FRDGBuilder& GraphBuilder, const FIVSmokePackedRenderData& RenderData, const FSceneView& View, FRDGTextureRef SmokeTex, FRDGTextureRef SmokeLocalPosAlphaTex, FRDGTextureRef SmokeWorldPosDepthTex, FRDGTextureRef SceneTex, const FIntPoint& TexSize)
{
	UMaterialInterface* SmokeVisualMat = RenderData.SmokeVisualMaterial;

	if (SmokeVisualMat == nullptr)
	{
		// UE_LOG(LogIVSmoke, Display, TEXT("SmokeVisualMaterial is none"));
		return SmokeTex;
	}

	FPostProcessMaterialInputs PostProcessInputs;

	// SmokeTex → PostProcessInput0
	PostProcessInputs.SetInput(GraphBuilder, EPostProcessMaterialInput::SceneColor, FScreenPassTexture(SmokeTex));

	// SmokeLocalPosAlphaTex → PostProcessInput1
	PostProcessInputs.SetInput(GraphBuilder, EPostProcessMaterialInput::SeparateTranslucency, FScreenPassTexture(SmokeLocalPosAlphaTex));

	// SmokeWorldPosDepthTex → PostProcessInput4
	PostProcessInputs.SetInput(GraphBuilder, EPostProcessMaterialInput::Velocity, FScreenPassTexture(SmokeWorldPosDepthTex));

	PostProcessInputs.SceneTextures = GetSceneTextureShaderParameters(View);

	//FRDGTextureRef OutputTex = GraphBuilder.CreateTexture(
	//	FRDGTextureDesc::Create2D(TexSize, PF_FloatRGBA, FClearValueBinding::Black,
	//		TexCreate_RenderTargetable | TexCreate_ShaderResource),
	//	TEXT("IVSmokeVisualTex")
	//);
			FRDGTextureRef OutputTexture = FIVSmokePostProcessPass::CreateOutputTexture(
			GraphBuilder,
			SceneTex,
			TEXT("IVSmokeVisualTex"),
			PF_FloatRGBA,
			TexSize,
			TexCreate_RenderTargetable | TexCreate_ShaderResource
		);
	PostProcessInputs.OverrideOutput = FScreenPassRenderTarget(OutputTexture, ERenderTargetLoadAction::ENoAction);

	AddPostProcessMaterialPass(GraphBuilder, View, PostProcessInputs, SmokeVisualMat);

	return OutputTexture;
}


//~==============================================================================
// Multi-Volume Ray March Pass (Occupancy-Based Three-Pass Pipeline)

void FIVSmokeRenderer::AddMultiVolumeRayMarchPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FIVSmokePackedRenderData& RenderData,
	FRDGTextureRef SmokeAlbedoTex,
	FRDGTextureRef SmokeLocalPosAlphaTex,
	FRDGTextureRef SmokeWorldPosDepthTex,
	const FIntPoint& TexSize,
	const FIntPoint& ViewportSize,
	const FIntPoint& ViewRectMin)
{
	const int32 VolumeCount = RenderData.VolumeCount;

	if (VolumeCount == 0 || !NoiseVolume || !RenderData.bIsValid)
	{
		return;
	}

	// Get global settings
	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();

	//~==========================================================================
	// Phase 0: Setup common resources (same as standard ray march)

	const int32 TexturePackInterval = 4;
	const int32 TexturePackMaxSize = 2048;
	const FIntVector VoxelResolution = RenderData.VoxelResolution;
	const FIntVector HoleResolution = RenderData.HoleResolution;
	const FIntVector VoxelAtlasCount = GetAtlasTexCount(VoxelResolution, VolumeCount, TexturePackInterval, TexturePackMaxSize);
	const FIntVector HoleAtlasCount = GetAtlasTexCount(HoleResolution, VolumeCount, TexturePackInterval, TexturePackMaxSize);

	// Voxel Atlas: 3D packing
	const FIntVector VoxelAtlasResolution = FIntVector(
		VoxelResolution.X * VoxelAtlasCount.X + TexturePackInterval * (VoxelAtlasCount.X - 1),
		VoxelResolution.Y * VoxelAtlasCount.Y + TexturePackInterval * (VoxelAtlasCount.Y - 1),
		VoxelResolution.Z * VoxelAtlasCount.Z + TexturePackInterval * (VoxelAtlasCount.Z - 1)
	);
	const FIntVector VoxelAtlasFXAAResolution = VoxelAtlasResolution * 1;

	// Hole Atlas: 3D packing
	const FIntVector HoleAtlasResolution = FIntVector(
		HoleResolution.X * HoleAtlasCount.X + TexturePackInterval * (HoleAtlasCount.X - 1),
		HoleResolution.Y * HoleAtlasCount.Y + TexturePackInterval * (HoleAtlasCount.Y - 1),
		HoleResolution.Z * HoleAtlasCount.Z + TexturePackInterval * (HoleAtlasCount.Z - 1)
	);

	// Create atlas textures
	FRDGTextureDesc VoxelAtlasDesc = FRDGTextureDesc::Create3D(
		VoxelAtlasResolution,
		PF_R32_FLOAT,
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV
	);
	FRDGTextureRef PackedVoxelAtlas = GraphBuilder.CreateTexture(VoxelAtlasDesc, TEXT("IVSmoke_PackedVoxelAtlas"));

	FRDGTextureDesc VoxelAtlasFXAAResDesc = FRDGTextureDesc::Create3D(
		VoxelAtlasFXAAResolution,
		PF_R32_FLOAT,
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV
	);
	FRDGTextureRef PackedVoxelAtlasFXAA = GraphBuilder.CreateTexture(VoxelAtlasFXAAResDesc, TEXT("IVSmoke_PackedVoxelAtlasFXAA"));

	FRDGTextureDesc HoleAtlasDesc = FRDGTextureDesc::Create3D(
		HoleAtlasResolution,
		PF_FloatRGBA,
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV
	);
	FRDGTextureRef PackedHoleAtlas = GraphBuilder.CreateTexture(HoleAtlasDesc, TEXT("IVSmoke_PackedHoleAtlas"));

	// Clear Hole Atlas with alpha = 1 (so density is not zeroed when HoleTexture is missing)
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PackedHoleAtlas), FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));

	// Copy Hole Textures to Atlas
	FRHICopyTextureInfo HoleCpyInfo;
	HoleCpyInfo.Size = HoleResolution;
	HoleCpyInfo.SourcePosition = FIntVector::ZeroValue;

	for (int z = 0; z < HoleAtlasCount.Z; ++z)
	{
		for (int y = 0; y < HoleAtlasCount.Y; ++y)
		{
			for (int x = 0; x < HoleAtlasCount.X; ++x)
			{
				int i = x + HoleAtlasCount.X * y + z * HoleAtlasCount.X * HoleAtlasCount.Y;

				if (i >= RenderData.HoleTextures.Num())
				{
					break;
				}

				FTextureRHIRef SourceRHI = RenderData.HoleTextures[i];
				if (!SourceRHI)
				{
					continue;
				}

				FRDGTextureRef SourceTexture = GraphBuilder.RegisterExternalTexture(
					CreateRenderTarget(SourceRHI, TEXT("IVSmoke_CopyHoleSource"))
				);

				HoleCpyInfo.DestPosition.X = x * (HoleResolution.X + TexturePackInterval);
				HoleCpyInfo.DestPosition.Y = y * (HoleResolution.Y + TexturePackInterval);
				HoleCpyInfo.DestPosition.Z = z * (HoleResolution.Z + TexturePackInterval);
				AddCopyTexturePass(GraphBuilder, SourceTexture, PackedHoleAtlas, HoleCpyInfo);
			}
		}
	}

	// Create GPU buffers
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);

	FRDGBufferDesc BirthBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(float), RenderData.PackedVoxelBirthTimes.Num());
	FRDGBufferRef BirthBuffer = GraphBuilder.CreateBuffer(BirthBufferDesc, TEXT("IVSmoke_PackedBirthBuffer"));
	GraphBuilder.QueueBufferUpload(BirthBuffer, RenderData.PackedVoxelBirthTimes.GetData(), RenderData.PackedVoxelBirthTimes.Num() * sizeof(float));

	FRDGBufferDesc DeathBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(float), RenderData.PackedVoxelDeathTimes.Num());
	FRDGBufferRef DeathBuffer = GraphBuilder.CreateBuffer(DeathBufferDesc, TEXT("IVSmoke_PackedDeathBuffer"));
	GraphBuilder.QueueBufferUpload(DeathBuffer, RenderData.PackedVoxelDeathTimes.GetData(), RenderData.PackedVoxelDeathTimes.Num() * sizeof(float));

	FRDGBufferDesc VolumeBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FIVSmokeVolumeGPUData), RenderData.VolumeDataArray.Num());
	FRDGBufferRef VolumeBuffer = GraphBuilder.CreateBuffer(VolumeBufferDesc, TEXT("IVSmokeVolumeDataBuffer"));
	GraphBuilder.QueueBufferUpload(VolumeBuffer, RenderData.VolumeDataArray.GetData(), RenderData.VolumeDataArray.Num() * sizeof(FIVSmokeVolumeGPUData));

	// StructuredToTexture Pass
	TShaderMapRef<FIVSmokeStructuredToTextureCS> StructuredCopyShader(ShaderMap);
	auto* StructuredCopyParams = GraphBuilder.AllocParameters<FIVSmokeStructuredToTextureCS::FParameters>();
	StructuredCopyParams->Desti = GraphBuilder.CreateUAV(PackedVoxelAtlas);
	StructuredCopyParams->BirthTimes = GraphBuilder.CreateSRV(BirthBuffer);
	StructuredCopyParams->DeathTimes = GraphBuilder.CreateSRV(DeathBuffer);
	StructuredCopyParams->VolumeDataBuffer = GraphBuilder.CreateSRV(VolumeBuffer);
	StructuredCopyParams->TexSize = VoxelAtlasResolution;
	StructuredCopyParams->VoxelResolution = RenderData.VoxelResolution;
	StructuredCopyParams->PackedInterval = TexturePackInterval;
	StructuredCopyParams->VoxelAtlasCount = VoxelAtlasCount;
	StructuredCopyParams->GameTime = RenderData.GameTime;
	StructuredCopyParams->VolumeCount = VolumeCount;

	FIVSmokePostProcessPass::AddComputeShaderPass<FIVSmokeStructuredToTextureCS>(
		GraphBuilder,
		ShaderMap,
		StructuredCopyShader,
		StructuredCopyParams,
		VoxelAtlasResolution
	);

	// Voxel FXAA Pass
	TShaderMapRef<FIVSmokeVoxelFXAACS> VoxelFXAAShader(ShaderMap);
	auto* VoxelFXAAParams = GraphBuilder.AllocParameters<FIVSmokeVoxelFXAACS::FParameters>();

	VoxelFXAAParams->Desti = GraphBuilder.CreateUAV(PackedVoxelAtlasFXAA);
	VoxelFXAAParams->Source = GraphBuilder.CreateSRV(PackedVoxelAtlas);
	VoxelFXAAParams->LinearBorder_Sampler = TStaticSamplerState<SF_Bilinear, AM_Border, AM_Border, AM_Border>::GetRHI();
	VoxelFXAAParams->TexSize = VoxelAtlasFXAAResolution;
	VoxelFXAAParams->FXAASpanMax = Settings->FXAASpanMax;
	VoxelFXAAParams->FXAARange = Settings->FXAARange;
	VoxelFXAAParams->FXAASharpness = Settings->FXAASharpness;

	FIVSmokePostProcessPass::AddComputeShaderPass<FIVSmokeVoxelFXAACS>(
		GraphBuilder,
		ShaderMap,
		VoxelFXAAShader,
		VoxelFXAAParams,
		VoxelAtlasFXAAResolution
	);

	//~==========================================================================
	// Phase 1: Create Occupancy Resources

	const FIntPoint TileCount = IVSmokeOccupancy::ComputeTileCount(ViewportSize);
	const uint32 StepSliceCount = IVSmokeOccupancy::ComputeStepSliceCount(RenderData.MaxSteps);

	FIVSmokeOccupancyResources OccResources = IVSmokeOccupancy::CreateOccupancyResources(
		GraphBuilder,
		TileCount,
		StepSliceCount
	);

	// Calculate max ray distance and GlobalAABB based on volumes
	float MaxRayDistance = 0.0f;
	FVector3f GlobalAABBMin(1e10f, 1e10f, 1e10f);
	FVector3f GlobalAABBMax(-1e10f, -1e10f, -1e10f);
	for (const FIVSmokeVolumeGPUData& VolData : RenderData.VolumeDataArray)
	{
		FVector3f Extent = VolData.VolumeWorldAABBMax - VolData.VolumeWorldAABBMin;
		MaxRayDistance = FMath::Max(MaxRayDistance, Extent.Size());

		// Accumulate GlobalAABB
		GlobalAABBMin = FVector3f::Min(GlobalAABBMin, VolData.VolumeWorldAABBMin);
		GlobalAABBMax = FVector3f::Max(GlobalAABBMax, VolData.VolumeWorldAABBMax);
	}
	MaxRayDistance = FMath::Max(MaxRayDistance, 10000.0f); // Minimum reasonable distance

	// MinStepSize from settings (minimum world units per step, TotalVolumeLength computed per-tile in shader)
	const float MinStepSize = Settings->GetEffectiveMinStepSize();

	//~==========================================================================
	// Phase 2: Pass 0 - Tile Setup

	IVSmokeOccupancy::AddTileSetupPass(
		GraphBuilder,
		View,
		VolumeBuffer,
		RenderData.VolumeDataArray.Num(),
		OccResources.TileDataBuffer,
		TileCount,
		StepSliceCount,
		MaxRayDistance,
		ViewportSize,
		ViewRectMin
	);

	//~==========================================================================
	// Phase 3: Pass 1 - Occupancy Build

	IVSmokeOccupancy::AddOccupancyBuildPass(
		GraphBuilder,
		View,
		OccResources.TileDataBuffer,
		VolumeBuffer,
		RenderData.VolumeDataArray.Num(),
		OccResources.ViewOccupancy,
		OccResources.LightOccupancy,
		TileCount,
		StepSliceCount,
		FVector3f(RenderData.LightDirection),
		RenderData.LightMarchingDistance > 0.0f ? RenderData.LightMarchingDistance : MaxRayDistance,
		ViewportSize
	);

	//~==========================================================================
	// Phase 4: Pass 2 - Ray March with Occupancy

	TShaderMapRef<FIVSmokeMultiVolumeRayMarchCS> ComputeShader(ShaderMap);
	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeMultiVolumeRayMarchCS::FParameters>();

	// Output (Dual Render Target)
	Parameters->SmokeAlbedoTex = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SmokeAlbedoTex));
	Parameters->SmokeLocalPosAlphaTex = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SmokeLocalPosAlphaTex));
	Parameters->SmokeWorldPosDepthTex = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SmokeWorldPosDepthTex));

	// Occupancy inputs
	Parameters->TileDataBuffer = GraphBuilder.CreateSRV(OccResources.TileDataBuffer);
	Parameters->ViewOccupancy = GraphBuilder.CreateSRV(OccResources.ViewOccupancy);
	Parameters->LightOccupancy = GraphBuilder.CreateSRV(OccResources.LightOccupancy);

	// Tile configuration
	Parameters->TileCount = TileCount;
	Parameters->StepSliceCount = StepSliceCount;
	Parameters->StepDivisor = FIVSmokeOccupancyConfig::StepDivisor;

	// Noise Volume
	FTextureRHIRef TextureRHI = NoiseVolume->GetRenderTargetResource()->GetRenderTargetTexture();
	FRDGTextureRef NoiseVolumeRDG = GraphBuilder.RegisterExternalTexture(
		CreateRenderTarget(TextureRHI, TEXT("IVSmokeNoiseVolume"))
	);
	Parameters->NoiseVolume = NoiseVolumeRDG;
	Parameters->NoiseUVMul = FIVSmokeNoiseConfig::NoiseUVMul;

	// Sampler
	Parameters->LinearBorder_Sampler = TStaticSamplerState<SF_Trilinear, AM_Border, AM_Border, AM_Border>::GetRHI();
	Parameters->LinearRepeat_Sampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

	// Time
	//Parameters->ElapsedTime = View.Family->Time.GetRealTimeSeconds();
	Parameters->ElapsedTime = View.Family->Time.GetRealTimeSeconds() + ServerTimeOffset;

	// Viewport
	Parameters->TexSize = FIntPoint(TexSize.X, TexSize.Y);
	Parameters->ViewportSize = FVector2f(ViewportSize);
	Parameters->ViewRectMin = FVector2f(ViewRectMin);

	// Camera
	const FViewMatrices& ViewMatrices = View.ViewMatrices;
	Parameters->CameraPosition = FVector3f(ViewMatrices.GetViewOrigin());
	Parameters->CameraForward = FVector3f(View.GetViewDirection());
	Parameters->CameraRight = FVector3f(View.GetViewRight());
	Parameters->CameraUp = FVector3f(View.GetViewUp());

	const FMatrix& ProjMatrix = ViewMatrices.GetProjectionMatrix();
	Parameters->TanHalfFOV = 1.0f / ProjMatrix.M[1][1];
	Parameters->AspectRatio = (float)ViewportSize.X / (float)ViewportSize.Y;

	// Ray Marching
	Parameters->MaxSteps = RenderData.MaxSteps;
	Parameters->MinStepSize = MinStepSize;

	// Volume Data Buffer
	Parameters->VolumeDataBuffer = GraphBuilder.CreateSRV(VolumeBuffer);
	Parameters->NumActiveVolumes = RenderData.VolumeDataArray.Num();

	// Packed Textures
	Parameters->PackedInterval = TexturePackInterval;
	Parameters->PackedVoxelAtlas = GraphBuilder.CreateSRV(PackedVoxelAtlasFXAA);
	Parameters->VoxelTexSize = VoxelResolution;
	Parameters->PackedVoxelTexSize = VoxelAtlasResolution;
	Parameters->VoxelAtlasCount = VoxelAtlasCount;
	Parameters->PackedHoleAtlas = GraphBuilder.CreateSRV(PackedHoleAtlas);
	Parameters->HoleTexSize = HoleResolution;
	Parameters->PackedHoleTexSize = HoleAtlasResolution;
	Parameters->HoleAtlasCount = HoleAtlasCount;

	// Scene Textures
	Parameters->SceneTexturesStruct = GetSceneTextureShaderParameters(View).SceneTextures;
	Parameters->InvDeviceZToWorldZTransform = FVector4f(View.InvDeviceZToWorldZTransform);

	// View (for BlueNoise access)
	Parameters->View = View.ViewUniformBuffer;

	// Global Smoke Parameters
	Parameters->GlobalAbsorption = RenderData.GlobalAbsorption;
	Parameters->SmokeSize = RenderData.SmokeSize;
	Parameters->WindDirection = FVector3f(RenderData.WindDirection);
	Parameters->VolumeRangeOffset = RenderData.VolumeRangeOffset;
	Parameters->VolumeEdgeNoiseFadeOffset = RenderData.VolumeEdgeNoiseFadeOffset;
	Parameters->VolumeEdgeFadeSharpness = RenderData.VolumeEdgeFadeSharpness;

	// Rayleigh Scattering
	Parameters->LightDirection = FVector3f(RenderData.LightDirection);
	Parameters->LightColor = FVector3f(RenderData.LightColor.R, RenderData.LightColor.G, RenderData.LightColor.B);
	Parameters->ScatterScale = RenderData.bEnableScattering ? (RenderData.ScatterScale * RenderData.LightIntensity) : 0.0f;
	Parameters->ScatteringAnisotropy = RenderData.ScatteringAnisotropy;

	// Self-Shadowing
	Parameters->LightMarchingSteps = RenderData.bEnableSelfShadowing ? RenderData.LightMarchingSteps : 0;
	Parameters->LightMarchingDistance = RenderData.LightMarchingDistance;
	Parameters->LightMarchingExpFactor = RenderData.LightMarchingExpFactor;
	Parameters->ShadowAmbient = RenderData.ShadowAmbient;

	// Global AABB for per-pixel light march distance calculation
	Parameters->GlobalAABBMin = GlobalAABBMin;
	Parameters->GlobalAABBMax = GlobalAABBMax;

	// External Shadowing (CSM)
	Parameters->ShadowDepthBias = RenderData.ShadowDepthBias;
	Parameters->ExternalShadowAmbient = RenderData.ExternalShadowAmbient;
	Parameters->NumCascades = RenderData.NumCascades;
	Parameters->CascadeBlendRange = RenderData.CascadeBlendRange;
	Parameters->CSMCameraPosition = FVector3f(ViewMatrices.GetViewOrigin());
	Parameters->bEnableVSM = RenderData.bEnableVSM ? 1 : 0;
	Parameters->VSMMinVariance = RenderData.VSMMinVariance;
	Parameters->VSMLightBleedingReduction = RenderData.VSMLightBleedingReduction;

	// CSM cascade data
	for (int32 i = 0; i < 8; i++)
	{
		if (i < RenderData.NumCascades && i < RenderData.CSMViewProjectionMatrices.Num())
		{
			Parameters->CSMViewProjectionMatrices[i] = FMatrix44f(RenderData.CSMViewProjectionMatrices[i]);
			Parameters->CSMLightCameraPositions[i] = FVector4f(
				FVector3f(RenderData.CSMLightCameraPositions[i]),
				0.0f
			);
			Parameters->CSMLightCameraForwards[i] = FVector4f(
				FVector3f(RenderData.CSMLightCameraForwards[i]),
				0.0f
			);
		}
		else
		{
			Parameters->CSMViewProjectionMatrices[i] = FMatrix44f::Identity;
			Parameters->CSMLightCameraPositions[i] = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
			Parameters->CSMLightCameraForwards[i] = FVector4f(0.0f, 0.0f, -1.0f, 0.0f);
		}
	}

	// Split distances
	{
		float SplitDists[8];
		for (int32 i = 0; i < 8; i++)
		{
			SplitDists[i] = (i < RenderData.CSMSplitDistances.Num()) ? RenderData.CSMSplitDistances[i] : 100000.0f;
		}
		Parameters->CSMSplitDistances[0] = FVector4f(SplitDists[0], SplitDists[1], SplitDists[2], SplitDists[3]);
		Parameters->CSMSplitDistances[1] = FVector4f(SplitDists[4], SplitDists[5], SplitDists[6], SplitDists[7]);
	}

	// CSM texture arrays
	if (RenderData.NumCascades > 0)
	{
		const int32 CascadeCount = RenderData.NumCascades;
		const FIntPoint CascadeResolution = RenderData.CSMDepthTextures.Num() > 0 && RenderData.CSMDepthTextures[0].IsValid()
			? FIntPoint(RenderData.CSMDepthTextures[0]->GetSizeXYZ().X, RenderData.CSMDepthTextures[0]->GetSizeXYZ().Y)
			: FIntPoint(512, 512);

		FRDGTextureDesc DepthArrayDesc = FRDGTextureDesc::Create2DArray(
			CascadeResolution,
			PF_R32_FLOAT,
			FClearValueBinding(FLinearColor(1.0f, 0.0f, 0.0f, 0.0f)),
			TexCreate_ShaderResource | TexCreate_UAV,
			CascadeCount
		);
		FRDGTextureRef CSMDepthArray = GraphBuilder.CreateTexture(DepthArrayDesc, TEXT("IVSmokeCSMDepthArray"));

		FRDGTextureDesc VSMArrayDesc = FRDGTextureDesc::Create2DArray(
			CascadeResolution,
			PF_G32R32F,
			FClearValueBinding(FLinearColor(1.0f, 1.0f, 0.0f, 0.0f)),
			TexCreate_ShaderResource | TexCreate_UAV,
			CascadeCount
		);
		FRDGTextureRef CSMVSMArray = GraphBuilder.CreateTexture(VSMArrayDesc, TEXT("IVSmokeCSMVSMArray"));

		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CSMDepthArray), FVector4f(1.0f, 0.0f, 0.0f, 0.0f));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CSMVSMArray), FVector4f(1.0f, 1.0f, 0.0f, 0.0f));

		const int32 VSMBlurRadius = Settings ? Settings->VSMBlurRadius : 2;

		const uint32 CurrentRenderFrameNumber = View.Family->FrameNumber;
		const bool bNeedVSMProcessing = RenderData.bEnableVSM && VSMProcessor &&
		                                (CurrentRenderFrameNumber != LastVSMProcessFrameNumber);

		if (bNeedVSMProcessing)
		{
			LastVSMProcessFrameNumber = CurrentRenderFrameNumber;
		}

		for (int32 i = 0; i < CascadeCount; i++)
		{
			if (i < RenderData.CSMDepthTextures.Num() && RenderData.CSMDepthTextures[i].IsValid())
			{
				FRDGTextureRef SourceDepth = GraphBuilder.RegisterExternalTexture(
					CreateRenderTarget(RenderData.CSMDepthTextures[i], TEXT("IVSmokeCSMDepthSource"))
				);

				FRHICopyTextureInfo DepthCopyInfo;
				DepthCopyInfo.Size = FIntVector(CascadeResolution.X, CascadeResolution.Y, 1);
				DepthCopyInfo.SourcePosition = FIntVector::ZeroValue;
				DepthCopyInfo.DestPosition = FIntVector::ZeroValue;
				DepthCopyInfo.DestSliceIndex = i;
				DepthCopyInfo.NumSlices = 1;
				AddCopyTexturePass(GraphBuilder, SourceDepth, CSMDepthArray, DepthCopyInfo);

				if (RenderData.bEnableVSM && i < RenderData.CSMVSMTextures.Num() && RenderData.CSMVSMTextures[i].IsValid())
				{
					FRDGTextureRef VSMTexture = GraphBuilder.RegisterExternalTexture(
						CreateRenderTarget(RenderData.CSMVSMTextures[i], TEXT("IVSmokeCSMVSMSource"))
					);

					if (bNeedVSMProcessing && VSMProcessor)
					{
						VSMProcessor->Process(GraphBuilder, SourceDepth, VSMTexture, VSMBlurRadius);
					}

					FRHICopyTextureInfo VSMCopyInfo;
					VSMCopyInfo.Size = FIntVector(CascadeResolution.X, CascadeResolution.Y, 1);
					VSMCopyInfo.SourcePosition = FIntVector::ZeroValue;
					VSMCopyInfo.DestPosition = FIntVector::ZeroValue;
					VSMCopyInfo.DestSliceIndex = i;
					VSMCopyInfo.NumSlices = 1;
					AddCopyTexturePass(GraphBuilder, VSMTexture, CSMVSMArray, VSMCopyInfo);
				}
			}
		}

		Parameters->CSMDepthTextureArray = CSMDepthArray;
		Parameters->CSMVSMTextureArray = CSMVSMArray;
	}
	else
	{
		FRDGTextureDesc DummyDepthArrayDesc = FRDGTextureDesc::Create2DArray(
			FIntPoint(1, 1),
			PF_R32_FLOAT,
			FClearValueBinding(FLinearColor(1.0f, 0.0f, 0.0f, 0.0f)),
			TexCreate_ShaderResource | TexCreate_UAV,
			1
		);
		FRDGTextureRef DummyDepthArray = GraphBuilder.CreateTexture(DummyDepthArrayDesc, TEXT("IVSmokeCSMDepthArrayDummy"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DummyDepthArray), FVector4f(1.0f, 0.0f, 0.0f, 0.0f));

		FRDGTextureDesc DummyVSMArrayDesc = FRDGTextureDesc::Create2DArray(
			FIntPoint(1, 1),
			PF_G32R32F,
			FClearValueBinding(FLinearColor(1.0f, 1.0f, 0.0f, 0.0f)),
			TexCreate_ShaderResource | TexCreate_UAV,
			1
		);
		FRDGTextureRef DummyVSMArray = GraphBuilder.CreateTexture(DummyVSMArrayDesc, TEXT("IVSmokeCSMVSMArrayDummy"));
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DummyVSMArray), FVector4f(1.0f, 1.0f, 0.0f, 0.0f));

		Parameters->CSMDepthTextureArray = DummyDepthArray;
		Parameters->CSMVSMTextureArray = DummyVSMArray;
	}
	Parameters->CSMSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	// Temporal
	Parameters->FrameNumber = View.Family->FrameNumber;
	Parameters->JitterIntensity = 1.0f;

	// Dispatch
	FIVSmokePostProcessPass::AddComputeShaderPass<FIVSmokeMultiVolumeRayMarchCS>(
		GraphBuilder,
		ShaderMap,
		ComputeShader,
		Parameters,
		FIntVector(TexSize.X, TexSize.Y, 1)
	);
}

//~==============================================================================
// Stats Tracking

void FIVSmokeRenderer::UpdateStatsIfNeeded(const FIVSmokePackedRenderData& RenderData, const FIntPoint& ViewportSize)
{
	const double CurrentTime = FPlatformTime::Seconds();
	if (CurrentTime - LastStatUpdateTime < 1.0)
	{
		return;
	}
	LastStatUpdateTime = CurrentTime;

	// Calculate per-frame texture size
	CachedPerFrameSize = CalculatePerFrameTextureSize(
		ViewportSize,
		RenderData.VolumeCount,
		RenderData.VoxelResolution,
		RenderData.HoleResolution
	);

	// Calculate CSM size using CalcTextureMemorySizeEnum
	CachedCSMSize = 0;
	if (CSMRenderer && CSMRenderer->IsInitialized())
	{
		const TArray<FIVSmokeCascadeData>& Cascades = CSMRenderer->GetCascades();
		for (const FIVSmokeCascadeData& Cascade : Cascades)
		{
			if (Cascade.DepthRT)
			{
				CachedCSMSize += Cascade.DepthRT->CalcTextureMemorySizeEnum(TMC_ResidentMips);
			}
			if (Cascade.VSMRT)
			{
				CachedCSMSize += Cascade.VSMRT->CalcTextureMemorySizeEnum(TMC_ResidentMips);
			}
		}
	}

	// Update all stats
	UpdateAllStats();
}

int64 FIVSmokeRenderer::CalculatePerFrameTextureSize(
	const FIntPoint& ViewportSize,
	int32 VolumeCount,
	const FIntVector& VoxelResolution,
	const FIntVector& HoleResolution
) const
{
	if (VolumeCount == 0)
	{
		return 0;
	}

	int64 TotalSize = 0;

	// Half-resolution Smoke Albedo + Mask (PF_FloatRGBA)
	const FIntPoint HalfSize(FMath::Max(1, ViewportSize.X / 2), FMath::Max(1, ViewportSize.Y / 2));
	TotalSize += CalculateImageBytes(HalfSize.X, HalfSize.Y, 1, PF_FloatRGBA) * 2;

	// Voxel Atlas: Use existing GetAtlasTexCount logic constants
	const int32 TexturePackInterval = 4;
	const int32 TexturePackMaxSize = 2048;

	FIntVector VoxelAtlasCount = GetAtlasTexCount(VoxelResolution, VolumeCount, TexturePackInterval, TexturePackMaxSize);
	FIntVector VoxelAtlasResolution(
		VoxelResolution.X * VoxelAtlasCount.X + TexturePackInterval * (VoxelAtlasCount.X - 1),
		VoxelResolution.Y * VoxelAtlasCount.Y + TexturePackInterval * (VoxelAtlasCount.Y - 1),
		VoxelResolution.Z * VoxelAtlasCount.Z + TexturePackInterval * (VoxelAtlasCount.Z - 1)
	);
	// PackedVoxelAtlas (PF_R32_FLOAT) + PackedVoxelAtlasFXAA (PF_R32_FLOAT)
	TotalSize += CalculateImageBytes(VoxelAtlasResolution.X, VoxelAtlasResolution.Y, VoxelAtlasResolution.Z, PF_R32_FLOAT) * 2;

	// Hole Atlas (PF_FloatRGBA)
	FIntVector HoleAtlasCount = GetAtlasTexCount(HoleResolution, VolumeCount, TexturePackInterval, TexturePackMaxSize);
	FIntVector HoleAtlasResolution(
		HoleResolution.X * HoleAtlasCount.X + TexturePackInterval * (HoleAtlasCount.X - 1),
		HoleResolution.Y * HoleAtlasCount.Y + TexturePackInterval * (HoleAtlasCount.Y - 1),
		HoleResolution.Z * HoleAtlasCount.Z + TexturePackInterval * (HoleAtlasCount.Z - 1)
	);
	TotalSize += CalculateImageBytes(HoleAtlasResolution.X, HoleAtlasResolution.Y, HoleAtlasResolution.Z, PF_FloatRGBA);

	// Occupancy textures (View + Light): Use FIVSmokeOccupancyConfig constants
	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();
	if (Settings)
	{
		const FIntPoint TileCount(
			(ViewportSize.X + FIVSmokeOccupancyConfig::TileSizeX - 1) / FIVSmokeOccupancyConfig::TileSizeX,
			(ViewportSize.Y + FIVSmokeOccupancyConfig::TileSizeY - 1) / FIVSmokeOccupancyConfig::TileSizeY
		);
		const uint32 StepSliceCount = (Settings->GetEffectiveMaxSteps() + FIVSmokeOccupancyConfig::StepDivisor - 1) / FIVSmokeOccupancyConfig::StepDivisor;
		// uint4 = 16 bytes per texel, 2 textures (View + Light)
		TotalSize += CalculateImageBytes(TileCount.X, TileCount.Y, StepSliceCount, PF_R32G32B32A32_UINT) * 2;
	}

	return TotalSize;
}

void FIVSmokeRenderer::UpdateAllStats()
{
	// Memory stats
	SET_MEMORY_STAT(STAT_IVSmoke_NoiseVolume, CachedNoiseVolumeSize);
	SET_MEMORY_STAT(STAT_IVSmoke_CSMShadowMaps, CachedCSMSize);
	SET_MEMORY_STAT(STAT_IVSmoke_PerFrameTextures, CachedPerFrameSize);
	SET_MEMORY_STAT(STAT_IVSmoke_TotalVRAM, CachedNoiseVolumeSize + CachedCSMSize + CachedPerFrameSize);
}
#endif
