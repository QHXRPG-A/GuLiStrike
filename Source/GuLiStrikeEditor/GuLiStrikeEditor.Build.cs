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
			"EditorSubsystem",
			"GuLiStrike"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"AssetRegistry",
			"GeometryCore", "GeometryAlgorithms", "MeshDescription", "StaticMeshDescription",
			"AIModule",
			"MassEntity", // PIE selection reads the local commander's presented transform.
			"Json",
			"Slate", "SlateCore", // Opt-in per-window Prepass diagnostics; no engine changes.
			"Landscape",
			"Foliage", // LandscapeEdit.h exposes foliage integration to editor callers.
			"NavigationSystem",
			"Navmesh",
			"Niagara",
			"NiagaraEditor",
			"UMG", "InputCore", "RHI", // Scoped rogue-card lifecycle and GPU timing acceptance adapters.
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
