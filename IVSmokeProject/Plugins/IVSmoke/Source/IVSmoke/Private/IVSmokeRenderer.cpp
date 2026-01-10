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

	// Cache default preset on Game Thread to avoid Render Thread access issues
	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();

	// Use !IsNull() instead of IsValid() because IsValid() returns false for unloaded assets
	// IsNull() checks if the path is empty, !IsNull() means a path is configured
	if (!Settings->DefaultSmokePreset.IsNull())
	{
		CachedDefaultPreset = Settings->DefaultSmokePreset.LoadSynchronous();
		if (CachedDefaultPreset)
		{
			UE_LOG(LogIVSmoke, Log, TEXT("[FIVSmokeRenderer::Initialize] Loaded DefaultSmokePreset: %s, Color: (%.2f, %.2f, %.2f)"),
				*CachedDefaultPreset->GetName(),
				CachedDefaultPreset->SmokeColor.R,
				CachedDefaultPreset->SmokeColor.G,
				CachedDefaultPreset->SmokeColor.B);
		}
		else
		{
			UE_LOG(LogIVSmoke, Error, TEXT("[FIVSmokeRenderer::Initialize] Failed to load DefaultSmokePreset from path: %s"),
				*Settings->DefaultSmokePreset.ToString());
		}
	}
	else
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[FIVSmokeRenderer::Initialize] DefaultSmokePreset is not configured in Project Settings! Smoke will use fallback gray color (0.8, 0.8, 0.8)."));
	}
}

void FIVSmokeRenderer::Shutdown()
{
	if (NoiseVolume)
	{
		NoiseVolume->RemoveFromRoot();
		NoiseVolume = nullptr;
	}
	CachedDefaultPreset = nullptr;
	ElapsedTime = 0.0f;
}

void FIVSmokeRenderer::RefreshCachedPreset()
{
	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();

	if (!Settings->DefaultSmokePreset.IsNull())
	{
		CachedDefaultPreset = Settings->DefaultSmokePreset.LoadSynchronous();
		if (CachedDefaultPreset)
		{
			UE_LOG(LogIVSmoke, Log, TEXT("[FIVSmokeRenderer::RefreshCachedPreset] Refreshed DefaultSmokePreset: %s, Color: (%.2f, %.2f, %.2f)"),
				*CachedDefaultPreset->GetName(),
				CachedDefaultPreset->SmokeColor.R,
				CachedDefaultPreset->SmokeColor.G,
				CachedDefaultPreset->SmokeColor.B);
		}
	}
	else
	{
		CachedDefaultPreset = nullptr;
		UE_LOG(LogIVSmoke, Warning, TEXT("[FIVSmokeRenderer::RefreshCachedPreset] DefaultSmokePreset is not configured. Using fallback values."));
	}
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

	// Fall back to cached default preset (loaded on Initialize() to avoid Render Thread issues)
	return CachedDefaultPreset.Get();
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
// Rendering
// ============================================================================

FScreenPassTexture FIVSmokeRenderer::Render(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	// Check if rendering is enabled
	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();
	if (!Settings->bEnableSmokeRendering)
	{
		return FScreenPassTexture();
	}

	// Get scene color from inputs
	FScreenPassTextureSlice SceneColorSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
	if (!SceneColorSlice.IsValid())
	{
		return FScreenPassTexture();
	}

	FScreenPassTexture SceneColor(SceneColorSlice);

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
	// Upscaling Pipeline (1/2 → Full)
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

	// Collect valid volumes
	TArray<AIVSmokeVoxelVolume*> SortedVolumes;
	{
		FScopeLock Lock(&VolumesMutex);
		for (const auto& WeakVolume : Volumes)
		{
			if (auto* Volume = WeakVolume.Get())
			{
				SortedVolumes.Add(Volume);
			}
		}
	}

	if (SortedVolumes.Num() == 0)
	{
		return FScreenPassTexture();
	}

	// ============================================================================
	// Ray March Pass (1/2 Resolution)
	// ============================================================================
	AddMultiVolumeRayMarchPass(
		GraphBuilder,
		View,
		SortedVolumes,
		SmokeAlbedoTex,
		SmokeMaskTex,
		HalfSize,
		ViewportSize,
		ViewRectMin
	);

	// ============================================================================
	// Upscaling (1/2 → Full)
	// ============================================================================
	// Single-step bilinear upscaling smooths IGN grain patterns.

	// Albedo: 1/2 → Full
	FRDGTextureRef SmokeAlbedoFull = AddCopyPass(
		GraphBuilder,
		View,
		SmokeAlbedoTex,
		ViewportSize,
		TEXT("IVSmokeAlbedoTex_Full")
	);

	// Mask: 1/2 → Full
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
	const float Sharpness = CachedDefaultPreset ? CachedDefaultPreset->Sharpness : 0.0f;
	AddSharpenCompositePass(GraphBuilder, View, SceneColor.Texture, SmokeAlbedoFull, SmokeMaskFull, Output, ViewportSize, Sharpness);

	return Output;
}

// ============================================================================
// Pass Functions
// ============================================================================

void FIVSmokeRenderer::AddMultiVolumeRayMarchPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const TArray<AIVSmokeVoxelVolume*>& SortedVolumes,
	FRDGTextureRef SmokeAlbedoTex,
	FRDGTextureRef SmokeMaskTex,
	const FIntPoint& TexSize,
	const FIntPoint& ViewportSize,
	const FIntPoint& ViewRectMin)
{
	if (SortedVolumes.Num() == 0 || !NoiseVolume)
	{
		return;
	}

	// Performance warning for too many volumes
	if (SortedVolumes.Num() > 8)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[FIVSmokeRenderer] Performance warning: %d volumes active (recommended: 8 or fewer)"), SortedVolumes.Num());
	}

	// ============================================================================
	// Build Volume GPU Data and Pack Voxel Buffers
	// ============================================================================

	TArray<FIVSmokeVolumeGPUData> VolumeDataArray;
	VolumeDataArray.Reserve(SortedVolumes.Num());

	// Calculate total voxel count for packed buffer
	uint32 TotalVoxelCount = 0;
	for (AIVSmokeVoxelVolume* Volume : SortedVolumes)
	{
		TotalVoxelCount += Volume->GetVoxelBufferSize();
	}

	// Create packed voxel buffer
	TArray<float> PackedVoxelData;
	PackedVoxelData.Reserve(TotalVoxelCount);

	// Create packed SDF buffer (atlas of all hole textures)
	// Use actual texture dimensions instead of VoxelResolution to avoid size mismatch
	int32 VolumeCount = SortedVolumes.Num();
	FIntVector HoleSDFTexSize = FIntVector::ZeroValue;

	// Get texture size from first valid hole texture
	for (int32 i = 0; i < VolumeCount; ++i)
	{
		UIVSmokeHoleGeneratorComponent* HoleComp = SortedVolumes[i]->GetHoleGeneratorComponent();
		if (HoleComp && HoleComp->GetHoleTexture())
		{
			FIntVector TexSize = HoleComp->GetHoleTexture()->GetSizeXYZ();
			HoleSDFTexSize = TexSize;
			break;
		}
	}

	// If no valid hole texture found, use default size
	if (HoleSDFTexSize == FIntVector::ZeroValue)
	{
		HoleSDFTexSize = FIntVector(64, 64, 64);
	}

	FIntVector AtlasSize = FIntVector(HoleSDFTexSize.X, HoleSDFTexSize.Y, HoleSDFTexSize.Z * VolumeCount);
	FRDGTextureDesc AtlasDesc = FRDGTextureDesc::Create3D(
		AtlasSize,
		PF_A32B32G32R32F,
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef PackedHoleSDFBuffer = GraphBuilder.CreateTexture(AtlasDesc, TEXT("PackedHoleSDFBuffer"));

	for (int32 i = 0; i < VolumeCount; ++i)
	{
		UIVSmokeHoleGeneratorComponent* HoleComp = SortedVolumes[i]->GetHoleGeneratorComponent();
		if (!HoleComp)
		{
			continue;
		}

		FTextureRHIRef SourceRHI = HoleComp->GetHoleTexture();
		if (!SourceRHI)
		{
			continue;
		}

		FIntVector SourceSize = SourceRHI->GetSizeXYZ();
		FRDGTextureRef SourceTexture = GraphBuilder.RegisterExternalTexture(
			CreateRenderTarget(SourceRHI, TEXT("Source"))
		);

		FRHICopyTextureInfo CopyInfo;
		CopyInfo.Size = SourceSize;
		CopyInfo.SourcePosition = FIntVector::ZeroValue;
		CopyInfo.DestPosition = FIntVector(0, 0, i * HoleSDFTexSize.Z);

		AddCopyTexturePass(
			GraphBuilder,
			SourceTexture,
			PackedHoleSDFBuffer,
			CopyInfo
		);
	}

	uint32 CurrentOffset = 0;
	for (int32 i = 0; i < SortedVolumes.Num(); ++i)
	{
		AIVSmokeVoxelVolume* Volume = SortedVolumes[i];
		const TArray<float>& VoxelData = Volume->GetVoxelArray();
		const FIntVector GridRes = Volume->GetGridResolution();
		const FIntVector CenterOff = Volume->GetCenterOffset();
		const float VoxelSz = Volume->GetVoxelSize();

		// Skip invalid volumes
		if (VoxelData.Num() == 0 || GridRes.X <= 0 || GridRes.Y <= 0 || GridRes.Z <= 0)
		{
			continue;
		}

		// Calculate local and world AABB
		FVector HalfExtent = FVector(CenterOff) * VoxelSz;
		FVector LocalMin = -HalfExtent;
		FVector LocalMax = FVector(GridRes - CenterOff - FIntVector(1, 1, 1)) * VoxelSz;

		FTransform VolumeTransform = Volume->GetActorTransform();
		FBox LocalBox(LocalMin, LocalMax);
		FBox WorldBox = LocalBox.TransformBy(VolumeTransform);

		// Get effective preset
		const UIVSmokeSmokePreset* Preset = GetEffectivePreset(Volume);

		// Build GPU data
		FIVSmokeVolumeGPUData GPUData;
		FMemory::Memzero(&GPUData, sizeof(GPUData));

		GPUData.WorldToLocal = FMatrix44f(VolumeTransform.ToInverseMatrixWithScale());
		GPUData.LocalToWorld = FMatrix44f(VolumeTransform.ToMatrixWithScale());
		GPUData.AABBMin = FVector3f(LocalMin);
		GPUData.VoxelSize = VoxelSz;
		GPUData.AABBMax = FVector3f(LocalMax);
		GPUData.VoxelBufferOffset = CurrentOffset;
		GPUData.GridResolution = FIntVector3(GridRes.X, GridRes.Y, GridRes.Z);
		GPUData.VoxelCount = VoxelData.Num();

		if (Preset)
		{
			GPUData.SmokeColor = FVector3f(Preset->SmokeColor.R, Preset->SmokeColor.G, Preset->SmokeColor.B);
			GPUData.Absorption = Preset->SmokeAbsorption;
		}
		else
		{
			GPUData.SmokeColor = FVector3f(0.8f, 0.8f, 0.8f);
			GPUData.Absorption = 0.1f;
		}

		GPUData.CenterOffset = FVector3f(CenterOff.X, CenterOff.Y, CenterOff.Z);
		GPUData.DensityScale = Preset ? Preset->VolumeDensity : 1.0f;
		GPUData.WorldAABBMin = FVector3f(WorldBox.Min);
		GPUData.WorldAABBMax = FVector3f(WorldBox.Max);

		VolumeDataArray.Add(GPUData);

		// Append voxel data to packed buffer
		PackedVoxelData.Append(VoxelData);
		CurrentOffset += VoxelData.Num();
	}

	if (VolumeDataArray.Num() == 0 || PackedVoxelData.Num() == 0)
	{
		return;
	}

	// ============================================================================
	// Create GPU Buffers
	// ============================================================================

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
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

	// Sampler
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
	const UIVSmokeSmokePreset* DefaultPreset = CachedDefaultPreset.Get();
	Parameters->MaxSteps = DefaultPreset ? DefaultPreset->MaxSteps : 128;

	// Volume Data Buffer
	FRDGBufferDesc VolumeBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FIVSmokeVolumeGPUData), VolumeDataArray.Num());
	FRDGBufferRef VolumeBuffer = GraphBuilder.CreateBuffer(VolumeBufferDesc, TEXT("IVSmokeVolumeDataBuffer"));
	GraphBuilder.QueueBufferUpload(VolumeBuffer, VolumeDataArray.GetData(), VolumeDataArray.Num() * sizeof(FIVSmokeVolumeGPUData));
	Parameters->VolumeDataBuffer = GraphBuilder.CreateSRV(VolumeBuffer);
	Parameters->NumActiveVolumes = VolumeDataArray.Num();

	// Packed Voxel Buffer
	FRDGBufferDesc VoxelBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(float), PackedVoxelData.Num());
	FRDGBufferRef VoxelBuffer = GraphBuilder.CreateBuffer(VoxelBufferDesc, TEXT("IVSmokePackedVoxelBuffer"));
	GraphBuilder.QueueBufferUpload(VoxelBuffer, PackedVoxelData.GetData(), PackedVoxelData.Num() * sizeof(float));
	Parameters->PackedVoxelBuffer = GraphBuilder.CreateSRV(VoxelBuffer);

	// Packed SDF Buffer
	Parameters->PackedHoleSDFBuffer = GraphBuilder.CreateSRV(PackedHoleSDFBuffer);
	Parameters->HoleSDFTexCount = VolumeCount;
	Parameters->HoleSDFTexSize = HoleSDFTexSize;

	// Scene Textures
	Parameters->SceneTexturesStruct = GetSceneTextureShaderParameters(View).SceneTextures;
	Parameters->InvDeviceZToWorldZTransform = FVector4f(View.InvDeviceZToWorldZTransform);

	// View (for BlueNoise access)
	Parameters->View = View.ViewUniformBuffer;

	// Global Smoke Parameters
	Parameters->GlobalAbsorption = DefaultPreset ? DefaultPreset->SmokeAbsorption : 0.1f;
	Parameters->SmokeSize = DefaultPreset ? DefaultPreset->SmokeSize : 128.0f;
	Parameters->SmokeDensityFalloff = DefaultPreset ? DefaultPreset->SmokeDensityFalloff : 0.2f;
	Parameters->WindDirection = DefaultPreset ? FVector3f(DefaultPreset->WindDirection) : FVector3f(0.01f, 0.02f, 0.1f);

	// Rayleigh Scattering
	float ScatterScaleValue = DefaultPreset ? DefaultPreset->ScatterScale : 0.5f;
	bool bEnableScatter = DefaultPreset ? DefaultPreset->bEnableScattering : true;

	// Light Direction - use preset override or default overhead sun
	FVector3f LightDir = FVector3f(0.2f, 0.1f, 0.9f).GetSafeNormal();
	if (DefaultPreset && DefaultPreset->bOverrideLightDirection)
	{
		LightDir = FVector3f(DefaultPreset->LightDirectionOverride.GetSafeNormal());
	}

	// Light Color - use preset override or default warm white
	FVector3f LightColorValue = FVector3f(1.0f, 0.95f, 0.9f);
	if (DefaultPreset && DefaultPreset->bOverrideLightColor)
	{
		LightColorValue = FVector3f(
			DefaultPreset->LightColorOverride.R,
			DefaultPreset->LightColorOverride.G,
			DefaultPreset->LightColorOverride.B
		);
	}

	Parameters->LightDirection = LightDir;
	Parameters->LightColor = LightColorValue;
	Parameters->ScatterScale = bEnableScatter ? ScatterScaleValue : 0.0f;

	// Henyey-Greenstein Anisotropy
	float AnisotropyValue = DefaultPreset ? DefaultPreset->ScatteringAnisotropy : 0.5f;
	Parameters->ScatteringAnisotropy = AnisotropyValue;

	// Self-Shadowing (Light Marching)
	bool bSelfShadow = DefaultPreset ? DefaultPreset->bEnableSelfShadowing : true;
	int32 LightSteps = DefaultPreset ? DefaultPreset->LightMarchingSteps : 6;
	float LightDistance = DefaultPreset ? DefaultPreset->LightMarchingDistance : 0.0f;
	float LightExpFactor = DefaultPreset ? DefaultPreset->LightMarchingExpFactor : 2.0f;
	float ShadowAmbientValue = DefaultPreset ? DefaultPreset->ShadowAmbient : 0.2f;
	Parameters->LightMarchingSteps = bSelfShadow ? LightSteps : 0;
	Parameters->LightMarchingDistance = LightDistance;
	Parameters->LightMarchingExpFactor = LightExpFactor;
	Parameters->ShadowAmbient = ShadowAmbientValue;

	// Temporal (for TAA integration)
	Parameters->FrameNumber = View.Family->FrameNumber;

	// Dispatch at reduced resolution (TexSize)
	FIVSmokePassConfig Config;
	Config.EventName = TEXT("IVSmokeMultiVolumeRayMarch");
	Config.ThreadGroupSizeX = FIVSmokeMultiVolumeRayMarchCS::ThreadGroupSizeX;
	Config.ThreadGroupSizeY = FIVSmokeMultiVolumeRayMarchCS::ThreadGroupSizeY;

	FIVSmokePostProcessPass::AddComputeShaderPass(
		GraphBuilder,
		ShaderMap,
		ComputeShader,
		Parameters,
		TexSize,  // Dispatch at reduced resolution
		Config
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

	FIVSmokePassConfig Config;
	Config.EventName = TEXT("IVSmokeSharpenComposite");
	// No blend state needed - shader does the compositing internally
	Config.BlendState = TStaticBlendState<>::GetRHI();

	FIVSmokePostProcessPass::AddPixelShaderPass(
		GraphBuilder,
		ShaderMap,
		PixelShader,
		Parameters,
		Output,
		Config
	);
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

	FIVSmokePassConfig Config;
	Config.EventName = TEXT("IVSmokeCopy");
	Config.BlendState = TStaticBlendState<>::GetRHI();

	FIVSmokePostProcessPass::AddPixelShaderPass(
		GraphBuilder,
		ShaderMap,
		CopyShader,
		Parameters,
		Output,
		Config
	);
}
