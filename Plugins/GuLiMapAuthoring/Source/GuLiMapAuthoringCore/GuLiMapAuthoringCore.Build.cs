using UnrealBuildTool;

public class GuLiMapAuthoringCore : ModuleRules
{
    public GuLiMapAuthoringCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "Json", "GeometryCore" });
    }
}
