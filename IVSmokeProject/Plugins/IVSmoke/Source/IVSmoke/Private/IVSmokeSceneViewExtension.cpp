// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeSceneViewExtension.h"
#include "IVSmokeRenderer.h"
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
	if (Pass == EPostProcessingPass::BeforeDOF)
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
