// Copyright SDB. All Rights Reserved.

#include "IVSmokeDebugShaders.h"

IMPLEMENT_GLOBAL_SHADER(
	FIVSmokeVolumeSliceDebugVS,
	"/Plugin/IVSmoke/IVSmokeVolumeSliceDebug.usf",
	"MainVS",
	SF_Vertex
);

IMPLEMENT_GLOBAL_SHADER(
	FIVSmokeVolumeSliceDebugPS,
	"/Plugin/IVSmoke/IVSmokeVolumeSliceDebug.usf",
	"MainPS",
	SF_Pixel
);
