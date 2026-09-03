using UnrealBuildTool;

public class GuLiFlightNavigationEditor : ModuleRules
{
	public GuLiFlightNavigationEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GuLiFlightNavigationRuntime"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"DataValidation",
			"UnrealEd"
		});
	}
}
