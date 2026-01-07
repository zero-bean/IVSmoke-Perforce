// Copyright SDB. All Rights Reserved.

#include "IVSmokeVolumeDebugRenderer.h"

#include "IVSmokeHoleGeneratorComponent.h"
#include "IVSmokeDebugShaders.h"
#include "PipelineStateCache.h"
#include "RenderGraphBuilder.h"

// ============================================================================
// Cube Vertex Declaration
// ============================================================================

/**
 * Vertex structure for volume cube.
 * Only Position is needed - ray marching handles the rest.
 */
struct FIVSmokeCubeVertex
{
	FVector3f Position;  // Normalized cube position (-1 to 1)
};

/**
 * Global vertex declaration for cube rendering.
 */
class FIVSmokeCubeVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		FVertexDeclarationElementList Elements;
		uint32 Stride = sizeof(FIVSmokeCubeVertex);
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FIVSmokeCubeVertex, Position), VET_Float3, 0, Stride));
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI() override
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

static TGlobalResource<FIVSmokeCubeVertexDeclaration> GIVSmokeCubeVertexDeclaration;

FIVSmokeVolumeDebugRenderer& FIVSmokeVolumeDebugRenderer::Get()
{
	static FIVSmokeVolumeDebugRenderer Instance;
	return Instance;
}

void FIVSmokeVolumeDebugRenderer::Register(UIVSmokeHoleGeneratorComponent* Component)
{
	if (!Component)
	{
		return;
	}

	FScopeLock Lock(&ComponentsMutex);
	DebugComponents.AddUnique(Component);
}

void FIVSmokeVolumeDebugRenderer::Unregister(UIVSmokeHoleGeneratorComponent* Component)
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

void FIVSmokeVolumeDebugRenderer::UpdateRenderData(UIVSmokeHoleGeneratorComponent* Component)
{
	if (!Component)
	{
		return;
	}

	const uint32 ComponentID = Component->GetUniqueID();

	// If debug visualization is disabled, remove from cache
	if (!Component->bShowVolumeDebug)
	{
		FScopeLock Lock(&RenderDataMutex);
		CachedRenderData.Remove(ComponentID);
		return;
	}

	// Get hole texture (GPU compute shader output)
	FTextureRHIRef TextureRHI = Component->GetHoleTexture();
	if (!TextureRHI.IsValid())
	{
		return;
	}

	// Update cache directly (game thread)
	FScopeLock Lock(&RenderDataMutex);

	FIVSmokeDebugRenderData& Data = CachedRenderData.FindOrAdd(ComponentID);
	Data.VolumeTextureRHI = TextureRHI;
	Data.Resolution = Component->GetVoxelResolution();
	Data.bIsValid = true;

	// World-space rendering data
	// Use unscaled extent since LocalToWorld already includes scale
	const FTransform& Transform = Component->GetComponentTransform();
	Data.LocalToWorld = Transform.ToMatrixWithScale();
	Data.WorldToLocal = Transform.ToInverseMatrixWithScale();
	Data.VolumeExtent = Component->GetUnscaledBoxExtent();
	Data.NumSteps = FMath::Clamp(Component->GetVoxelResolution().Z, 32, 128);
	Data.StepOpacity = 1.0f;
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
			RenderVolumeCube(GraphBuilder, View, Output, Pair.Value);
		}
	}

	return MoveTemp(Output);
}

void FIVSmokeVolumeDebugRenderer::RenderVolumeCube(
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
	TShaderMapRef<FIVSmokeVolumeSliceDebugVS> VertexShader(ShaderMap);
	TShaderMapRef<FIVSmokeVolumeSliceDebugPS> PixelShader(ShaderMap);

	// Set up shader parameters
	auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeVolumeSliceParameters>();
	Parameters->VolumeTexture = RenderData.VolumeTextureRHI;
	Parameters->VolumeSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->LocalToWorld = FMatrix44f(RenderData.LocalToWorld);
	Parameters->WorldToLocal = FMatrix44f(RenderData.WorldToLocal);
	Parameters->WorldToClip = FMatrix44f(View.ViewMatrices.GetViewProjectionMatrix());
	Parameters->VolumeExtent = FVector3f(RenderData.VolumeExtent);
	Parameters->CameraWorldPos = FVector3f(View.ViewMatrices.GetViewOrigin());
	Parameters->NumSteps = RenderData.NumSteps;
	Parameters->StepOpacity = RenderData.StepOpacity;
	Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	// Cube geometry: 8 vertices, 36 indices (12 triangles)
	static const FIVSmokeCubeVertex CubeVertices[8] = {
		{ FVector3f(-1.0f, -1.0f, -1.0f) },  // 0: back-bottom-left
		{ FVector3f( 1.0f, -1.0f, -1.0f) },  // 1: back-bottom-right
		{ FVector3f( 1.0f,  1.0f, -1.0f) },  // 2: back-top-right
		{ FVector3f(-1.0f,  1.0f, -1.0f) },  // 3: back-top-left
		{ FVector3f(-1.0f, -1.0f,  1.0f) },  // 4: front-bottom-left
		{ FVector3f( 1.0f, -1.0f,  1.0f) },  // 5: front-bottom-right
		{ FVector3f( 1.0f,  1.0f,  1.0f) },  // 6: front-top-right
		{ FVector3f(-1.0f,  1.0f,  1.0f) },  // 7: front-top-left
	};

	// Indices for 12 triangles (6 faces, 2 triangles each)
	static const uint16 CubeIndices[36] = {
		// Back face (-Z)
		0, 2, 1, 0, 3, 2,
		// Front face (+Z)
		4, 5, 6, 4, 6, 7,
		// Left face (-X)
		0, 4, 7, 0, 7, 3,
		// Right face (+X)
		1, 2, 6, 1, 6, 5,
		// Bottom face (-Y)
		0, 1, 5, 0, 5, 4,
		// Top face (+Y)
		3, 7, 6, 3, 6, 2,
	};

	// Add render pass
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("IVSmokeVolumeCube"),
		Parameters,
		ERDGPassFlags::Raster,
		[VertexShader, PixelShader, Parameters](FRHICommandList& RHICmdList)
		{
			// Create transient vertex buffer
			FRHIResourceCreateInfo CreateInfoVB(TEXT("IVSmokeCubeVB"));
			FBufferRHIRef VertexBuffer = RHICmdList.CreateVertexBuffer(
				sizeof(CubeVertices),
				BUF_Volatile,
				CreateInfoVB
			);
			void* VBData = RHICmdList.LockBuffer(VertexBuffer, 0, sizeof(CubeVertices), RLM_WriteOnly);
			FMemory::Memcpy(VBData, CubeVertices, sizeof(CubeVertices));
			RHICmdList.UnlockBuffer(VertexBuffer);

			// Create transient index buffer
			FRHIResourceCreateInfo CreateInfoIB(TEXT("IVSmokeCubeIB"));
			FBufferRHIRef IndexBuffer = RHICmdList.CreateIndexBuffer(
				sizeof(uint16),
				sizeof(CubeIndices),
				BUF_Volatile,
				CreateInfoIB
			);
			void* IBData = RHICmdList.LockBuffer(IndexBuffer, 0, sizeof(CubeIndices), RLM_WriteOnly);
			FMemory::Memcpy(IBData, CubeIndices, sizeof(CubeIndices));
			RHICmdList.UnlockBuffer(IndexBuffer);

			// Set up graphics pipeline state
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GIVSmokeCubeVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

			// Set shader parameters
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *Parameters);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);

			// Draw cube
			RHICmdList.SetStreamSource(0, VertexBuffer, 0);
			RHICmdList.DrawIndexedPrimitive(
				IndexBuffer,
				0,   // BaseVertexIndex
				0,   // FirstInstance
				8,   // NumVertices
				0,   // StartIndex
				12,  // NumPrimitives (triangles)
				1    // NumInstances
			);
		}
	);
}
