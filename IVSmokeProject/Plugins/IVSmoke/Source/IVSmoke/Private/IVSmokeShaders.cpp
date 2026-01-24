// Copyright (c) 2026, Team SDB. All rights reserved.

#include "IVSmokeShaders.h"
#include "IVSmokeHoleCarveCS.h"

// Note: FIVSmokeMultiVolumeRayMarchCS is now implemented in IVSmokeOccupancy.cpp
IMPLEMENT_GLOBAL_SHADER(FIVSmokeNoiseGeneratorGlobalCS, "/Plugin/IVSmoke/IVSmokeNoiseGeneratorCS.usf", "GenerateNoise", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeStructuredToTextureCS, "/Plugin/IVSmoke/IVSmokeStructuredToTextureCS.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeVoxelFXAACS, "/Plugin/IVSmoke/IVSmokeVoxelFXAACS.usf", "MainCS", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FIVSmokeSharpenCompositePS, "/Plugin/IVSmoke/IVSmokeCompositePS.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeCopyPS, "/Plugin/IVSmoke/IVSmokeCopy.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeTranslucencyCompositePS, "/Plugin/IVSmoke/IVSmokeTranslucencyCompositePS.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeDepthSortedCompositePS, "/Plugin/IVSmoke/IVSmokeDepthSortedCompositePS.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeHoleCarveCS, "/Plugin/IVSmoke/IVSmokeHoleCarveCS.usf", "MainCS", SF_Compute);

// VSM (Variance Shadow Map) Shaders
IMPLEMENT_GLOBAL_SHADER(FIVSmokeDepthToVarianceCS, "/Plugin/IVSmoke/IVSmokeVSM.usf", "DepthToVarianceCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeVSMBlurCS, "/Plugin/IVSmoke/IVSmokeVSM.usf", "BlurCS", SF_Compute);
