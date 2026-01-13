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
		return SceneColor;
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
	const float Sharpness = Settings->Sharpness;
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

		// Create output texture
		FRDGTextureRef OutputTexture = FIVSmokePostProcessPass::CreateOutputTexture(
			GraphBuilder,
			SceneColor.Texture,
			TEXT("IVSmokeDepthSortedOutput"),
			PF_FloatRGBA,
			ViewportSize
		);

		FScreenPassRenderTarget SortedOutput(
			OutputTexture,
			SceneColor.ViewRect,
			ERenderTargetLoadAction::ENoAction
		);

		AddDepthSortedCompositePass(
			GraphBuilder,
			View,
			SceneColor.Texture,
			SmokeAlbedoFull,
			SmokeMaskFull,
			ParticlesTex.Texture,
			SortedOutput
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

		// Create a NEW output texture to avoid read/write conflict
		// (Cannot read ParticlesTex while writing to the same texture)
		FRDGTextureRef OutputTexture = FIVSmokePostProcessPass::CreateOutputTexture(
			GraphBuilder,
			ParticlesTex.Texture,
			TEXT("IVSmokeTranslucencyOutput"),
			PF_FloatRGBA,
			ParticlesTex.ViewRect.Size()
		);

		FScreenPassRenderTarget TranslucencyOutput(
			OutputTexture,
			ParticlesTex.ViewRect,
			ERenderTargetLoadAction::ENoAction
		);

		AddTranslucencyCompositePass(
			GraphBuilder,
			View,
			SmokeAlbedoFull,
			SmokeMaskFull,
			ParticlesTex.Texture,
			TranslucencyOutput,
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
	const TArray<AIVSmokeVoxelVolume*>& SortedVolumes,
	FRDGTextureRef SmokeAlbedoTex,
	FRDGTextureRef SmokeMaskTex,
	const FIntPoint& TexSize,
	const FIntPoint& ViewportSize,
	const FIntPoint& ViewRectMin)
{
	int32 VolumeCount = SortedVolumes.Num();

	if (VolumeCount == 0 || !NoiseVolume)
	{
		return;
	}

	// Performance warning for too many volumes
	if (VolumeCount > 8)
	{
		UE_LOG(LogIVSmoke, Warning, TEXT("[FIVSmokeRenderer] Performance warning: %d volumes active (recommended: 8 or fewer)"), VolumeCount);
	}



	//크기가 다를 땐 단순히 아틀라스 채워넣는 기법이 추가되므로 일단 배제

	// ============================================================================
	// Build Volume GPU Data and Pack Voxel Buffers
	// ============================================================================

	//texture size set
	int32 TexturePackInterval = 4;
	TArray<FIVSmokeVolumeGPUData> VolumeDataArray;
	TArray<float> PackedVoxelData;
	TArray<float> VoxelIntervalData;

	FIntVector VoxelResolution = SortedVolumes[0]->GetGridResolution();
	FIntVector VoxelHighResolution = VoxelResolution * 1;
	FIntVector HoleResolution = SortedVolumes[0]->GetHoleGeneratorComponent()->GetHoleTexture()->GetSizeXYZ();

	FIntVector VoxelAtlasResolution = FIntVector(VoxelResolution.X, VoxelResolution.Y, VoxelResolution.Z * VolumeCount + TexturePackInterval * (VolumeCount - 1));
	FIntVector VoxelAtlasHighResolution = VoxelAtlasResolution * 1;
	FIntVector HoleAtlasResolution = FIntVector(HoleResolution.X, HoleResolution.Y, HoleResolution.Z * VolumeCount + TexturePackInterval * (VolumeCount - 1));

	int32 TotalVoxelSize = VoxelAtlasResolution.X * VoxelAtlasResolution.Y * VoxelAtlasResolution.Z;
	int32 TotalHoleSize = HoleAtlasResolution.X * HoleAtlasResolution.Y * HoleAtlasResolution.Z;
	VolumeDataArray.Reserve(VolumeCount);
	PackedVoxelData.Reserve(TotalVoxelSize);
	VoxelIntervalData.Init(0, VoxelResolution.X * VoxelResolution.Y * TexturePackInterval);
	HoleAtlasResolution = HoleAtlasResolution == FIntVector::ZeroValue ? FIntVector(64, 64, 64) : HoleAtlasResolution;

	//atlas texture create
	FRDGTextureDesc VoxelAtlasDesc = FRDGTextureDesc::Create3D(VoxelAtlasResolution, PF_R32_FLOAT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef PackedVoxelAtlas = GraphBuilder.CreateTexture(VoxelAtlasDesc, TEXT("IVSmoke_PackedVoxelAtlas"));
	FRDGTextureDesc VoxelAtlasHighResDesc = FRDGTextureDesc::Create3D(VoxelAtlasHighResolution, PF_R32_FLOAT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef PackedVoxelAtlasHighRes = GraphBuilder.CreateTexture(VoxelAtlasHighResDesc, TEXT("IVSmoke_PackedVoxelAtlasHighRes"));
	FRDGTextureDesc HoleAtlas = FRDGTextureDesc::Create3D(HoleAtlasResolution, PF_R16F, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef PackedHoleAtlas = GraphBuilder.CreateTexture(HoleAtlas, TEXT("IVSmoke_PackedHoleAtlas"));

	FRDGTextureSRVRef PackedVoxelAtlasSRV = GraphBuilder.CreateSRV(PackedVoxelAtlas);
	FRDGTextureSRVRef PackedVoxelAtlasHighResSRV = GraphBuilder.CreateSRV(PackedVoxelAtlas);

	//copy hole texture to atlas
	//set PackedVoxelData array
	//set FIVSmokeVolumeGPUData
	FRHICopyTextureInfo HoleCpyInfo;
	HoleCpyInfo.Size = HoleResolution;
	HoleCpyInfo.SourcePosition = FIntVector::ZeroValue;

	for (int32 i = 0; i < VolumeCount; ++i)
	{
		AIVSmokeVoxelVolume* Volume = SortedVolumes[i];
		FTextureRHIRef SourceRHI = Volume->GetHoleTexture();
		if (SourceRHI == nullptr)
		{
			continue;
		}

		// ============================================================================
		// copy hole texture to atlas
		// ============================================================================
		FRDGTextureRef SourceTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(SourceRHI, TEXT("IVSmoke_CopyHoleSource")));
		HoleCpyInfo.DestPosition = FIntVector(0, 0, i * (HoleResolution.Z + TexturePackInterval));
		AddCopyTexturePass(GraphBuilder, SourceTexture, PackedHoleAtlas, HoleCpyInfo);

		// ============================================================================
		// set PackedVoxelData array
		// ============================================================================
		const TArray<float>& VoxelData = Volume->GetVoxelArray();
		PackedVoxelData.Append(VoxelData);
		if (i < VolumeCount - 1)
		{
			PackedVoxelData.Append(VoxelIntervalData);
		}

		// ============================================================================
		// set FIVSmokeVolumeGPUData
		// ============================================================================
		const FIntVector GridRes = Volume->GetGridResolution();
		const FIntVector CenterOff = Volume->GetCenterOffset();
		const float VoxelSz = Volume->GetVoxelSize();
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

		GPUData.VoxelSize = VoxelSz;
		GPUData.VoxelBufferOffset = VoxelResolution.X * VoxelResolution.Y * (VoxelResolution.Z + TexturePackInterval) * i;
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
	}

	if (VolumeDataArray.Num() == 0 || PackedVoxelData.Num() == 0)
	{
		return;
	}

	// Get global settings for rendering parameters
	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();

	// ============================================================================
	// StructuredToTexture Pass
	// ============================================================================
	FRDGBufferDesc PackedVoxelBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(float), PackedVoxelData.Num());
	FRDGBufferRef PackedVoxelBuffer = GraphBuilder.CreateBuffer(PackedVoxelBufferDesc, TEXT("IVSmoke_PackedVoxelBuffer"));
	GraphBuilder.QueueBufferUpload(PackedVoxelBuffer, PackedVoxelData.GetData(), PackedVoxelData.Num() * sizeof(float));

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeStructuredToTextureCS> StructuredCopyShader(ShaderMap);
	auto* StructuredCopyParams = GraphBuilder.AllocParameters<FIVSmokeStructuredToTextureCS::FParameters>();
	StructuredCopyParams->Desti = GraphBuilder.CreateUAV(PackedVoxelAtlas);
	StructuredCopyParams->Source = GraphBuilder.CreateSRV(PackedVoxelBuffer);
	StructuredCopyParams->TexSize = VoxelAtlasResolution;

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

	VoxelFXAAParams->Desti = GraphBuilder.CreateUAV(PackedVoxelAtlasHighRes);
	VoxelFXAAParams->Source = GraphBuilder.CreateSRV(PackedVoxelAtlas);
	VoxelFXAAParams->LinearBorder_Sampler = TStaticSamplerState<SF_Bilinear, AM_Border, AM_Border, AM_Border>::GetRHI();
	VoxelFXAAParams->TexSize = VoxelAtlasHighResolution;
	VoxelFXAAParams->FXAASpanMax = Settings->FXAASpanMax;
	VoxelFXAAParams->FXAARange = Settings->FXAARange;
	VoxelFXAAParams->FXAASharpness = Settings->FXAASharpness;

	FIVSmokePostProcessPass::AddComputeShaderPass<FIVSmokeVoxelFXAACS>(
		GraphBuilder,
		ShaderMap,
		VoxelFXAAShader,
		VoxelFXAAParams,
		VoxelAtlasHighResolution );

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
	Parameters->MaxSteps = Settings->MaxSteps;

	// Volume Data Buffer
	FRDGBufferDesc VolumeBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FIVSmokeVolumeGPUData), VolumeDataArray.Num());
	FRDGBufferRef VolumeBuffer = GraphBuilder.CreateBuffer(VolumeBufferDesc, TEXT("IVSmokeVolumeDataBuffer"));
	GraphBuilder.QueueBufferUpload(VolumeBuffer, VolumeDataArray.GetData(), VolumeDataArray.Num() * sizeof(FIVSmokeVolumeGPUData));
	Parameters->VolumeDataBuffer = GraphBuilder.CreateSRV(VolumeBuffer);
	Parameters->NumActiveVolumes = VolumeDataArray.Num();

	// Packed Textures
	Parameters->PackedInterval = TexturePackInterval;
	Parameters->PackedVoxelAtlas = GraphBuilder.CreateSRV(PackedVoxelAtlasHighRes);
	Parameters->VoxelTexSize = VoxelHighResolution;
	//Parameters->PackedVoxelAtlas = GraphBuilder.CreateSRV(PackedVoxelAtlas);
	//Parameters->VoxelTexSize = VoxelResolution;
	Parameters->PackedHoleAtlas = GraphBuilder.CreateSRV(PackedHoleAtlas);
	Parameters->HoleTexSize = HoleResolution;

	// Scene Textures
	Parameters->SceneTexturesStruct = GetSceneTextureShaderParameters(View).SceneTextures;
	Parameters->InvDeviceZToWorldZTransform = FVector4f(View.InvDeviceZToWorldZTransform);

	// View (for BlueNoise access)
	Parameters->View = View.ViewUniformBuffer;

	// Global Smoke Parameters (from Settings)
	Parameters->GlobalAbsorption = 0.1f; // Default value, per-volume absorption is used from preset
	Parameters->SmokeSize = Settings->SmokeSize;
	Parameters->SmokeDensityFalloff = Settings->SmokeDensityFalloff;
	Parameters->WindDirection = FVector3f(Settings->WindDirection);

	// Rayleigh Scattering (from Settings)
	const float ScatterScaleValue = Settings->ScatterScale;
	const bool bEnableScatter = Settings->bEnableScattering;

	// Light Direction - use settings override or default overhead sun
	FVector3f LightDir = FVector3f(0.2f, 0.1f, 0.9f).GetSafeNormal();
	if (Settings->bOverrideLightDirection)
	{
		LightDir = FVector3f(Settings->LightDirectionOverride.GetSafeNormal());
	}

	// Light Color - use settings override or default warm white
	FVector3f LightColorValue = FVector3f(1.0f, 0.95f, 0.9f);
	if (Settings->bOverrideLightColor)
	{
		LightColorValue = FVector3f(
			Settings->LightColorOverride.R,
			Settings->LightColorOverride.G,
			Settings->LightColorOverride.B
		);
	}

	Parameters->LightDirection = LightDir;
	Parameters->LightColor = LightColorValue;
	Parameters->ScatterScale = bEnableScatter ? ScatterScaleValue : 0.0f;

	// Henyey-Greenstein Anisotropy
	Parameters->ScatteringAnisotropy = Settings->ScatteringAnisotropy;

	// Self-Shadowing (Light Marching) - from Settings
	const bool bSelfShadow = Settings->bEnableSelfShadowing;
	Parameters->LightMarchingSteps = bSelfShadow ? Settings->LightMarchingSteps : 0;
	Parameters->LightMarchingDistance = Settings->LightMarchingDistance;
	Parameters->LightMarchingExpFactor = Settings->LightMarchingExpFactor;
	Parameters->ShadowAmbient = Settings->ShadowAmbient;

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
	float Sharpness)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeTranslucencyCompositePS> PixelShader(ShaderMap);

	// Get texture sizes for UV calculation (avoids GetDimensions() in shader)
	const FIntPoint SmokeTexSize = SmokeAlbedoTex->Desc.Extent;
	const FIntPoint ParticlesTexSize = ParticlesTex->Desc.Extent;

	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeTranslucencyCompositePS::FParameters>();
	Parameters->SmokeAlbedoTex = SmokeAlbedoTex;
	Parameters->SmokeMaskTex = SmokeMaskTex;
	Parameters->ParticlesTex = ParticlesTex;
	Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->Sharpness = Sharpness;
	Parameters->ViewRectMin = FVector2f(Output.ViewRect.Min);
	Parameters->SmokeTexSize = FVector2f(SmokeTexSize);
	Parameters->ParticlesTexSize = FVector2f(ParticlesTexSize);
	Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FIVSmokePostProcessPass::AddPixelShaderPass< FIVSmokeTranslucencyCompositePS>(GraphBuilder, ShaderMap, PixelShader, Parameters, Output);
}

void FIVSmokeRenderer::AddDepthSortedCompositePass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef SceneTex,
	FRDGTextureRef SmokeAlbedoTex,
	FRDGTextureRef SmokeMaskTex,
	FRDGTextureRef SeparateTranslucencyTex,
	const FScreenPassRenderTarget& Output)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeDepthSortedCompositePS> PixelShader(ShaderMap);

	const FIntPoint ViewportSize = Output.ViewRect.Size();

	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeDepthSortedCompositePS::FParameters>();

	// Scene background
	Parameters->SceneTex = SceneTex;

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

	// Viewport
	Parameters->ViewportSize = FVector2f(ViewportSize);
	Parameters->ViewRectMin = FVector2f(Output.ViewRect.Min);
	Parameters->InvDeviceZToWorldZTransform = FVector4f(View.InvDeviceZToWorldZTransform);

	// Render target
	Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FIVSmokePostProcessPass::AddPixelShaderPass<FIVSmokeDepthSortedCompositePS>(GraphBuilder, ShaderMap, PixelShader, Parameters, Output);
}
