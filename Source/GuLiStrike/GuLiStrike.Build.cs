// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GuLiStrike : ModuleRules
{
	public GuLiStrike(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"Niagara",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"GuLiStrike",
			"GuLiStrike/AI",
			"GuLiStrike/Gameplay",
			"GuLiStrike/UI",
			"GuLiStrike/Variant_Strategy",
			"GuLiStrike/Variant_Strategy/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add the plugins section in your uproject file with the Enabled attribute set to true
	}
}
