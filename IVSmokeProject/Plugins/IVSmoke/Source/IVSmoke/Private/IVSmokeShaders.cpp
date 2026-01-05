// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeShaders.h"

IMPLEMENT_GLOBAL_SHADER(FIVSmokeRayMarchCS, "/Plugin/IVSmoke/IVSmokeRayMarch.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeCompositePS, "/Plugin/IVSmoke/IVSmokeRayMarch.usf", "CompositePS", SF_Pixel);
