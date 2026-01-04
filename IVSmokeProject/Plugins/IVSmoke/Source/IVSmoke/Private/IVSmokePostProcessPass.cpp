// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokePostProcessPass.h"
#include "RenderGraphUtils.h"

// Console variable for runtime render mode switching
static TAutoConsoleVariable<int32> CVarIVSmokeRenderMode(
	TEXT("IVSmoke.RenderMode"),
	0,
	TEXT("IVSmoke rendering mode:\n")
	TEXT("  0: Pixel Shader (default)\n")
	TEXT("  1: Compute Shader"),
	ECVF_RenderThreadSafe
);

FRDGTextureRef FIVSmokePostProcessPass::CreateUAVOutputTexture(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SourceTexture,
	const TCHAR* DebugName)
{
	FRDGTextureDesc OutputDesc = SourceTexture->Desc;
	OutputDesc.Flags |= ETextureCreateFlags::UAV;
	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, DebugName);

	// Copy source content to output
	AddCopyTexturePass(GraphBuilder, SourceTexture, OutputTexture);

	return OutputTexture;
}

EIVSmokeRenderMode FIVSmokePostProcessPass::GetRenderModeFromCVar()
{
	return static_cast<EIVSmokeRenderMode>(CVarIVSmokeRenderMode.GetValueOnRenderThread());
}
