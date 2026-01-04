// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class IVSmoke : ModuleRules
{
	public IVSmoke(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// FPostProcessingInputs is required for PrePostProcessPass_RenderThread (ISceneViewExtension).
		// Despite the function being public API, FPostProcessingInputs is defined in Renderer/Internal.
		// No public alternative exists for accessing scene textures at this rendering stage.
		// Usage is confined to cpp files only; not exposed in any public headers.
		PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Internal"));

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"RenderCore",
				"RHI",
				"Renderer",
				"Projects"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine"
			}
		);
	}
}
