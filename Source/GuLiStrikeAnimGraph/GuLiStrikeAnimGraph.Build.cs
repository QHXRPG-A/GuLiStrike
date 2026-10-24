using UnrealBuildTool;

// Blueprint authoring nodes must also exist during cook. Pose evaluation remains in GuLiStrike.
public class GuLiStrikeAnimGraph : ModuleRules
{
    public GuLiStrikeAnimGraph(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "GuLiStrike", "AnimGraphRuntime", "AnimGraph", "BlueprintGraph", "UnrealEd"
        });
    }
}
