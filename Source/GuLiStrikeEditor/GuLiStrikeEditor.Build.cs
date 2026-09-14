// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GuLiStrikeEditor : ModuleRules
{
	public GuLiStrikeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GuLiStrike"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"AssetRegistry",
			"Json",
			"Landscape",
			"NavigationSystem",
			"StructUtils",
			"UnrealEd",
			"GuLiMapAuthoringCore",
			"GuLiMapAuthoringEditor"
		});
	}
}
