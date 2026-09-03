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
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"Niagara",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"GuLiFlightNavigationRuntime",
			// Commander 网络合同使用 FastArray 与自定义 NetSerialize。
			"NetCore",
			// Non-shipping skill network QA writes structured JSON evidence.
			"Json",
			// Landscape：相机避障命中过滤需要 ALandscapeProxy 类型判断
			"Landscape",
			// Mass 框架（UE5.7：核心 MassEntity 已并入引擎，其余在 MassGameplay/MassAI/MassCrowd 插件中）
			"MassEntity",
			"MassCommon",
			"MassActors",
			"MassSpawner",
			"MassSimulation",
			"MassSignals",
			"MassRepresentation",
			"MassLOD",
			"MassMovement",
			"MassNavigation",
			"MassNavMeshNavigation",
			"MassZoneGraphNavigation",
			"MassCrowd",
			"MassAIBehavior",
			"ZoneGraph"
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"AssetRegistry",
				"MaterialUtilities",
				"MeshMergeUtilities",
				"UnrealEd",
				"MeshUtilities",
				"MeshDescription",
				"StaticMeshDescription",
				"SlateCore",
				"ImageCore",
				"RenderCore",
				"StaticMeshEditor",
				"StateTreeEditorModule",
				"PropertyBindingUtils"
			});
		}

		PublicIncludePaths.AddRange(new string[] {
			"GuLiStrike",
			"GuLiStrike/AI",
			"GuLiStrike/Gameplay",
			"GuLiStrike/Gameplay/Ship",
			"GuLiStrike/Gameplay/Data/Generated",
			"GuLiStrike/UI",
			"GuLiStrike/Variant_Strategy",
			"GuLiStrike/Variant_Strategy/UI",
			"GuLiStrike/Commander",
			"GuLiStrike/Commander/Network",
			"GuLiStrike/Commander/Framework",
			"GuLiStrike/Commander/Mass"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add the plugins section in your uproject file with the Enabled attribute set to true
	}
}
