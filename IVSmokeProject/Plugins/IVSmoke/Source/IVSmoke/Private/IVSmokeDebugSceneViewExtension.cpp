// Copyright SDB. All Rights Reserved.

#include "IVSmokeDebugSceneViewExtension.h"
#include "IVSmokeVolumeDebugRenderer.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "ScreenPass.h"

TSharedPtr<FIVSmokeDebugSceneViewExtension, ESPMode::ThreadSafe> FIVSmokeDebugSceneViewExtension::Instance;

FIVSmokeDebugSceneViewExtension::FIVSmokeDebugSceneViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

void FIVSmokeDebugSceneViewExtension::Initialize()
{
	if (!Instance.IsValid())
	{
		Instance = FSceneViewExtensions::NewExtension<FIVSmokeDebugSceneViewExtension>();
	}
}

void FIVSmokeDebugSceneViewExtension::Shutdown()
{
	Instance.Reset();
}

bool FIVSmokeDebugSceneViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	// Use HasAnyComponents() instead of HasDebugComponents() to avoid game object access from render thread
	return FIVSmokeVolumeDebugRenderer::Get().HasAnyComponents();
}

void FIVSmokeDebugSceneViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass,
	const FSceneView& InView,
	FPostProcessingPassDelegateArray& InOutPassCallbacks,
	bool bIsPassEnabled)
{
	// Render after DOF so debug UI is always visible
	if (Pass == EPostProcessingPass::MotionBlur)
	{
		InOutPassCallbacks.Add(
			FPostProcessingPassDelegate::CreateRaw(
				this,
				&FIVSmokeDebugSceneViewExtension::Render_RenderThread
			)
		);
	}
}

FScreenPassTexture FIVSmokeDebugSceneViewExtension::Render_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	FScreenPassTextureSlice SceneColorSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
	if (!SceneColorSlice.IsValid())
	{
		return FScreenPassTexture();
	}

	FScreenPassTexture SceneColor(SceneColorSlice);

	return FIVSmokeVolumeDebugRenderer::Get().Render(GraphBuilder, View, SceneColor);
}
