// Fill out your copyright notice in the Description page of Project Settings.


#include "IVSmokeNoiseGeneratorGlobalCS.h"

IMPLEMENT_GLOBAL_SHADER(FIVSmokeNoiseGeneratorGlobalCS, "/Plugin/IVSmoke/IVSmokeNoiseGenerator.usf", "GenerateNoise", SF_Compute);

