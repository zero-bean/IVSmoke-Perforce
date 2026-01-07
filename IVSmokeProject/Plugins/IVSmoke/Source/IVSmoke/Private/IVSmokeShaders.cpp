// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeShaders.h"
#include "IVSmokeHoleCarveCS.h"

IMPLEMENT_GLOBAL_SHADER(FIVSmokeSharpenCompositePS, "/Plugin/IVSmoke/IVSmokeCompositePS.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeCopyPS, "/Plugin/IVSmoke/IVSmokeCopy.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeNoiseGeneratorGlobalCS, "/Plugin/IVSmoke/IVSmokeNoiseGeneratorCS.usf", "GenerateNoise", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeHoleCarveCS, "/Plugin/IVSmoke/IVSmokeHoleCarveCS.usf", "MainCS", SF_Compute);

