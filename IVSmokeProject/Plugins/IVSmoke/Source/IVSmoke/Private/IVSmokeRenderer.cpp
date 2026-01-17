// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeRenderer.h"
#include "IVSmoke.h"
#include "IVSmokePostProcessPass.h"
#include "IVSmokeSettings.h"
#include "IVSmokeShaders.h"
#include "IVSmokeSmokePreset.h"
#include "IVSmokeVoxelVolume.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "SceneRenderTargetParameters.h"
#include "IVSmokeHoleGeneratorComponent.h"
#include "RenderGraphUtils.h"

#if !UE_SERVER
FIVSmokeRenderer& FIVSmokeRenderer::Get()
{
	static FIVSmokeRenderer Instance;
	return Instance;
}

// ============================================================================
// Lifecycle
// ============================================================================

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
	ElapsedTime = 0.0f;
}
FIntVector FIVSmokeRenderer::GetAtlasTexCount(const FIntVector& TexSize, const int32 TexCount, const int32 TexturePackInterval, const int32 TexturePackMaxSize)
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
		//warning size full
		AtlasTexCount.Z = QuotientZ;
	}
	else
	{
		AtlasTexCount.Z = CurTexCount;
	}
	return AtlasTexCount;
}


void FIVSmokeRenderer::CreateNoiseVolume()
{
	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();
	const FIVSmokeNoiseSettings& NoiseSettings = Settings->NoiseSettings;

	// Create volume texture
	NoiseVolume = NewObject<UTextureRenderTargetVolume>();
	NoiseVolume->AddToRoot(); // Prevent GC
	NoiseVolume->Init(NoiseSettings.TexSize, NoiseSettings.TexSize, NoiseSettings.TexSize, EPixelFormat::PF_R16F);
	NoiseVolume->bCanCreateUAV = true;
	NoiseVolume->ClearColor = FLinearColor::Black;
	NoiseVolume->SRGB = false;
	NoiseVolume->UpdateResourceImmediate(true);

	// Run compute shader to generate noise
	FTextureRenderTargetResource* RenderTargetResource = NoiseVolume->GameThread_GetRenderTargetResource();
	if (!RenderTargetResource)
	{
		UE_LOG(LogIVSmoke, Error, TEXT("[FIVSmokeRenderer::CreateNoiseVolume] Failed to get render target resource"));
		return;
	}

	ENQUEUE_RENDER_COMMAND(IVSmokeGenerateNoise)(
		[RenderTargetResource, NoiseSettings](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef NoiseTexture = GraphBuilder.RegisterExternalTexture(
				CreateRenderTarget(RenderTargetResource->TextureRHI, TEXT("IVSmokeNoiseVolume"))
			);

			FRDGTextureUAVRef OutputUAV = GraphBuilder.CreateUAV(NoiseTexture);

			auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeNoiseGeneratorGlobalCS::FParameters>();
			Parameters->RWNoiseTex = OutputUAV;
			Parameters->TexSize = FUintVector3(NoiseSettings.TexSize, NoiseSettings.TexSize, NoiseSettings.TexSize);
			Parameters->Octaves = NoiseSettings.Octaves;
			Parameters->Wrap = NoiseSettings.Wrap;
			Parameters->AxisCellCount = NoiseSettings.AxisCellCount;
			Parameters->Amplitude = NoiseSettings.Amplitude;
			Parameters->CellSize = NoiseSettings.CellSize;
			Parameters->Seed = NoiseSettings.Seed;

			TShaderMapRef<FIVSmokeNoiseGeneratorGlobalCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

			FIntVector GroupCount(
				FMath::DivideAndRoundUp(NoiseSettings.TexSize, 8),
				FMath::DivideAndRoundUp(NoiseSettings.TexSize, 8),
				FMath::DivideAndRoundUp(NoiseSettings.TexSize, 8)
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

// ============================================================================
// Volume Management
// ============================================================================

void FIVSmokeRenderer::AddVolume(AIVSmokeVoxelVolume* Volume)
{
	FScopeLock Lock(&VolumesMutex);
	Volumes.AddUnique(Volume);

	// Auto-initialize on first volume
	if (!IsInitialized())
	{
		Initialize();
	}
}

void FIVSmokeRenderer::RemoveVolume(AIVSmokeVoxelVolume* Volume)
{
	FScopeLock Lock(&VolumesMutex);
	Volumes.Remove(Volume);
}

bool FIVSmokeRenderer::HasVolumes() const
{
	FScopeLock Lock(&VolumesMutex);
	return Volumes.Num() > 0;
}

// ============================================================================
// Thread-Safe Render Data Preparation
// ============================================================================

FIVSmokePackedRenderData FIVSmokeRenderer::PrepareRenderData(const TArray<AIVSmokeVoxelVolume*>& InVolumes)
{
	// Must be called on Game Thread
	check(IsInGameThread());

	FIVSmokePackedRenderData Result;

	if (InVolumes.Num() == 0)
	{
		return Result;
	}

	Result.VolumeCount = InVolumes.Num();
	Result.VolumeDataArray.Reserve(Result.VolumeCount);
	Result.HoleTextures.Reserve(Result.VolumeCount);
	Result.HoleTextureSizes.Reserve(Result.VolumeCount);

	// Get resolution info from first valid volume
	for (AIVSmokeVoxelVolume* Volume : InVolumes)
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
	Result.PackedVoxelData.Reserve(TotalVoxelSize);

	// Collect data from all volumes (Game Thread - safe to access)
	for (int32 i = 0; i < InVolumes.Num(); ++i)
	{
		AIVSmokeVoxelVolume* Volume = InVolumes[i];
		if (!Volume)
		{
			continue;
		}

		// ============================================
		// Copy VoxelArray data (Game Thread safe)
		// ============================================
		const TArray<float>& VoxelArray = Volume->GetVoxelArray();
		Result.PackedVoxelData.Append(VoxelArray);
		if (i < InVolumes.Num() - 1)
		{
			Result.PackedVoxelData.Append(VoxelIntervalData);
		}

		// ============================================
		// Hole Texture reference (RHI resources are thread-safe)
		// ============================================
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

		// ============================================
		// Build GPU metadata
		// ============================================
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
		GPUData.VoxelCount = VoxelArray.Num();
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

	// ============================================
	// Copy global settings parameters
	// ============================================
	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();

	if (Settings)
	{
		// Post processing
		Result.Sharpness = Settings->Sharpness;

		// Ray marching
		Result.MaxSteps = Settings->MaxSteps;

		// Appearance
		Result.GlobalAbsorption = 0.1f;  // Default, per-volume absorption from preset
		Result.SmokeSize = Settings->SmokeSize;
		Result.SmokeDensityFalloff = Settings->SmokeDensityFalloff;
		Result.WindDirection = Settings->WindDirection;
		Result.VolumeRangeOffset = Settings->VolumeRangeOffset;
		Result.VolumeEdgeNoiseFadeOffset = Settings->VolumeEdgeNoiseFadeOffset;
		Result.VolumeEdgeFadeSharpness = Settings->VolumeEdgeFadeShapness;

		// Scattering
		Result.bEnableScattering = Settings->bEnableScattering;
		Result.ScatterScale = Settings->ScatterScale;
		Result.ScatteringAnisotropy = Settings->ScatteringAnisotropy;

		if (Settings->bOverrideLightDirection)
		{
			Result.LightDirection = Settings->LightDirectionOverride.GetSafeNormal();
		}
		else
		{
			Result.LightDirection = FVector(0.2f, 0.1f, 0.9f).GetSafeNormal();
		}

		if (Settings->bOverrideLightColor)
		{
			Result.LightColor = Settings->LightColorOverride;
		}
		else
		{
			Result.LightColor = FLinearColor(1.0f, 0.95f, 0.9f);
		}

		// Self-shadowing
		Result.bEnableSelfShadowing = Settings->bEnableSelfShadowing;
		Result.LightMarchingSteps = Settings->LightMarchingSteps;
		Result.LightMarchingDistance = Settings->LightMarchingDistance;
		Result.LightMarchingExpFactor = Settings->LightMarchingExpFactor;
		Result.ShadowAmbient = Settings->ShadowAmbient;
	}

	Result.bIsValid = Result.VolumeDataArray.Num() > 0 && Result.PackedVoxelData.Num() > 0;

	if (InVolumes.Num() > 0 && InVolumes[0])
	{
		Result.GameTime = InVolumes[0]->GetWorld()->GetTimeSeconds();
	}
	else
	{
		Result.GameTime = 0.0f;
	}

	return Result;
}

// ============================================================================
// Rendering
// ============================================================================

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

	// Check if rendering is enabled - passthrough if disabled
	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();
	if (!Settings->bEnableSmokeRendering)
	{
		return SceneColor;
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

	// ============================================================================
	// Upscaling Pipeline (1/2 Full)
	// ============================================================================
	//
	// Ray March at 1/2 resolution for quality/performance balance.
	// Single-step upscaling with bilinear filtering smooths IGN grain.
	// Note: 1/4 resolution causes excessive grain when camera is inside smoke.
	//
	const FIntPoint HalfSize = FIntPoint(
		FMath::Max(1, ViewportSize.X / 2),
		FMath::Max(1, ViewportSize.Y / 2)
	);

	// Create Dual Render Target textures at 1/2 resolution
	FRDGTextureRef SmokeAlbedoTex = FIVSmokePostProcessPass::CreateOutputTexture(
		GraphBuilder,
		SceneColor.Texture,
		TEXT("IVSmokeAlbedoTex_Half"),
		PF_FloatRGBA,
		HalfSize
	);

	FRDGTextureRef SmokeMaskTex = FIVSmokePostProcessPass::CreateOutputTexture(
		GraphBuilder,
		SceneColor.Texture,
		TEXT("IVSmokeMaskTex_Half"),
		PF_FloatRGBA,
		HalfSize
	);

	// Get cached render data (prepared on Game Thread via BeginRenderViewFamily)
	// Use copy instead of MoveTemp - multiple views in same frame share the same data
	FIVSmokePackedRenderData RenderData;
	{
		FScopeLock Lock(&RenderDataMutex);
		RenderData = CachedRenderData;  // Copy - don't consume, other views may need it
	}

	if (!RenderData.bIsValid)
	{
		return SceneColor;
	}

	// ============================================================================
	// Ray March Pass (1/2 Resolution)
	// ============================================================================
	AddMultiVolumeRayMarchPass(
		GraphBuilder,
		View,
		RenderData,
		SmokeAlbedoTex,
		SmokeMaskTex,
		HalfSize,
		ViewportSize,
		ViewRectMin
	);

	// ============================================================================
	// Upscaling (1/2 Full)
	// ============================================================================
	// Single-step bilinear upscaling smooths IGN grain patterns.

	// Albedo: 1/2 Full
	FRDGTextureRef SmokeAlbedoFull = AddCopyPass(
		GraphBuilder,
		View,
		SmokeAlbedoTex,
		ViewportSize,
		TEXT("IVSmokeAlbedoTex_Full")
	);

	// Mask: 1/2 Full
	FRDGTextureRef SmokeMaskFull = AddCopyPass(
		GraphBuilder,
		View,
		SmokeMaskTex,
		ViewportSize,
		TEXT("IVSmokeMaskTex_Full")
	);

	// ============================================================================
	// Composite Pass
	// ============================================================================
	const float Sharpness = RenderData.Sharpness;
	const bool bUseCustomDepthBasedSorting = Settings->bUseCustomDepthBasedSorting;

	// Check if we're in TranslucencyAfterDOF mode (setting + SeparateTranslucency input valid)
	const bool bTranslucencyMode = (Settings->RenderPass == EIVSmokeRenderPass::TranslucencyAfterDOF);
	FScreenPassTextureSlice SeparateTranslucencySlice = Inputs.GetInput(EPostProcessMaterialInput::SeparateTranslucency);

	// ============================================================================
	// Depth-Sorted Composite: Proper smoke/particle sorting using CustomDepth
	// ============================================================================
	if (bUseCustomDepthBasedSorting && bTranslucencyMode && SeparateTranslucencySlice.IsValid())
	{
		FScreenPassTexture ParticlesTex(SeparateTranslucencySlice);

		// Create output texture based on ParticlesTex (same as TranslucencyComposite)
		// TranslucencyAfterDOF mode expects output in SeparateTranslucency format
		FRDGTextureRef OutputTexture = FIVSmokePostProcessPass::CreateOutputTexture(
			GraphBuilder,
			ParticlesTex.Texture,
			TEXT("IVSmokeDepthSortedOutput"),
			PF_FloatRGBA
		);

		FScreenPassRenderTarget SortedOutput(
			OutputTexture,
			ParticlesTex.ViewRect,
			ERenderTargetLoadAction::ENoAction
		);

		// Pass texture extents for UV calculation (UV = SvPosition / TexExtent)
		const FIntPoint SmokeExtent = ViewportSize;

		AddDepthSortedCompositePass(
			GraphBuilder,
			View,
			SmokeAlbedoFull,
			SmokeMaskFull,
			ParticlesTex.Texture,
			SortedOutput,
			SmokeExtent,
			Sharpness
		);

		return FScreenPassTexture(SortedOutput);
	}
	// ============================================================================
	// Standard TranslucencyAfterDOF Mode: Smoke OVER particles (no depth sorting)
	// ============================================================================
	else if (bTranslucencyMode && SeparateTranslucencySlice.IsValid())
	{
		// TranslucencyAfterDOF mode: Composite smoke OVER particles
		FScreenPassTexture ParticlesTex(SeparateTranslucencySlice);

		// Smoke textures are rendered at SceneColor.ViewRect
		// Particles texture is at its own ViewRect (SeparateTranslucency)
		// These can differ! Shader handles separate UV calculation for each.

		// Create output texture with SAME SIZE as ParticlesTex
		FRDGTextureRef OutputTexture = FIVSmokePostProcessPass::CreateOutputTexture(
			GraphBuilder,
			ParticlesTex.Texture,
			TEXT("IVSmokeTranslucencyOutput"),
			PF_FloatRGBA
		);

		FScreenPassRenderTarget TranslucencyOutput(
			OutputTexture,
			ParticlesTex.ViewRect,
			ERenderTargetLoadAction::ENoAction
		);

		// Pass texture extents for UV calculation (UV = SvPosition / TexExtent)
		const FIntPoint SmokeExtent = ViewportSize;
		const FIntPoint ParticlesExtent(ParticlesTex.Texture->Desc.Extent.X, ParticlesTex.Texture->Desc.Extent.Y);

		AddTranslucencyCompositePass(
			GraphBuilder,
			View,
			SmokeAlbedoFull,
			SmokeMaskFull,
			ParticlesTex.Texture,
			TranslucencyOutput,
			SmokeExtent,
			ParticlesExtent,
			Sharpness
		);

		return FScreenPassTexture(TranslucencyOutput);
	}
	// ============================================================================
	// Standard Mode: Composite smoke with scene color
	// ============================================================================
	else
	{
		AddSharpenCompositePass(
			GraphBuilder,
			View,
			SceneColor.Texture,
			SmokeAlbedoFull,
			SmokeMaskFull,
			Output,
			ViewportSize,
			Sharpness
		);

		return FScreenPassTexture(Output);
	}
}

// ============================================================================
// Pass Functions
// ============================================================================

void FIVSmokeRenderer::AddMultiVolumeRayMarchPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FIVSmokePackedRenderData& RenderData,
	FRDGTextureRef SmokeAlbedoTex,
	FRDGTextureRef SmokeMaskTex,
	const FIntPoint& TexSize,
	const FIntPoint& ViewportSize,
	const FIntPoint& ViewRectMin)
{
	const int32 VolumeCount = RenderData.VolumeCount;

	if (VolumeCount == 0 || !NoiseVolume || !RenderData.bIsValid)
	{
		return;
	}

	// Performance warning for too many volumes
	if (VolumeCount > 8)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[FIVSmokeRenderer] Performance warning: %d volumes active (recommended: 8 or fewer)"), VolumeCount);
	}

	// ============================================================================
	// Use Pre-Packed Data from RenderData (prepared on Game Thread)
	// ============================================================================

	const int32 TexturePackInterval = 4;
	const int32 TexturePackMaxSize = 2048;
	const FIntVector VoxelResolution = RenderData.VoxelResolution;
	const FIntVector HoleResolution = RenderData.HoleResolution;
	const FIntVector HoleAtlasCount = GetAtlasTexCount(HoleResolution, VolumeCount, TexturePackInterval, TexturePackMaxSize);

	const FIntVector VoxelAtlasResolution = FIntVector(
		VoxelResolution.X,
		VoxelResolution.Y,
		VoxelResolution.Z * VolumeCount + TexturePackInterval * (VolumeCount - 1));
	const FIntVector VoxelAtlasFXAAResolution = VoxelAtlasResolution * 1;
	const FIntVector HoleAtlasResolution = FIntVector(
		HoleResolution.X * HoleAtlasCount.X + TexturePackInterval * (HoleAtlasCount.X - 1),
		HoleResolution.Y * HoleAtlasCount.Y + TexturePackInterval * (HoleAtlasCount.Y - 1),
		HoleResolution.Z * HoleAtlasCount.Z + TexturePackInterval * (HoleAtlasCount.Z - 1));

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
		PF_R16F,
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV
	);
	FRDGTextureRef PackedHoleAtlas = GraphBuilder.CreateTexture(HoleAtlasDesc, TEXT("IVSmoke_PackedHoleAtlas"));

	// ============================================================================
	// Clear Hole Atlas first (ensures uninitialized regions are zero, not garbage)
	// ============================================================================
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PackedHoleAtlas), 0.0f);

	// ============================================================================
	// Copy Hole Textures to Atlas (using pre-collected RHI references)
	// ============================================================================
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

	// Get global settings for FXAA parameters
	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();

	// ============================================================================
	// StructuredToTexture Pass
	// ============================================================================
	FRDGBufferDesc PackedVoxelBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(float), RenderData.PackedVoxelData.Num());
	FRDGBufferRef PackedVoxelBuffer = GraphBuilder.CreateBuffer(PackedVoxelBufferDesc, TEXT("IVSmoke_PackedVoxelBuffer"));
	GraphBuilder.QueueBufferUpload(PackedVoxelBuffer, RenderData.PackedVoxelData.GetData(), RenderData.PackedVoxelData.Num() * sizeof(float));

	FRDGBufferDesc VolumeBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FIVSmokeVolumeGPUData), RenderData.VolumeDataArray.Num());
	FRDGBufferRef VolumeBuffer = GraphBuilder.CreateBuffer(VolumeBufferDesc, TEXT("IVSmokeVolumeDataBuffer"));
	GraphBuilder.QueueBufferUpload(VolumeBuffer, RenderData.VolumeDataArray.GetData(), RenderData.VolumeDataArray.Num() * sizeof(FIVSmokeVolumeGPUData));

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeStructuredToTextureCS> StructuredCopyShader(ShaderMap);
	auto* StructuredCopyParams = GraphBuilder.AllocParameters<FIVSmokeStructuredToTextureCS::FParameters>();
	StructuredCopyParams->Desti = GraphBuilder.CreateUAV(PackedVoxelAtlas);
	StructuredCopyParams->Source = GraphBuilder.CreateSRV(PackedVoxelBuffer);
	StructuredCopyParams->VolumeDataBuffer = GraphBuilder.CreateSRV(VolumeBuffer);
	StructuredCopyParams->TexSize = VoxelAtlasResolution;
	StructuredCopyParams->VoxelResolution = RenderData.VoxelResolution;
	StructuredCopyParams->PackedInterval = TexturePackInterval;
	StructuredCopyParams->GameTime = RenderData.GameTime;

	FIVSmokePostProcessPass::AddComputeShaderPass<FIVSmokeStructuredToTextureCS>(
		GraphBuilder,
		ShaderMap,
		StructuredCopyShader,
		StructuredCopyParams,
		VoxelAtlasResolution  // Dispatch at reduced resolution
	);

	// ============================================================================
	// Voxel Fxaa Pass
	// ============================================================================*/
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
		VoxelAtlasFXAAResolution );

	// ============================================================================
	// Create GPU Buffers
	// ============================================================================

	TShaderMapRef<FIVSmokeMultiVolumeRayMarchCS> ComputeShader(ShaderMap);

	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeMultiVolumeRayMarchCS::FParameters>();

	// Output (Dual Render Target)
	Parameters->SmokeAlbedoTex = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SmokeAlbedoTex));
	Parameters->SmokeMaskTex = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SmokeMaskTex));

	// Noise Volume
	FTextureRHIRef TextureRHI = NoiseVolume->GetRenderTargetResource()->GetRenderTargetTexture();
	FRDGTextureRef NoiseVolumeRDG = GraphBuilder.RegisterExternalTexture(
		CreateRenderTarget(TextureRHI, TEXT("IVSmokeNoiseVolume"))
	);
	Parameters->NoiseVolume = NoiseVolumeRDG;
	Parameters->NoiseUVMul = Settings->NoiseUVMul;

	// Sampler
	Parameters->LinearBorder_Sampler = TStaticSamplerState<SF_Trilinear, AM_Border, AM_Border, AM_Border>::GetRHI();
	Parameters->LinearRepeat_Sampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

	// Time (use RealTimeSeconds to keep jitter working during pause)
	ElapsedTime = View.Family->Time.GetRealTimeSeconds();
	Parameters->ElapsedTime = ElapsedTime;

	// Viewport (TexSize = reduced resolution, ViewportSize = full resolution for depth)
	Parameters->TexSize = FIntPoint(TexSize.X, TexSize.Y);
	Parameters->ViewportSize = FVector2f(ViewportSize);
	Parameters->ViewRectMin = FVector2f(ViewRectMin);

	// Camera
	const FViewMatrices& ViewMatrices = View.ViewMatrices;
	Parameters->CameraPosition = FVector3f(ViewMatrices.GetViewOrigin());
	Parameters->CameraForward = FVector3f(View.GetViewDirection());
	Parameters->CameraRight = FVector3f(View.GetViewRight());
	Parameters->CameraUp = FVector3f(View.GetViewUp());

	// Calculate FOV and aspect ratio based on full viewport (not reduced)
	const FMatrix& ProjMatrix = ViewMatrices.GetProjectionMatrix();
	float TanHalfFOV = 1.0f / ProjMatrix.M[1][1];
	float AspectRatio = (float)ViewportSize.X / (float)ViewportSize.Y;

	Parameters->TanHalfFOV = TanHalfFOV;
	Parameters->AspectRatio = AspectRatio;

	// Ray Marching
	Parameters->MaxSteps = RenderData.MaxSteps;

	// Volume Data Buffer (use pre-built data from RenderData)
	Parameters->VolumeDataBuffer = GraphBuilder.CreateSRV(VolumeBuffer);
	Parameters->NumActiveVolumes = RenderData.VolumeDataArray.Num();

	// Packed Textures
	Parameters->PackedInterval = TexturePackInterval;
	Parameters->PackedVoxelAtlas = GraphBuilder.CreateSRV(PackedVoxelAtlasFXAA);
	Parameters->VoxelTexSize = VoxelResolution;
	//Parameters->PackedVoxelAtlas = GraphBuilder.CreateSRV(PackedVoxelAtlas);
	//Parameters->VoxelTexSize = VoxelResolution;
	Parameters->PackedHoleAtlas = GraphBuilder.CreateSRV(PackedHoleAtlas);
	Parameters->HoleTexSize = HoleResolution;
	Parameters->PackedHoleTexSize = HoleAtlasResolution;
	Parameters->HoleAtlasCount = HoleAtlasCount;

	// Scene Textures
	Parameters->SceneTexturesStruct = GetSceneTextureShaderParameters(View).SceneTextures;
	Parameters->InvDeviceZToWorldZTransform = FVector4f(View.InvDeviceZToWorldZTransform);

	// View (for BlueNoise access)
	Parameters->View = View.ViewUniformBuffer;

	// Global Smoke Parameters (from RenderData - prepared on Game Thread)
	Parameters->GlobalAbsorption = RenderData.GlobalAbsorption;
	Parameters->SmokeSize = RenderData.SmokeSize;
	Parameters->WindDirection = FVector3f(RenderData.WindDirection);
	Parameters->VolumeRangeOffset = RenderData.VolumeRangeOffset;
	Parameters->VolumeEdgeNoiseFadeOffset = RenderData.VolumeEdgeNoiseFadeOffset;
	Parameters->VolumeEdgeFadeShapness = RenderData.VolumeEdgeFadeSharpness;

	// Rayleigh Scattering (from RenderData)
	Parameters->LightDirection = FVector3f(RenderData.LightDirection);
	Parameters->LightColor = FVector3f(RenderData.LightColor.R, RenderData.LightColor.G, RenderData.LightColor.B);
	Parameters->ScatterScale = RenderData.bEnableScattering ? RenderData.ScatterScale : 0.0f;

	// Henyey-Greenstein Anisotropy
	Parameters->ScatteringAnisotropy = RenderData.ScatteringAnisotropy;

	// Self-Shadowing (Light Marching) - from RenderData
	Parameters->LightMarchingSteps = RenderData.bEnableSelfShadowing ? RenderData.LightMarchingSteps : 0;
	Parameters->LightMarchingDistance = RenderData.LightMarchingDistance;
	Parameters->LightMarchingExpFactor = RenderData.LightMarchingExpFactor;
	Parameters->ShadowAmbient = RenderData.ShadowAmbient;

	// Temporal (for TAA integration)
	Parameters->FrameNumber = View.Family->FrameNumber;

	// Dispatch at reduced resolution (TexSize)
	FIVSmokePostProcessPass::AddComputeShaderPass<FIVSmokeMultiVolumeRayMarchCS>(
		GraphBuilder,
		ShaderMap,
		ComputeShader,
		Parameters,
		FIntVector(TexSize.X, TexSize.Y, 1) // Dispatch at reduced resolution
	);
}

void FIVSmokeRenderer::AddSharpenCompositePass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef SceneTex,
	FRDGTextureRef SmokeAlbedoTex,
	FRDGTextureRef SmokeMaskTex,
	const FScreenPassRenderTarget& Output,
	const FIntPoint& ViewportSize,
	float Sharpness)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeSharpenCompositePS> PixelShader(ShaderMap);

	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeSharpenCompositePS::FParameters>();
	Parameters->SceneTex = SceneTex;
	Parameters->SmokeAlbedoTex = SmokeAlbedoTex;
	Parameters->SmokeMaskTex = SmokeMaskTex;
	Parameters->LinearRepeat_Sampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->Sharpness = Sharpness;
	Parameters->ViewportSize = FVector2f(ViewportSize);
	Parameters->ViewRectMin = FVector2f(Output.ViewRect.Min);
	Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FIVSmokePostProcessPass::AddPixelShaderPass<FIVSmokeSharpenCompositePS>(GraphBuilder, ShaderMap, PixelShader, Parameters, Output);
}

// ============================================================================
// Copy Pass (Progressive Upscaling)
// ============================================================================

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

void FIVSmokeRenderer::AddTranslucencyCompositePass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef SmokeAlbedoTex,
	FRDGTextureRef SmokeMaskTex,
	FRDGTextureRef ParticlesTex,
	const FScreenPassRenderTarget& Output,
	const FIntPoint& SmokeTexExtent,
	const FIntPoint& ParticlesTexExtent,
	float Sharpness)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeTranslucencyCompositePS> PixelShader(ShaderMap);

	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeTranslucencyCompositePS::FParameters>();
	Parameters->SmokeAlbedoTex = SmokeAlbedoTex;
	Parameters->SmokeMaskTex = SmokeMaskTex;
	Parameters->ParticlesTex = ParticlesTex;
	Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->Sharpness = Sharpness;
	Parameters->SmokeTexExtent = FVector2f(SmokeTexExtent);
	Parameters->ParticlesTexExtent = FVector2f(ParticlesTexExtent);
	Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FIVSmokePostProcessPass::AddPixelShaderPass<FIVSmokeTranslucencyCompositePS>(GraphBuilder, ShaderMap, PixelShader, Parameters, Output);
}

void FIVSmokeRenderer::AddDepthSortedCompositePass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef SmokeAlbedoTex,
	FRDGTextureRef SmokeMaskTex,
	FRDGTextureRef SeparateTranslucencyTex,
	const FScreenPassRenderTarget& Output,
	const FIntPoint& SmokeTexExtent,
	float Sharpness)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeDepthSortedCompositePS> PixelShader(ShaderMap);

	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeDepthSortedCompositePS::FParameters>();

	// Smoke layer (from ray marching CS)
	Parameters->SmokeAlbedoTex = SmokeAlbedoTex;
	Parameters->SmokeMaskTex = SmokeMaskTex;

	// Particle layer (from Separate Translucency)
	Parameters->SeparateTranslucencyTex = SeparateTranslucencyTex;

	// Scene Textures (provides CustomDepth and SceneDepth via uniform buffer)
	Parameters->SceneTexturesStruct = GetSceneTextureShaderParameters(View).SceneTextures;

	// Samplers
	Parameters->PointClamp_Sampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->LinearClamp_Sampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	// Texture Extents for UV calculation (UV = SvPosition / TexExtent)
	Parameters->SmokeTexExtent = FVector2f(SmokeTexExtent);
	Parameters->Sharpness = Sharpness;
	Parameters->InvDeviceZToWorldZTransform = FVector4f(View.InvDeviceZToWorldZTransform);

	// Render target
	Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FIVSmokePostProcessPass::AddPixelShaderPass<FIVSmokeDepthSortedCompositePS>(GraphBuilder, ShaderMap, PixelShader, Parameters, Output);
}
#endif
