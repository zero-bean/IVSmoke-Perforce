// Fill out your copyright notice in the Description page of Project Settings.

#include "IVSmokeNoiseGeneratorComponent.h"

UIVSmokeNoiseGeneratorComponent::UIVSmokeNoiseGeneratorComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // No longer needs to tick
}

void UIVSmokeNoiseGeneratorComponent::BeginPlay()
{
	Super::BeginPlay();

	// NOTE: This component is deprecated. FIVSmokeRenderer now manages noise volume internally.
	// Keeping this component for backwards compatibility with existing blueprints.
	// The noise generation is now handled by FIVSmokeRenderer::Initialize() using UIVSmokeSettings.

	UE_LOG(LogTemp, Warning,
		TEXT("[UIVSmokeNoiseGeneratorComponent] This component is deprecated. ")
		TEXT("Noise volume is now managed by FIVSmokeRenderer. Configure noise settings in Project Settings > Plugins > IVSmoke."));
}

void UIVSmokeNoiseGeneratorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// Deprecated - no longer performs any action
}

void UIVSmokeNoiseGeneratorComponent::CreateVolume()
{
	// Deprecated - noise volume is now created by FIVSmokeRenderer
}

void UIVSmokeNoiseGeneratorComponent::RunComputeShader()
{
	// Deprecated - noise generation is now handled by FIVSmokeRenderer
}
