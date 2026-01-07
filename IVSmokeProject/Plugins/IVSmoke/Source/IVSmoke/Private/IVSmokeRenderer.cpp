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

	// Calculate reduced resolution (1/4 = half width * half height)
	const FIntPoint TexSize = FIntPoint(
		FMath::Max(1, ViewportSize.X / 2),
		FMath::Max(1, ViewportSize.Y / 2)
	);

	// Create Dual Render Target textures at reduced resolution
	FRDGTextureRef SmokeAlbedoTex = FIVSmokePostProcessPass::CreateOutputTexture(
		GraphBuilder,
		SceneColor.Texture,
		TEXT("IVSmokeSmokeAlbedoTex"),
		PF_FloatRGBA,
		TexSize
	);

	FRDGTextureRef SmokeMaskTex = FIVSmokePostProcessPass::CreateOutputTexture(
		GraphBuilder,
		SceneColor.Texture,
		TEXT("IVSmokeSmokeMaskTex"),
		PF_FloatRGBA,
		TexSize
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

	// Single-pass multi-volume rendering (correct Beer-Lambert integration)
	// Outputs to Dual RT (SmokeAlbedoTex + SmokeMaskTex) at reduced resolution
	AddMultiVolumeRayMarchPass(
		GraphBuilder,
		View,
		SortedVolumes,
		SmokeAlbedoTex,
		SmokeMaskTex,
		TexSize,
		ViewportSize,
		ViewRectMin
	);

	// Composite with sharpening (upscales from reduced resolution)
	AddSharpenCompositePass(GraphBuilder, View, SceneColor.Texture, SmokeAlbedoTex, SmokeMaskTex, Output, ViewportSize);

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

	// Time
	ElapsedTime += View.Family->Time.GetDeltaWorldTimeSeconds();
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

	// Scene Textures
	Parameters->SceneTexturesStruct = GetSceneTextureShaderParameters(View).SceneTextures;
	Parameters->InvDeviceZToWorldZTransform = FVector4f(View.InvDeviceZToWorldZTransform);

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
	const FIntPoint& ViewportSize)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeSharpenCompositePS> PixelShader(ShaderMap);

	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeSharpenCompositePS::FParameters>();
	Parameters->SceneTex = SceneTex;
	Parameters->SmokeAlbedoTex = SmokeAlbedoTex;
	Parameters->SmokeMaskTex = SmokeMaskTex;
	Parameters->LinearRepeat_Sampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->Sharpness = 0.5f;  // TODO: Make configurable via preset
	Parameters->ViewportSize = FVector2f(ViewportSize);
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
