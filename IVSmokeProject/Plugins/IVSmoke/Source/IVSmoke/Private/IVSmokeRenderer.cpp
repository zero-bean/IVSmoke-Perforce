// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeRenderer.h"

#include "IVSmokePostProcessPass.h"
#include "IVSmokeShaders.h"
#include "IVSmokeVolumeComponent.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "IVSmokeVoxelVolume.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "SceneRenderTargetParameters.h"

FIVSmokeRenderer& FIVSmokeRenderer::Get()
{
	static FIVSmokeRenderer Instance;
	return Instance;
}

FScreenPassTexture FIVSmokeRenderer::Render(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
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
	FRDGTextureRef SmokeTexture = FIVSmokePostProcessPass::CreateUAVOutputTexture(
		GraphBuilder,
		SceneColor.Texture,
		TEXT("IVSmokeResultTexture"),
		PF_FloatRGBA,
		ViewportSize
	);

	AddRayMarchPass(GraphBuilder, View, SmokeTexture, ViewportSize, ViewRectMin);
	AddCompositePass(GraphBuilder, View, SmokeTexture, Output, ViewportSize);

	return Output;
}

void FIVSmokeRenderer::AddVolume(AIVSmokeVoxelVolume* Volume)
{
	FScopeLock Lock(&VolumesMutex);
	Volumes.AddUnique(Volume);
}

void FIVSmokeRenderer::RemoveVolume(AIVSmokeVoxelVolume* Volume)
{
	FScopeLock Lock(&VolumesMutex);
	Volumes.Remove(Volume);
}
void FIVSmokeRenderer::SetNoiseVolume(UTextureRenderTargetVolume* InNoiseVolume)
{
	NoiseVolume = InNoiseVolume;
}

bool FIVSmokeRenderer::HasVolumes() const
{
	FScopeLock Lock(&VolumesMutex);
	return Volumes.Num() > 0;
}

// ============================================================================
// Pass Functions
// ============================================================================

void FIVSmokeRenderer::AddRayMarchPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef OutputTexture,
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

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeRayMarchCS> ComputeShader(ShaderMap);

	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeRayMarchCS::FParameters>();

	// Output
	Parameters->OutputTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputTexture));

	// Input Texture
	FTextureRHIRef TextureRHI =
		NoiseVolume->GetRenderTargetResource()->GetRenderTargetTexture();
	FRDGTextureRef NoiseVolumeRDG =
		GraphBuilder.RegisterExternalTexture(
			CreateRenderTarget(TextureRHI, TEXT("NoiseVolumeRTV"))
		);
	Parameters->NoiseVolume = NoiseVolumeRDG;

	// Sampler
	Parameters->LinearRepeat_Sampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

	// DeltaTime
	static int FrameCount = 0;
	Parameters->ElapseTime = FrameCount / 60.0f;
	FrameCount++;

	// Viewport
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

	// Ray Marching Setup
	Parameters->MaxSteps = 64;

	// ============================================================================
	// Voxel Volume Data
	// ============================================================================

	// Note: RDG buffers are transient, so we must upload every frame.
	// For true persistent buffer optimization, use FRDGPooledBuffer (requires more complex setup).
	// The dirty flag is kept for future optimization when persistent buffers are implemented.
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

	// Scene Textures (uniform buffer - named SceneTexturesStruct for UE helper compatibility)
	Parameters->SceneTexturesStruct = GetSceneTextureShaderParameters(View).SceneTextures;
	Parameters->InvDeviceZToWorldZTransform = FVector4f(View.InvDeviceZToWorldZTransform);

	// Smoke rendering parameters
	Parameters->VolumeDensity = 1.0f;
	Parameters->SmokeColor = FVector3f(0.8f, 0.8f, 0.8f);
	Parameters->SmokeAbsorption = 0.1f;
	Parameters->SmokeSize = 128.0f;
	Parameters->SmokeDensityFalloff = 0.2f;

	// Wind Animation
	Parameters->WindDirection = FVector3f(0.01f, 0.02f, 0.1f);

	FIVSmokePassConfig Config;
	Config.EventName = TEXT("IVSmokeRayMarch");
	Config.ThreadGroupSizeX = FIVSmokeRayMarchCS::ThreadGroupSizeX;
	Config.ThreadGroupSizeY = FIVSmokeRayMarchCS::ThreadGroupSizeY;

	FIVSmokePostProcessPass::AddComputeShaderPass(
		GraphBuilder,
		ShaderMap,
		ComputeShader,
		Parameters,
		ViewportSize,
		Config
	);
}

void FIVSmokeRenderer::AddCompositePass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef SmokeTexture,
	const FScreenPassRenderTarget& Output,
	const FIntPoint& ViewportSize)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeCompositePS> PixelShader(ShaderMap);

	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeCompositePS::FParameters>();
	Parameters->ViewportSize = FVector2f(ViewportSize);
	Parameters->ViewRectMin = FVector2f(Output.ViewRect.Min);
	Parameters->SmokeTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(SmokeTexture));
	Parameters->TextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FIVSmokePassConfig Config;
	Config.EventName = TEXT("IVSmokeComposite");
	Config.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();

	FIVSmokePostProcessPass::AddPixelShaderPass(
		GraphBuilder,
		ShaderMap,
		PixelShader,
		Parameters,
		Output,
		Config
	);
}
