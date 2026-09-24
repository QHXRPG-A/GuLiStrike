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
			"AIModule",
			"Json",
			"Landscape",
			"Foliage", // LandscapeEdit.h exposes foliage integration to editor callers.
			"NavigationSystem",
			"Navmesh",
			"Niagara",
			"NiagaraEditor",
			"GuLiFlightNavigationRuntime",
			"GuLiFlightNavigationEditor",
			"DeveloperToolSettings",
			"StructUtils",
			"UnrealEd",
			"GuLiMapAuthoringCore",
			"GuLiMapAuthoringEditor"
		});
	}
}
