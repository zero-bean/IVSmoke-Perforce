// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeRenderer.h"

#include "IVSmokePostProcessPass.h"
#include "IVSmokeShaders.h"
#include "IVSmokeVolumeComponent.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "PostProcess/PostProcessMaterialInputs.h"

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

	// Create intermediate texture with ViewRect size for smoke compositing
	FRDGTextureRef SmokeTexture = FIVSmokePostProcessPass::CreateUAVOutputTexture(
		GraphBuilder,
		SceneColor.Texture,
		TEXT("IVSmokeResultTexture"),
		PF_FloatRGBA,
		ViewportSize
	);

	AddRayMarchPass(GraphBuilder, View, SmokeTexture, ViewportSize);
	AddCompositePass(GraphBuilder, View, SmokeTexture, Output, ViewportSize);

	return Output;
}

void FIVSmokeRenderer::AddVolume(UIVSmokeVolumeComponent* Volume)
{
	FScopeLock Lock(&VolumesMutex);
	Volumes.AddUnique(Volume);
}

void FIVSmokeRenderer::RemoveVolume(UIVSmokeVolumeComponent* Volume)
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

TArray<FBox> FIVSmokeRenderer::GatherVolumeBounds() const
{
	FScopeLock Lock(&VolumesMutex);

	TArray<FBox> Result;
	for (const auto& WeakVolume : Volumes)
	{
		if (auto* Volume = WeakVolume.Get())
		{
			if (Volume->IsVisible())
			{
				Result.Add(Volume->GetWorldBounds());
			}
		}
	}
	return Result;
}

// ============================================================================
// Pass Functions
// ============================================================================

void FIVSmokeRenderer::AddRayMarchPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef OutputTexture,
	const FIntPoint& ViewportSize)
{
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

	// Camera (Ray reconstruction using vectors)
	const FViewMatrices& ViewMatrices = View.ViewMatrices;
	Parameters->CameraPosition = FVector3f(ViewMatrices.GetViewOrigin());

	// Get camera orientation vectors from view matrix
	const FMatrix& ViewMatrix = ViewMatrices.GetViewMatrix();
	// UE5 view matrix: X=Right, Y=Forward, Z=Up (in view space)
	// The inverse of view matrix gives us world space directions
	FVector CameraForward = ViewMatrix.GetColumn(2);  // View Z axis = Forward
	FVector CameraRight = ViewMatrix.GetColumn(0);    // View X axis = Right
	FVector CameraUp = ViewMatrix.GetColumn(1);       // View Y axis = Up

	Parameters->CameraForward = FVector3f(View.GetViewDirection());
	Parameters->CameraRight = FVector3f(View.GetViewRight());
	Parameters->CameraUp = FVector3f(View.GetViewUp());

	// Calculate FOV from projection matrix
	// ProjectionMatrix[0][0] = 1 / (tan(HalfFOV) * AspectRatio)
	// ProjectionMatrix[1][1] = 1 / tan(HalfFOV)
	const FMatrix& ProjMatrix = ViewMatrices.GetProjectionMatrix();
	float TanHalfFOV = 1.0f / ProjMatrix.M[1][1];
	float AspectRatio = (float)ViewportSize.X / (float)ViewportSize.Y;

	Parameters->TanHalfFOV = TanHalfFOV;
	Parameters->AspectRatio = AspectRatio;

	// Ray Marching Setup (TODO: Make configurable)
	Parameters->MaxSteps = 32;

	// Volume Data (TODO: Get from registered volumes)
	TArray<FBox> VolumeBounds = GatherVolumeBounds();
	if (VolumeBounds.Num() > 0)
	{
		const FBox& Bounds = VolumeBounds[0];
		Parameters->VolumeMin = FVector3f(Bounds.Min);
		Parameters->VolumeMax = FVector3f(Bounds.Max);
	}
	else
	{
		// Default test volume
		Parameters->VolumeMin = FVector3f(-300.0f, -300.0f, -300.0f);
		Parameters->VolumeMax = FVector3f(300.0f, 300.0f, 300.0f);
	}
	Parameters->VolumeDensity = 0.3f;
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
