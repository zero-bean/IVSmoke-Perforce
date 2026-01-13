// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IVSmokeNoiseGeneratorComponent.generated.h"

class UTextureRenderTargetVolume;
class FRHITexture;
class FRHIUnorderedAccessView;
/**
 * @class UIVSmokeNoiseGeneratorComponent
 * @brief Worley noise volume generator using Computeshader
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class IVSMOKE_API UIVSmokeNoiseGeneratorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UIVSmokeNoiseGeneratorComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
	/**
	 * @brief Create 3d volume to NoiseVolume (TexSize, TexSize, TexSize) R16F
	 */
	void CreateVolume();

	/**
	 * @brief Dispatch IVSmokeNoiseGenerator => GenerateNoise()
	 */
	void RunComputeShader();
private:
	/** Random Seed */
	UPROPERTY(EditAnywhere, category = "NoiseGenerator")
	int32 Seed = 0;
	/** Tex Size x, y, z */
	UPROPERTY(EditAnywhere, category = "NoiseGenerator")
	uint32 TexSize = 128;
	UPROPERTY(EditAnywhere, category = "NoiseGenerator")
	uint32 Octaves = 6;
	UPROPERTY(EditAnywhere, category = "NoiseGenerator")
	float Wrap = 0.76f;
	UPROPERTY(EditAnywhere, category = "NoiseGenerator")
	float Amplitude = 0.62f;

	UPROPERTY(EditAnywhere, category = "NoiseGenerator")
	uint32 AxisCellCount = 4;
	UPROPERTY(EditAnywhere, category = "NoiseGenerator")
	uint32 CellSize = 32;

	UPROPERTY(EditAnywhere, category = "NoiseGenerator")
	TObjectPtr<UTextureRenderTargetVolume> NoiseVolume;


};
