// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeShaders.h"

// FIVSmokeRayMarchCS removed - using FIVSmokeMultiVolumeRayMarchCS for all rendering
IMPLEMENT_GLOBAL_SHADER(FIVSmokeSharpenCompositePS, "/Plugin/IVSmoke/IVSmokeCompositePS.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeCopyPS, "/Plugin/IVSmoke/IVSmokeCopy.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeNoiseGeneratorGlobalCS, "/Plugin/IVSmoke/IVSmokeNoiseGeneratorCS.usf", "GenerateNoise", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeMultiVolumeRayMarchCS, "/Plugin/IVSmoke/IVSmokeMultiVolumeRayMarch.usf", "MainCS", SF_Compute);

