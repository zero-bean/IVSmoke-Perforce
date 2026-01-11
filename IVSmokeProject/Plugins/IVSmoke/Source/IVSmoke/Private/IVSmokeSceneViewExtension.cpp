// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeSceneViewExtension.h"
#include "IVSmokeRenderer.h"
#include "IVSmokeSettings.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "ScreenPass.h"

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

void FIVSmokeSceneViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass,
	const FSceneView& InView,
	FPostProcessingPassDelegateArray& InOutPassCallbacks,
	bool bIsPassEnabled)
{
	// Map IVSmoke render pass setting to engine post-processing pass
	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();
	EIVSmokeRenderPass RenderPassSetting = Settings ? Settings->RenderPass : EIVSmokeRenderPass::TranslucencyAfterDOF;

	EPostProcessingPass TargetPass;
	switch (RenderPassSetting)
	{
	case EIVSmokeRenderPass::BeforeDOF:
		TargetPass = EPostProcessingPass::BeforeDOF;
		break;
	case EIVSmokeRenderPass::AfterDOF:
		TargetPass = EPostProcessingPass::AfterDOF;
		break;
	case EIVSmokeRenderPass::TranslucencyAfterDOF:
		TargetPass = EPostProcessingPass::TranslucencyAfterDOF;
		break;
	case EIVSmokeRenderPass::MotionBlur:
		TargetPass = EPostProcessingPass::MotionBlur;
		break;
	case EIVSmokeRenderPass::Tonemap:
		TargetPass = EPostProcessingPass::Tonemap;
		break;
	default:
		TargetPass = EPostProcessingPass::TranslucencyAfterDOF;
		break;
	}

	if (Pass == TargetPass)
	{
		InOutPassCallbacks.Add(
			FPostProcessingPassDelegate::CreateRaw(
				this,
				&FIVSmokeSceneViewExtension::Render_RenderThread
			)
		);
	}
}

FScreenPassTexture FIVSmokeSceneViewExtension::Render_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	return FIVSmokeRenderer::Get().Render(GraphBuilder, View, Inputs);
}
