// Copyright (c) 2026, Team SDB. All rights reserved.

#include "IVSmokeSceneViewExtension.h"
#include "IVSmokeRenderer.h"
#include "IVSmokeSettings.h"
#include "IVSmokeVoxelVolume.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "ScreenPass.h"
#include "RenderingThread.h"

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

void FIVSmokeSceneViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	// Called ONCE per frame on Game Thread (not per-view!)
	// This ensures render data is prepared exactly once per frame
	FIVSmokeRenderer& Renderer = FIVSmokeRenderer::Get();

	// Skip if no volumes
	if (!Renderer.HasVolumes())
	{
		return;
	}

	// Collect valid volumes under lock
	TArray<AIVSmokeVoxelVolume*> ValidVolumes;
	{
		FScopeLock Lock(&Renderer.GetVolumesMutex());
		for (const auto& WeakVolume : Renderer.GetVolumes())
		{
			if (AIVSmokeVoxelVolume* Volume = WeakVolume.Get())
			{
				ValidVolumes.Add(Volume);
			}
		}
	}

	if (ValidVolumes.Num() == 0)
	{
		return;
	}

	// Prepare render data on Game Thread (all Volume data access happens here)
	FIVSmokePackedRenderData RenderData = Renderer.PrepareRenderData(ValidVolumes);

	// Transfer to Render Thread via command queue
	ENQUEUE_RENDER_COMMAND(IVSmokeSetRenderData)(
		[&Renderer, RenderData = MoveTemp(RenderData)](FRHICommandListImmediate& RHICmdList) mutable
		{
			Renderer.SetCachedRenderData(MoveTemp(RenderData));
		}
	);
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
