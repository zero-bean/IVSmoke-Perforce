// Copyright (c) 2026, Team SDB. All rights reserved.

#include "IVSmokeSceneViewExtension.h"
#include "IVSmokeRenderer.h"
#include "IVSmokeSettings.h"
#include "IVSmokeShaders.h"
#include "IVSmokeVoxelVolume.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "ScreenPass.h"
#include "RenderingThread.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "PixelShaderUtils.h"
#include "SceneTexturesConfig.h"
#include "SceneRenderTargetParameters.h"

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

	// Get world from ViewFamily
	UWorld* World = nullptr;
	if (InViewFamily.Scene)
	{
		World = InViewFamily.Scene->GetWorld();
	}

	if (!World)
	{
		return;
	}

	// Sync server time if needed
	if (!Renderer.bIsServerTimeSynced())
	{
		if (AGameStateBase* GS = World->GetGameState())
		{
			float LocalTime = World->GetTimeSeconds();
			float ServerTime = GS->GetServerWorldTimeSeconds();
			Renderer.SetServerTimeOffset(ServerTime - LocalTime);
		}
	}

	// Collect renderable volumes using TActorIterator (Pull-based pattern)
	TArray<AIVSmokeVoxelVolume*> ValidVolumes;
	for (TActorIterator<AIVSmokeVoxelVolume> It(World); It; ++It)
	{
		if (It->ShouldRender())
		{
			ValidVolumes.Add(*It);
		}
	}

	if (ValidVolumes.Num() == 0)
	{
		// Clear cached render data to stop rendering
		ENQUEUE_RENDER_COMMAND(IVSmokeClearRenderData)(
			[&Renderer](FRHICommandListImmediate& RHICmdList)
			{
				Renderer.SetCachedRenderData(FIVSmokePackedRenderData());
			}
		);
		return;
	}

	// Get camera position from first view for distance-based filtering
	FVector CameraPosition = FVector::ZeroVector;
	if (InViewFamily.Views.Num() > 0 && InViewFamily.Views[0])
	{
		CameraPosition = InViewFamily.Views[0]->ViewLocation;
	}

	// Prepare render data on Game Thread (all Volume data access happens here)
	FIVSmokePackedRenderData RenderData = Renderer.PrepareRenderData(ValidVolumes, CameraPosition);
	
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
	// Always active - actual filtering happens in BeginRenderViewFamily via TActorIterator
	// This is intentional: the cost of iterating 128 volumes per frame is negligible (~1μs)
	return true;
}

void FIVSmokeSceneViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass,
	const FSceneView& InView,
	FPostProcessingPassDelegateArray& InOutPassCallbacks,
	bool bIsPassEnabled)
{
	// Always use AfterDOF pass - DOF applied to smoke, best balance of quality and compatibility
	if (Pass == EPostProcessingPass::AfterDOF)
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

void FIVSmokeSceneViewExtension::PostRenderBasePassDeferred_RenderThread(
	FRDGBuilder& GraphBuilder,
	FSceneView& InView,
	const FRenderTargetBindingSlots& RenderTargets,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
	const UIVSmokeSettings* Settings = UIVSmokeSettings::Get();
	if (!Settings)
	{
		return;
	}

	// Pre-pass Pipeline: Ray March → Upscale → UpsampleFilter (→ Depth Write if enabled)
	// Always runs when smoke rendering is enabled.
	// Results are cached in View-based RDG cache for Post-process Visual/Composite passes.
	if (Settings->bEnableSmokeRendering)
	{
		FIVSmokeRenderer::Get().RunPrePassPipeline(GraphBuilder, InView, RenderTargets, SceneTextures);
	}
}

void FIVSmokeSceneViewExtension::PostRenderViewFamily_RenderThread(
	FRDGBuilder& GraphBuilder,
	FSceneViewFamily& InViewFamily)
{
	// Clear View-based RDG caches at end of frame
	// RDG textures are only valid within the same GraphBuilder, so clear the map for next frame
	FIVSmokeRenderer::Get().ClearFrameViewCaches();
}
