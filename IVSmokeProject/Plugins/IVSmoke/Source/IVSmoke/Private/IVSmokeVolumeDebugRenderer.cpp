// Copyright SDB. All Rights Reserved.

#include "IVSmokeVolumeDebugRenderer.h"

#include "IVSmokeCollisionComponent.h"
#include "IVSmokeDebugShaders.h"
#include "IVSmokePostProcessPass.h"
#include "PixelShaderUtils.h"
#include "RenderGraphBuilder.h"

FIVSmokeVolumeDebugRenderer& FIVSmokeVolumeDebugRenderer::Get()
{
	static FIVSmokeVolumeDebugRenderer Instance;
	return Instance;
}

void FIVSmokeVolumeDebugRenderer::Register(UIVSmokeCollisionComponent* Component)
{
	if (!Component)
	{
		return;
	}

	FScopeLock Lock(&ComponentsMutex);
	DebugComponents.AddUnique(Component);
}

void FIVSmokeVolumeDebugRenderer::Unregister(UIVSmokeCollisionComponent* Component)
{
	if (!Component)
	{
		return;
	}

	{
		FScopeLock Lock(&ComponentsMutex);
		DebugComponents.Remove(Component);
	}

	{
		FScopeLock Lock(&RenderDataMutex);
		CachedRenderData.Remove(Component->GetUniqueID());
	}
}

bool FIVSmokeVolumeDebugRenderer::HasAnyComponents() const
{
	FScopeLock Lock(&RenderDataMutex);

	for (const auto& Pair : CachedRenderData)
	{
		if (Pair.Value.bIsValid)
		{
			return true;
		}
	}
	return false;
}

void FIVSmokeVolumeDebugRenderer::UpdateRenderData(UIVSmokeCollisionComponent* Component)
{
	if (!Component)
	{
		return;
	}

	const uint32 ComponentID = Component->GetUniqueID();

	// If debug visualization is disabled, remove from cache
	if (!Component->bShowVolumeSlice)
	{
		FScopeLock Lock(&RenderDataMutex);
		CachedRenderData.Remove(ComponentID);
		return;
	}

	// Get texture and resource
	UVolumeTexture* VolumeTexture = Component->GetHoleDataTexture();
	if (!VolumeTexture)
	{
		return;
	}

	FTextureResource* TextureResource = VolumeTexture->GetResource();
	if (!TextureResource)
	{
		return;
	}

	FTextureRHIRef TextureRHI = TextureResource->TextureRHI;
	if (!TextureRHI.IsValid())
	{
		return;
	}

	// Update cache directly (game thread)
	FScopeLock Lock(&RenderDataMutex);

	FIVSmokeDebugRenderData& Data = CachedRenderData.FindOrAdd(ComponentID);
	Data.VolumeTextureRHI = TextureRHI;
	Data.Resolution = Component->GetVoxelResolution();
	Data.SliceIndex = Component->DebugSliceIndex;
	Data.DebugMode = Component->DebugMode;
	Data.CurrentTime = Component->GetWorld()->GetTimeSeconds();
	Data.HoleLifeTime = Component->GetHoleLifeTime();
	Data.bIsValid = true;
}

FScreenPassTexture FIVSmokeVolumeDebugRenderer::Render(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassTexture& SceneColor)
{
	if (!SceneColor.IsValid())
	{
		return FScreenPassTexture();
	}

	FScreenPassRenderTarget Output = FScreenPassRenderTarget(
		SceneColor.Texture,
		SceneColor.ViewRect,
		View.GetOverwriteLoadAction()
	);

	FScopeLock Lock(&RenderDataMutex);

	for (const auto& Pair : CachedRenderData)
	{
		if (Pair.Value.bIsValid)
		{
			RenderDebugSlice(GraphBuilder, View, Output, Pair.Value);
		}
	}

	return MoveTemp(Output);
}

void FIVSmokeVolumeDebugRenderer::RenderDebugSlice(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FScreenPassRenderTarget& Output,
	const FIVSmokeDebugRenderData& RenderData)
{
	if (!RenderData.VolumeTextureRHI.IsValid())
	{
		return;
	}

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeVolumeTextureDebugPS> PixelShader(ShaderMap);

	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeVolumeTextureDebugPS::FParameters>();

	// View uniform buffer (required for View.ViewSizeAndInvSize in shader)
	Parameters->View = View.ViewUniformBuffer;

	// Texture parameters (from cached data)
	Parameters->SmokeVolumeTexture3D = RenderData.VolumeTextureRHI;
	Parameters->SmokeVolumeSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	// Debug parameters (from cached data)
	Parameters->Resolution = RenderData.Resolution;
	Parameters->SliceIndex = RenderData.SliceIndex;
	Parameters->DebugMode = RenderData.DebugMode;
	Parameters->CurrentTime = RenderData.CurrentTime;
	Parameters->HoleLifeTime = RenderData.HoleLifeTime;

	// Display area (bottom-right corner, fixed square regardless of aspect ratio)
	const float ScreenWidth = View.UnscaledViewRect.Width();
	const float ScreenHeight = View.UnscaledViewRect.Height();
	const float AspectRatio = ScreenWidth / ScreenHeight;

	const float MarginPixels = 20.0f;
	const float MarginX = MarginPixels / ScreenWidth;
	const float MarginY = MarginPixels / ScreenHeight;

	const float SizeY = 0.25f;
	const float SizeX = SizeY / AspectRatio;

	Parameters->DisplayOffset = FVector2f(1.0f - SizeX - MarginX, 1.0f - SizeY - MarginY);
	Parameters->DisplaySize = FVector2f(SizeX, SizeY);

	// Render target
	Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	// Dispatch
	FIVSmokePassConfig Config;
	Config.EventName = TEXT("IVSmokeVolumeDebug");
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
