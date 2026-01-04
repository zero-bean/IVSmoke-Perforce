// Copyright Epic Games, Inc. All Rights Reserved.

#include "IVSmoke.h"
#include "IVSmokeSceneViewExtension.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"

#define LOCTEXT_NAMESPACE "FIVSmokeModule"

void FIVSmokeModule::StartupModule()
{
	FString PluginShaderDir = FPaths::Combine(
		IPluginManager::Get().FindPlugin(TEXT("IVSmoke"))->GetBaseDir(),
		TEXT("Shaders")
	);
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/IVSmoke"), PluginShaderDir);

	// SceneViewExtension requires GEngine, defer until engine is ready
	UE_LOG(LogTemp, Warning, TEXT("IVSmoke: Registering OnPostEngineInit"));
	FCoreDelegates::OnPostEngineInit.AddLambda([]()
	{
		UE_LOG(LogTemp, Warning, TEXT("IVSmoke: OnPostEngineInit fired"));
		FIVSmokeSceneViewExtension::Initialize();
	});
}

void FIVSmokeModule::ShutdownModule()
{
	FIVSmokeSceneViewExtension::Shutdown();
	ResetAllShaderSourceDirectoryMappings();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FIVSmokeModule, IVSmoke)
