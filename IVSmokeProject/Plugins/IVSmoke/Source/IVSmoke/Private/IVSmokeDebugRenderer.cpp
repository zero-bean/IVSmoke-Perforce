// Copyright SDB. All Rights Reserved.

#include "IVSmokeDebugRenderer.h"
#include "IVSmokeHoleGeneratorComponent.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "PipelineStateCache.h"
#include "RenderGraphBuilder.h"

// ============================================================================
// Shader Implementation
// ============================================================================

IMPLEMENT_GLOBAL_SHADER(
	FIVSmokeVolumeSliceDebugVS,
	"/Plugin/IVSmoke/IVSmokeVolumeSliceDebug.usf",
	"MainVS",
	SF_Vertex
);

IMPLEMENT_GLOBAL_SHADER(
	FIVSmokeVolumeSliceDebugPS,
	"/Plugin/IVSmoke/IVSmokeVolumeSliceDebug.usf",
	"MainPS",
	SF_Pixel
);

// ============================================================================
// Cube Vertex Declaration
// ============================================================================

struct FIVSmokeCubeVertex
{
	FVector3f Position;
};

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

// ============================================================================
// Static Instance
// ============================================================================

TSharedPtr<FIVSmokeDebugRenderer, ESPMode::ThreadSafe> FIVSmokeDebugRenderer::Instance;

// ============================================================================
// Public Interface
// ============================================================================

FIVSmokeDebugRenderer& FIVSmokeDebugRenderer::Get()
{
	if (!Instance.IsValid())
	{
		Instance = FSceneViewExtensions::NewExtension<FIVSmokeDebugRenderer>();
	}
	return *Instance.Get();
}

FIVSmokeDebugRenderer::FIVSmokeDebugRenderer(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

void FIVSmokeDebugRenderer::Register(UIVSmokeHoleGeneratorComponent* Component)
{
	if (!Component)
	{
		return;
	}

	FScopeLock Lock(&DataMutex);
	DebugComponents.AddUnique(Component);
}

void FIVSmokeDebugRenderer::Unregister(UIVSmokeHoleGeneratorComponent* Component)
{
	if (!Component)
	{
		return;
	}

	FScopeLock Lock(&DataMutex);
	DebugComponents.Remove(Component);
	CachedRenderData.Remove(Component->GetUniqueID());
}

bool FIVSmokeDebugRenderer::HasAnyComponents() const
{
	FScopeLock Lock(&DataMutex);

	for (const auto& Pair : CachedRenderData)
	{
		if (Pair.Value.bIsValid)
		{
			return true;
		}
	}
	return false;
}

void FIVSmokeDebugRenderer::UpdateRenderData(UIVSmokeHoleGeneratorComponent* Component)
{
	if (!Component)
	{
		return;
	}

	const uint32 ComponentID = Component->GetUniqueID();

	if (!Component->bShowVolumeDebug)
	{
		FScopeLock Lock(&DataMutex);
		CachedRenderData.Remove(ComponentID);
		return;
	}

	FTextureRHIRef TextureRHI = Component->GetHoleTexture();
	if (!TextureRHI.IsValid())
	{
		return;
	}

	FScopeLock Lock(&DataMutex);

	FIVSmokeDebugRenderData& Data = CachedRenderData.FindOrAdd(ComponentID);
	Data.VolumeTextureRHI = TextureRHI;
	Data.Resolution = Component->GetVoxelResolution();
	Data.bIsValid = true;

	const FTransform& Transform = Component->GetComponentTransform();
	Data.LocalToWorld = Transform.ToMatrixWithScale();
	Data.WorldToLocal = Transform.ToInverseMatrixWithScale();
	Data.VolumeExtent = Component->GetUnscaledBoxExtent();
	Data.NumSteps = FMath::Clamp(Component->GetVoxelResolution().Z, 32, 128);
	Data.StepOpacity = 1.0f;
}

// ============================================================================
// SceneViewExtension Interface
// ============================================================================

bool FIVSmokeDebugRenderer::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return HasAnyComponents();
}

void FIVSmokeDebugRenderer::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass,
	const FSceneView& InView,
	FPostProcessingPassDelegateArray& InOutPassCallbacks,
	bool bIsPassEnabled)
{
	if (Pass == EPostProcessingPass::MotionBlur)
	{
		InOutPassCallbacks.Add(
			FPostProcessingPassDelegate::CreateRaw(
				this,
				&FIVSmokeDebugRenderer::Render_RenderThread
			)
		);
	}
}

// ============================================================================
// Rendering
// ============================================================================

FScreenPassTexture FIVSmokeDebugRenderer::Render_RenderThread(
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

	FScreenPassRenderTarget Output = FScreenPassRenderTarget(
		SceneColor.Texture,
		SceneColor.ViewRect,
		View.GetOverwriteLoadAction()
	);

	FScopeLock Lock(&DataMutex);

	for (const auto& Pair : CachedRenderData)
	{
		if (Pair.Value.bIsValid)
		{
			RenderVolumeCube(GraphBuilder, View, Output, Pair.Value);
		}
	}

	return MoveTemp(Output);
}

void FIVSmokeDebugRenderer::RenderVolumeCube(
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

	static const FIVSmokeCubeVertex CubeVertices[8] = {
		{ FVector3f(-1.0f, -1.0f, -1.0f) },
		{ FVector3f( 1.0f, -1.0f, -1.0f) },
		{ FVector3f( 1.0f,  1.0f, -1.0f) },
		{ FVector3f(-1.0f,  1.0f, -1.0f) },
		{ FVector3f(-1.0f, -1.0f,  1.0f) },
		{ FVector3f( 1.0f, -1.0f,  1.0f) },
		{ FVector3f( 1.0f,  1.0f,  1.0f) },
		{ FVector3f(-1.0f,  1.0f,  1.0f) },
	};

	static const uint16 CubeIndices[36] = {
		0, 2, 1, 0, 3, 2,
		4, 5, 6, 4, 6, 7,
		0, 4, 7, 0, 7, 3,
		1, 2, 6, 1, 6, 5,
		0, 1, 5, 0, 5, 4,
		3, 7, 6, 3, 6, 2,
	};

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("IVSmokeVolumeCube"),
		Parameters,
		ERDGPassFlags::Raster,
		[VertexShader, PixelShader, Parameters](FRHICommandList& RHICmdList)
		{
			FRHIResourceCreateInfo CreateInfoVB(TEXT("IVSmokeCubeVB"));
			FBufferRHIRef VertexBuffer = RHICmdList.CreateVertexBuffer(
				sizeof(CubeVertices),
				BUF_Volatile,
				CreateInfoVB
			);
			void* VBData = RHICmdList.LockBuffer(VertexBuffer, 0, sizeof(CubeVertices), RLM_WriteOnly);
			FMemory::Memcpy(VBData, CubeVertices, sizeof(CubeVertices));
			RHICmdList.UnlockBuffer(VertexBuffer);

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

			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *Parameters);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);

			RHICmdList.SetStreamSource(0, VertexBuffer, 0);
			RHICmdList.DrawIndexedPrimitive(
				IndexBuffer,
				0,
				0,
				8,
				0,
				12,
				1
			);
		}
	);
}
