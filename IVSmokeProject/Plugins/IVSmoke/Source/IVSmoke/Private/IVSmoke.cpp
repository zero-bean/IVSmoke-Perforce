// Copyright (c) 2026, Team SDB. All rights reserved.

#include "IVSmoke.h"
#include "IVSmokeSceneViewExtension.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Stats/Stats.h"

DEFINE_LOG_CATEGORY(LogIVSmoke);

#define LOCTEXT_NAMESPACE "FIVSmokeModule"

void FIVSmokeModule::StartupModule()
{
	FString PluginShaderDir = FPaths::Combine(
		IPluginManager::Get().FindPlugin(TEXT("IVSmoke"))->GetBaseDir(),
		TEXT("Shaders")
	);
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/IVSmoke"), PluginShaderDir);

#if !UE_SERVER
	// SceneViewExtension requires GEngine, defer until engine is ready
	UE_LOG(LogIVSmoke, Log, TEXT("[FIVSmokeModule::StartupModule] Registering OnPostEngineInit"));
	FCoreDelegates::OnPostEngineInit.AddLambda([]()
	{
		UE_LOG(LogIVSmoke, Log, TEXT("[FIVSmokeModule::StartupModule] OnPostEngineInit fired"));
		FIVSmokeSceneViewExtension::Initialize();
	});
#endif
}

void FIVSmokeModule::ShutdownModule()
{
#if !UE_SERVER
	FIVSmokeSceneViewExtension::Shutdown();
#endif
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FIVSmokeModule, IVSmoke)
