// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeSceneViewExtension.h"
#include "IVSmokeRenderer.h"
#include "IVSmokeShaders.h"

// FPostProcessingInputs: Required for PrePostProcessPass_RenderThread scene texture access.
// Defined in Renderer/Internal - no public alternative exists for this rendering hook.
#include "PostProcess/PostProcessInputs.h"

#include "PixelShaderUtils.h"

TSharedPtr<FIVSmokeSceneViewExtension, ESPMode::ThreadSafe> FIVSmokeSceneViewExtension::Instance;

FIVSmokeSceneViewExtension::FIVSmokeSceneViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

void FIVSmokeSceneViewExtension::Initialize()
{
	if (!Instance.IsValid())
	{
		Instance = FSceneViewExtensions::NewExtension<FIVSmokeSceneViewExtension>();
	}
}

void FIVSmokeSceneViewExtension::Shutdown()
{
	Instance.Reset();
}

bool FIVSmokeSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return FIVSmokeRenderer::Get().HasVolumes();
}

namespace
{
	void RenderSmokeVolume(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs, const FBox& Bounds)
	{
		FRDGTextureRef SceneColor = Inputs.SceneTextures->GetContents()->SceneColorTexture;
		if (!SceneColor)
		{
			return;
		}

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);

		FIVSmokeRayMarchPS::FParameters* Parameters = GraphBuilder.AllocParameters<FIVSmokeRayMarchPS::FParameters>();
		Parameters->BoundsMin = FVector3f(Bounds.Min);
		Parameters->BoundsMax = FVector3f(Bounds.Max);
		Parameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);

		TShaderMapRef<FIVSmokeRayMarchPS> PixelShader(ShaderMap);

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("IVSmoke Test"),
			PixelShader,
			Parameters,
			View.UnscaledViewRect,
			TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI());
	}
}

void FIVSmokeSceneViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)
{
	TArray<FBox> VolumeBounds = FIVSmokeRenderer::Get().GatherVolumeBounds();
	if (VolumeBounds.IsEmpty())
	{
		return;
	}

	if (!Inputs.SceneTextures)
	{
		return;
	}

	for (const FBox& Bounds : VolumeBounds)
	{
		RenderSmokeVolume(GraphBuilder, View, Inputs, Bounds);
	}
}
