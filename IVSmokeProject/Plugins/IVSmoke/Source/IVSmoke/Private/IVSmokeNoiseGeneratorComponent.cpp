// Fill out your copyright notice in the Description page of Project Settings.


#include "IVSmokeNoiseGeneratorComponent.h"
#include "IVSmokeShaders.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "Engine/Texture2D.h"

#include "IVSmokeRenderer.h"

UIVSmokeNoiseGeneratorComponent::UIVSmokeNoiseGeneratorComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}


void UIVSmokeNoiseGeneratorComponent::BeginPlay()
{
    Super::BeginPlay();
    if (!NoiseVolume)
    {
        CreateVolume();
    }

	FIVSmokeRenderer::Get().SetNoiseVolume(NoiseVolume);

    // 1 frame wait
    GetWorld()->GetTimerManager().SetTimerForNextTick([this]()
    {
        RunComputeShader();
    });
}


void UIVSmokeNoiseGeneratorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UIVSmokeNoiseGeneratorComponent::CreateVolume()
{
    NoiseVolume = NewObject<UTextureRenderTargetVolume>(this);
    NoiseVolume->Init(TexSize, TexSize, TexSize, EPixelFormat::PF_R16F);
    NoiseVolume->bCanCreateUAV = true;
    NoiseVolume->ClearColor = FLinearColor::Black;
    NoiseVolume->SRGB = false;
    NoiseVolume->UpdateResourceImmediate(true);
}

void UIVSmokeNoiseGeneratorComponent::RunComputeShader()
{
    if (!NoiseVolume)
    {
        UE_LOG(LogTemp, Error, TEXT("NoiseVolume is null!"));
        return;
    }
    FTextureRenderTargetResource* RenderTargetResource = NoiseVolume->GameThread_GetRenderTargetResource();

    ENQUEUE_RENDER_COMMAND(RunMyComputeShader)(
        [this, RenderTargetResource](FRHICommandListImmediate& RHICmdList)
        {
            FRDGBuilder GraphBuilder(RHICmdList);
            FRDGTextureRef NoiseTexture = GraphBuilder.RegisterExternalTexture(
                CreateRenderTarget(RenderTargetResource->TextureRHI, TEXT("NoiseVolume"))
            );

            // Create UAV
            FRDGTextureUAVRef OutputUAV = GraphBuilder.CreateUAV(NoiseTexture);

            // Set Parameters
            auto* Parameters = GraphBuilder.AllocParameters<FIVSmokeNoiseGeneratorGlobalCS::FParameters>();
            Parameters->RWNoiseTex = OutputUAV;
            Parameters->TexSize = FUintVector3(TexSize, TexSize, TexSize);
            Parameters->Octaves = Octaves;
            Parameters->Wrap = Wrap;
            Parameters->AxisCellCount = AxisCellCount;
            Parameters->Amplitude = Amplitude;
            Parameters->CellSize = CellSize;
            Parameters->Seed = Seed;

            // Get Shader
            TShaderMapRef<FIVSmokeNoiseGeneratorGlobalCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

            // Calc GroupCount
            FIntVector GroupCount(
                FMath::DivideAndRoundUp(TexSize, (uint32)8),
                FMath::DivideAndRoundUp(TexSize, (uint32)8),
                FMath::DivideAndRoundUp(TexSize, (uint32)8)
            );

            // Add Pass Dispatch
            GraphBuilder.AddPass(
                RDG_EVENT_NAME("NoiseGenerationCS"),
                Parameters,
                ERDGPassFlags::Compute,
                [Parameters, ComputeShader, GroupCount](FRHIComputeCommandList& RHICmdList)
                {
                    FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *Parameters, GroupCount);
                }
            );
            GraphBuilder.Execute();

            UE_LOG(LogTemp, Display, TEXT("Compute Shader Executed Successfully"));
        }
        );
}


