// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmokeShaders.h"

IMPLEMENT_GLOBAL_SHADER(FIVSmokeTestPS, "/Plugin/IVSmoke/IVSmokeTest.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FIVSmokeTestCS, "/Plugin/IVSmoke/IVSmokeTest.usf", "MainCS", SF_Compute);
