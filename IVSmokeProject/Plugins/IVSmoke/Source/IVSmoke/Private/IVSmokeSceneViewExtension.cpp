// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeSceneViewExtension.h"
#include "IVSmokeShaders.h"
#include "IVSmokeRenderer.h"

// Public API headers only
#include "PostProcess/PostProcessMaterialInputs.h"
#include "PixelShaderUtils.h"
#include "ScreenPass.h"
#include "SceneView.h"

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

void FIVSmokeSceneViewExtension::RenderWithPixelShader(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FScreenPassRenderTarget& Output)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeTestPS> PixelShader(ShaderMap);

	FIVSmokeTestPS::FParameters* Parameters = GraphBuilder.AllocParameters<FIVSmokeTestPS::FParameters>();
	Parameters->TintColor = FLinearColor(1.0f, 0.0f, 0.0f, 0.3f); // Red tint
	Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	FIVSmokePassConfig Config;
	Config.EventName = TEXT("IVSmokeTest");

	FIVSmokePostProcessPass::AddPixelShaderPass(
		GraphBuilder,
		ShaderMap,
		PixelShader,
		Parameters,
		Output,
		Config
	);
}

void FIVSmokeSceneViewExtension::RenderWithComputeShader(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef OutputTexture)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.FeatureLevel);
	TShaderMapRef<FIVSmokeTestCS> ComputeShader(ShaderMap);

	const FIntPoint ViewportSize(View.UnscaledViewRect.Width(), View.UnscaledViewRect.Height());

	FIVSmokeTestCS::FParameters* Parameters = GraphBuilder.AllocParameters<FIVSmokeTestCS::FParameters>();
	Parameters->TintColor = FLinearColor(0.0f, 0.0f, 1.0f, 0.3f); // Blue tint
	Parameters->ViewportSize = FVector2f(ViewportSize);
	Parameters->SceneColorTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc(SceneColorTexture));
	Parameters->OutputTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputTexture));
	Parameters->SceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	FIVSmokePassConfig Config;
	Config.EventName = TEXT("IVSmokeTest");
	Config.ThreadGroupSizeX = FIVSmokeTestCS::ThreadGroupSizeX;
	Config.ThreadGroupSizeY = FIVSmokeTestCS::ThreadGroupSizeY;

	FIVSmokePostProcessPass::AddComputeShaderPass(
		GraphBuilder,
		ShaderMap,
		ComputeShader,
		Parameters,
		ViewportSize,
		Config
	);
}

FScreenPassTexture FIVSmokeSceneViewExtension::Render_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	// Get scene color from public API
	FScreenPassTextureSlice SceneColorSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
	if (!SceneColorSlice.IsValid())
	{
		return FScreenPassTexture();
	}

	FScreenPassTexture SceneColor(SceneColorSlice);

	// Get render mode from console variable
	const EIVSmokeRenderMode CurrentRenderMode = FIVSmokePostProcessPass::GetRenderModeFromCVar();

	if (CurrentRenderMode == EIVSmokeRenderMode::ComputeShader)
	{
		// Compute Shader path
		FRDGTextureRef OutputTexture = FIVSmokePostProcessPass::CreateUAVOutputTexture(
			GraphBuilder,
			SceneColor.Texture,
			TEXT("IVSmokeOutput")
		);

		RenderWithComputeShader(GraphBuilder, View, SceneColor.Texture, OutputTexture);

		// Copy result to override output if needed
		if (Inputs.OverrideOutput.IsValid())
		{
			AddCopyTexturePass(GraphBuilder, OutputTexture, Inputs.OverrideOutput.Texture);
			return FScreenPassTexture(Inputs.OverrideOutput);
		}

		return FScreenPassTexture(OutputTexture, SceneColor.ViewRect);
	}
	else
	{
		// Pixel Shader path
		FScreenPassRenderTarget Output = Inputs.OverrideOutput;
		if (!Output.IsValid())
		{
			Output = FScreenPassRenderTarget(
				SceneColor.Texture,
				SceneColor.ViewRect,
				View.GetOverwriteLoadAction()
			);
		}

		RenderWithPixelShader(GraphBuilder, View, Output);

		return MoveTemp(Output);
	}
}
