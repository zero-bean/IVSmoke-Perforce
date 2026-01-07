// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeRenderer.h"

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
		UE_LOG(LogTemp, Error, TEXT("[FIVSmokeRenderer::CreateNoiseVolume] Failed to get render target resource"));
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

	// Fall back to default preset from settings
	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();
	if (Settings->DefaultSmokePreset.IsValid())
	{
		return Settings->DefaultSmokePreset.LoadSynchronous();
	}

	return nullptr;
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

	// Create intermediate texture with ViewRect size for smoke compositing
	const FIntPoint QuaterTexSize = FIntPoint(FMath::CeilToInt(ViewportSize.X * 0.25f), FMath::CeilToInt(ViewportSize.Y * 0.25f));
	FRDGTextureRef SmokeAlbedoTex = FIVSmokePostProcessPass::CreateOutputTexture(
		GraphBuilder,
		SceneColor.Texture,
		TEXT("IVSmokeAlbedoTexture"),
		PF_FloatRGBA,
		QuaterTexSize,
		TexCreate_RenderTargetable | TexCreate_ShaderResource | ETextureCreateFlags::UAV
	);
	FRDGTextureRef SmokeMaskTex = FIVSmokePostProcessPass::CreateOutputTexture(
		GraphBuilder,
		SceneColor.Texture,
		TEXT("IVSmokeMaskTexture"),
		PF_FloatRGBA,
		QuaterTexSize,
		TexCreate_RenderTargetable | TexCreate_ShaderResource | ETextureCreateFlags::UAV
	);
	AddRayMarchPass(GraphBuilder, View, SmokeAlbedoTex, SmokeMaskTex, ViewportSize, ViewRectMin);
	AddCompositePass(GraphBuilder, View, SceneColor.Texture, SmokeAlbedoTex, SmokeMaskTex, Output, ViewportSize);

	return Output;
}

// ============================================================================
// Pass Functions
// ============================================================================

void FIVSmokeRenderer::AddRayMarchPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef SmokeAlbedoTex,
	FRDGTextureRef SmokeMaskTex,
	const FIntPoint& ViewportSize,
	const FIntPoint& ViewRectMin)
{
	// Get first valid volume
	AIVSmokeVoxelVolume* VoxelVolume = nullptr;
	{
		FScopeLock Lock(&VolumesMutex);
		for (const auto& WeakVolume : Volumes)
		{
			if (auto* Volume = WeakVolume.Get())
			{
				VoxelVolume = Volume;
				break;
			}
		}
	}

	if (!VoxelVolume)
	{
		return;
	}

	// Ensure noise volume is initialized
	if (!NoiseVolume)
	{
		return;
	}

	// Get voxel data from volume
	const TArray<int32>& VoxelData = VoxelVolume->GetVoxelArray();
	const FIntVector GridRes = VoxelVolume->GetGridResolution();
	const FIntVector CenterOff = VoxelVolume->GetCenterOffset();
	const float VoxelSz = VoxelVolume->GetVoxelSize();

	// Skip if voxel data is not initialized
	if (VoxelData.Num() == 0 || GridRes.X <= 0 || GridRes.Y <= 0 || GridRes.Z <= 0)
	{
		return;
	}

	// Get effective preset for this volume
	const UIVSmokeSmokePreset* Preset = GetEffectivePreset(VoxelVolume);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeRayMarchCS> ComputeShader(ShaderMap);

	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeRayMarchCS::FParameters>();

	// Output
	Parameters->SmokeAlbedoTex = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SmokeAlbedoTex));
	Parameters->SmokeMaskTex = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SmokeMaskTex));

	// Input Texture (Noise Volume)
	FTextureRHIRef TextureRHI = NoiseVolume->GetRenderTargetResource()->GetRenderTargetTexture();
	FRDGTextureRef NoiseVolumeRDG = GraphBuilder.RegisterExternalTexture(
		CreateRenderTarget(TextureRHI, TEXT("IVSmokeNoiseVolume"))
	);
	Parameters->NoiseVolume = NoiseVolumeRDG;

	// Sampler
	Parameters->LinearRepeat_Sampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

	// Time - use real delta time accumulation
	ElapsedTime += View.Family->Time.GetDeltaWorldTimeSeconds();
	Parameters->ElapseTime = ElapsedTime;


	// Viewport
	const FIntPoint TexSize = FIntPoint(SmokeAlbedoTex->Desc.GetSize().X, SmokeAlbedoTex->Desc.GetSize().Y);
	Parameters->TexSize = TexSize;
	Parameters->ViewportSize = FVector2f(ViewportSize);
	Parameters->ViewRectMin = FVector2f(ViewRectMin);

	// Camera (Ray reconstruction using vectors)
	const FViewMatrices& ViewMatrices = View.ViewMatrices;
	Parameters->CameraPosition = FVector3f(ViewMatrices.GetViewOrigin());
	Parameters->CameraForward = FVector3f(View.GetViewDirection());
	Parameters->CameraRight = FVector3f(View.GetViewRight());
	Parameters->CameraUp = FVector3f(View.GetViewUp());

	// Calculate FOV from projection matrix
	const FMatrix& ProjMatrix = ViewMatrices.GetProjectionMatrix();
	float TanHalfFOV = 1.0f / ProjMatrix.M[1][1];
	float AspectRatio = (float)ViewportSize.X / (float)ViewportSize.Y;

	Parameters->TanHalfFOV = TanHalfFOV;
	Parameters->AspectRatio = AspectRatio;

	// ============================================================================
	// Voxel Volume Data
	// ============================================================================

	FRDGBufferDesc BufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(int32), VoxelData.Num());
	FRDGBufferRef VoxelBuffer = GraphBuilder.CreateBuffer(BufferDesc, TEXT("IVSmokeVoxelBuffer"));
	GraphBuilder.QueueBufferUpload(VoxelBuffer, VoxelData.GetData(), VoxelData.Num() * sizeof(int32));
	Parameters->VoxelBuffer = GraphBuilder.CreateSRV(VoxelBuffer);

	// World to Local transform
	FMatrix WorldToLocal = VoxelVolume->GetActorTransform().ToInverseMatrixWithScale();
	Parameters->WorldToLocal = FMatrix44f(WorldToLocal);

	// Grid parameters
	Parameters->GridResolution = FIntVector3(GridRes.X, GridRes.Y, GridRes.Z);
	Parameters->CenterOffset = FIntVector3(CenterOff.X, CenterOff.Y, CenterOff.Z);
	Parameters->VoxelSize = VoxelSz;

	// Calculate volume bounds in world space for ray-box intersection
	FVector HalfExtent = FVector(CenterOff) * VoxelSz;
	FVector LocalMin = -HalfExtent;
	FVector LocalMax = FVector(GridRes - CenterOff - FIntVector(1, 1, 1)) * VoxelSz;

	FTransform VolumeTransform = VoxelVolume->GetActorTransform();
	FBox LocalBox(LocalMin, LocalMax);
	FBox WorldBox = LocalBox.TransformBy(VolumeTransform);

	Parameters->VolumeMin = FVector3f(WorldBox.Min);
	Parameters->VolumeMax = FVector3f(WorldBox.Max);

	// Scene Textures
	Parameters->SceneTexturesStruct = GetSceneTextureShaderParameters(View).SceneTextures;
	Parameters->InvDeviceZToWorldZTransform = FVector4f(View.InvDeviceZToWorldZTransform);

	// ============================================================================
	// Smoke Rendering Parameters (from Preset or defaults)
	// ============================================================================

	if (Preset)
	{
		Parameters->VolumeDensity = Preset->VolumeDensity;
		Parameters->SmokeColor = FVector3f(Preset->SmokeColor.R, Preset->SmokeColor.G, Preset->SmokeColor.B);
		Parameters->SmokeAbsorption = Preset->SmokeAbsorption;
		Parameters->SmokeSize = Preset->SmokeSize;
		Parameters->SmokeDensityFalloff = Preset->SmokeDensityFalloff;
		Parameters->WindDirection = FVector3f(Preset->WindDirection);
		Parameters->MaxSteps = Preset->MaxSteps;
	}
	else
	{
		// Fallback defaults
		Parameters->VolumeDensity = 1.0f;
		Parameters->SmokeColor = FVector3f(0.8f, 0.8f, 0.8f);
		Parameters->SmokeAbsorption = 0.1f;
		Parameters->SmokeSize = 128.0f;
		Parameters->SmokeDensityFalloff = 0.2f;
		Parameters->WindDirection = FVector3f(0.01f, 0.02f, 0.1f);
		Parameters->MaxSteps = 64;
	}

	FIVSmokePassConfig Config;
	Config.EventName = TEXT("IVSmokeRayMarch");
	Config.ThreadGroupSizeX = FIVSmokeRayMarchCS::ThreadGroupSizeX;
	Config.ThreadGroupSizeY = FIVSmokeRayMarchCS::ThreadGroupSizeY;

	const FIntPoint& TotalThreadSize = FIntPoint(TexSize.X, TexSize.Y);

	FIVSmokePostProcessPass::AddComputeShaderPass(
		GraphBuilder,
		ShaderMap,
		ComputeShader,
		Parameters,
		TotalThreadSize,
		Config
	);
}

void FIVSmokeRenderer::AddCompositePass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef SceneTex,
	FRDGTextureRef SmokeAlbedoTex,
	FRDGTextureRef SmokeMaskTex,
	const FScreenPassRenderTarget& Output,
	const FIntPoint& ViewportSize)
{
	const FIntPoint QuaterTexSize = FIntPoint(FMath::CeilToInt(ViewportSize.X * 0.25f), FMath::CeilToInt(ViewportSize.Y * 0.25f));
	const FIntPoint HalfTexSize = FIntPoint(FMath::CeilToInt(ViewportSize.X * 0.5f), FMath::CeilToInt(ViewportSize.Y * 0.5f));
	const FIntPoint FullTexSize = ViewportSize;

	FScreenPassViewInfo ViewInfo(View);
	FRDGTextureRef HalfMaskTex = AddCopyPass(GraphBuilder, View, SmokeMaskTex, HalfTexSize, TEXT("HalfMaskTex"));
	FRDGTextureRef FullMaskTex = AddCopyPass(GraphBuilder, View, HalfMaskTex, FullTexSize, TEXT("FullMaskTex"));
	AddCopyPass(GraphBuilder, View, FullMaskTex, HalfMaskTex);
	AddCopyPass(GraphBuilder, View, HalfMaskTex, SmokeMaskTex);


	FRDGTextureRef HalfAlbedoTex = AddCopyPass(GraphBuilder, View, SmokeAlbedoTex, HalfTexSize, TEXT("HalfAlbedoTex"));
	FRDGTextureRef FullAlbedoTex = AddCopyPass(GraphBuilder, View, HalfAlbedoTex, FullTexSize, TEXT("FullAlbedoTex"));

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeSharpenCompositePS> SharpenCompositePS(ShaderMap);
	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeSharpenCompositePS::FParameters>();
	Parameters->SceneTex = SceneTex;
	Parameters->SmokeAlbedoTex = FullAlbedoTex;
	Parameters->SmokeMaskTex = SmokeMaskTex;
	Parameters->LinearRepeat_Sampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	Parameters->Sharpness = 0.1f;
	Parameters->ViewportSize = ViewportSize;
	Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();
	FIVSmokePassConfig Config;
	Config.EventName = TEXT("IVSmokeSharpenComposite");
	Config.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();
	FIVSmokePostProcessPass::AddPixelShaderPass(
		GraphBuilder,
		ShaderMap,
		SharpenCompositePS,
		Parameters,
		Output,
		Config);
}

void FIVSmokeRenderer::AddCopyPass(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef SourceTex, FRDGTextureRef DestiTex)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeCopyPS> CopyPS(ShaderMap);

	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeCopyPS::FParameters>();
	Parameters->MainTex = SourceTex;
	Parameters->LinearRepeat_Sampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	Parameters->ViewportSize = DestiTex->Desc.Extent;
	Parameters->RenderTargets[0] = FRenderTargetBinding(DestiTex, ERenderTargetLoadAction::ENoAction);
	FScreenPassRenderTarget Output(
		DestiTex,
		FIntRect(0, 0, DestiTex->Desc.Extent.X, DestiTex->Desc.Extent.Y),
		ERenderTargetLoadAction::ENoAction
	);

	FIVSmokePassConfig Config;
	Config.EventName = TEXT("IVSmokeCopy");
	Config.BlendState = TStaticBlendState<>::GetRHI();

	FIVSmokePostProcessPass::AddPixelShaderPass(
		GraphBuilder,
		ShaderMap,
		CopyPS,
		Parameters,
		Output,
		Config
	);
}
FRDGTextureRef FIVSmokeRenderer::AddCopyPass(FRDGBuilder& GraphBuilder, const FSceneView& View, FRDGTextureRef SourceTex, const FIntPoint& DestiSize, const TCHAR* TexName )
{
	FIntPoint TexSize = DestiSize;
	if (DestiSize == FIntPoint::ZeroValue)
	{
		FIntPoint TextureSize = SourceTex->Desc.Extent;
		TexSize = FIntPoint(TextureSize.X, TextureSize.Y);
	}
	FRDGTextureRef DestiTex = FIVSmokePostProcessPass::CreateOutputTexture(
		GraphBuilder,
		SourceTex,
		TexName,
		PF_FloatRGBA,
		TexSize,
		TexCreate_RenderTargetable | TexCreate_ShaderResource);
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeCopyPS> CopyPS(ShaderMap);

	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeCopyPS::FParameters>();
	Parameters->MainTex = SourceTex;
	Parameters->LinearRepeat_Sampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	Parameters->ViewportSize = TexSize;
	Parameters->RenderTargets[0] = FRenderTargetBinding(DestiTex, ERenderTargetLoadAction::ENoAction);

	FScreenPassRenderTarget Output(
		DestiTex,
		FIntRect(0, 0, TexSize.X, TexSize.Y),
		ERenderTargetLoadAction::ENoAction
	);

	FIVSmokePassConfig Config;
	Config.EventName = TEXT("IVSmokeCopy");
	Config.BlendState = TStaticBlendState<>::GetRHI();

	FIVSmokePostProcessPass::AddPixelShaderPass(
		GraphBuilder,
		ShaderMap,
		CopyPS,
		Parameters,
		Output,
		Config
	);

	return DestiTex;
}
