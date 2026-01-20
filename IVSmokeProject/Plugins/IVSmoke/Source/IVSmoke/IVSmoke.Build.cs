// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IVSmoke : ModuleRules
{
	public IVSmoke(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Public API only - no Internal/Private engine headers required
		// Uses SubscribeToPostProcessingPass pattern with FPostProcessMaterialInputs

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
				"Engine",
				"DeveloperSettings",
				"NetCore"
			}
		);
	}
}
