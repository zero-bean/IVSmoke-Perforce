// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokePostProcessPass.h"
#include "RenderGraphUtils.h"

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
