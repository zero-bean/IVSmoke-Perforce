// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeShaders.h"
#include "IVSmokeHoleCarveCS.h"

IMPLEMENT_GLOBAL_SHADER(FIVSmokeMultiVolumeRayMarchCS, "/Plugin/IVSmoke/IVSmokeMultiVolumeRayMarch.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeNoiseGeneratorGlobalCS, "/Plugin/IVSmoke/IVSmokeNoiseGeneratorCS.usf", "GenerateNoise", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeStructuredToTextureCS, "/Plugin/IVSmoke/IVSmokeStructuredToTextureCS.usf", "MainCS", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FIVSmokeSharpenCompositePS, "/Plugin/IVSmoke/IVSmokeCompositePS.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeCopyPS, "/Plugin/IVSmoke/IVSmokeCopy.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeTranslucencyCompositePS, "/Plugin/IVSmoke/IVSmokeTranslucencyCompositePS.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeDepthSortedCompositePS, "/Plugin/IVSmoke/IVSmokeDepthSortedCompositePS.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeHoleCarveCS, "/Plugin/IVSmoke/IVSmokeHoleCarveCS.usf", "MainCS", SF_Compute);

