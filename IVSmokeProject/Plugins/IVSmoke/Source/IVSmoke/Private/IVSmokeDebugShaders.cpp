// Copyright SDB. All Rights Reserved.

#include "IVSmokeDebugShaders.h"

IMPLEMENT_GLOBAL_SHADER(
	FIVSmokeVolumeTextureDebugPS,
	"/Plugin/IVSmoke/IVSmokeVolumeTextureDebug.usf",
	"MainPS",
	SF_Pixel
);
