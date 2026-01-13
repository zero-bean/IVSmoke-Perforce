// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ScreenPass.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "PixelShaderUtils.h"

/**
 * Core utility for dispatching PS/CS post process passes.
 * Designed for reusability - bring your own shader and parameters.
 */

class IVSMOKE_API FIVSmokePostProcessPass
{
public:
	/**
	 * Add a fullscreen pixel shader pass.
	 *
	 * @param GraphBuilder    RDG builder
	 * @param ShaderMap       Global shader map
	 * @param PixelShader     Pixel shader to use
	 * @param Parameters      Shader parameters (must have RenderTargets bound)
	 * @param Output          Render target
	 * @param Config          Pass configuration
	 */
	template<typename TShaderClass>
	static void AddPixelShaderPass(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* ShaderMap,
		TShaderMapRef<TShaderClass> PixelShader,
		typename TShaderClass::FParameters* Parameters,
		const FScreenPassRenderTarget& Output);

	/**
	 * Add a compute shader pass.
	 *
	 * @param GraphBuilder    RDG builder
	 * @param ShaderMap       Global shader map
	 * @param ComputeShader   Compute shader to use
	 * @param Parameters      Shader parameters (must have UAV bound)
	 * @param ViewportSize    Size of the viewport for thread dispatch
	 * @param Config          Pass configuration
	 */
	template<typename TShaderClass>
	static void AddComputeShaderPass(
		FRDGBuilder& GraphBuilder,
		FGlobalShaderMap* ShaderMap,
		TShaderMapRef<TShaderClass> ComputeShader,
		typename TShaderClass::FParameters* Parameters,
		const FIntVector& TotalThreadSize);

	/**
	 * Create an output texture suitable for UAV (compute shader).
	 *
	 * @param GraphBuilder    RDG builder
	 * @param SourceTexture   Texture to base dimensions on
	 * @param DebugName       Debug name for the new texture
	 * @param OverrideFormat  Override pixel format (PF_Unknown = use source format)
	 * @param OverrideExtent  Override texture extent (zero = use source extent)
	 * @return New texture with UAV flag
	 */
	static FRDGTextureRef CreateOutputTexture(
		FRDGBuilder& GraphBuilder,
		FRDGTextureRef SourceTexture,
		const TCHAR* DebugName = TEXT("IVSmokeOutput"),
		EPixelFormat OverrideFormat = PF_Unknown,
		FIntPoint OverrideExtent = FIntPoint::ZeroValue,
		ETextureCreateFlags Flags = ETextureCreateFlags::UAV);
};
//------------------------------------------------------------------------------
// Template implementations
//------------------------------------------------------------------------------

template<typename TShaderClass>
void FIVSmokePostProcessPass::AddPixelShaderPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	TShaderMapRef<TShaderClass> PixelShader,
	typename TShaderClass::FParameters* Parameters,
	const FScreenPassRenderTarget& Output)
{
	RDG_EVENT_SCOPE(GraphBuilder, "%s", TShaderClass::EventName);

	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder,
		ShaderMap,
		RDG_EVENT_NAME("%s", TShaderClass::EventName),
		PixelShader,
		Parameters,
		Output.ViewRect,
		TShaderClass::GetBlendState()
	);
}

template<typename TShaderClass>
void FIVSmokePostProcessPass::AddComputeShaderPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	TShaderMapRef<TShaderClass> ComputeShader,
	typename TShaderClass::FParameters* Parameters,
	const FIntVector& TotalThreadSize)
{
	const uint32 GroupCountX = FMath::DivideAndRoundUp((uint32)TotalThreadSize.X, TShaderClass::ThreadGroupSizeX);
	const uint32 GroupCountY = FMath::DivideAndRoundUp((uint32)TotalThreadSize.Y, TShaderClass::ThreadGroupSizeY);
	const uint32 GroupCountZ = FMath::DivideAndRoundUp((uint32)TotalThreadSize.Z, TShaderClass::ThreadGroupSizeZ);

	RDG_EVENT_SCOPE(GraphBuilder, "%s", TShaderClass::EventName);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("%s", TShaderClass::EventName),
		ComputeShader,
		Parameters,
		FIntVector(GroupCountX, GroupCountY, GroupCountZ)
	);
}
