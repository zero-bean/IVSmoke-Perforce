// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeRenderer.h"

#include "IVSmokePostProcessPass.h"
#include "IVSmokeShaders.h"
#include "IVSmokeVolumeComponent.h"
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

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget(
			SceneColor.Texture,
			SceneColor.ViewRect,
			View.GetOverwriteLoadAction()
		);
	}

	TShaderMapRef<FIVSmokeTestPS> PixelShader(ShaderMap);
	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeTestPS::FParameters>();

	// 파라미터 채우기...
	Parameters->TintColor = FLinearColor::Red;
	Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	// Dispatch
	FIVSmokePassConfig Config;
	Config.EventName = TEXT("IVSmokeTest");
	Config.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();

	FIVSmokePostProcessPass::AddPixelShaderPass(
		GraphBuilder,
		ShaderMap,
		PixelShader,
		Parameters,
		Output,
		Config
	);
	return MoveTemp(Output);
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
