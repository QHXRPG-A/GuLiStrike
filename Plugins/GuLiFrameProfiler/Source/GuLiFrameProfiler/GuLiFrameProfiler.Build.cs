using UnrealBuildTool;

public class GuLiFrameProfiler : ModuleRules
{
    public GuLiFrameProfiler(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.Add("Core");
        PrivateDependencyModuleNames.AddRange(new[] {
            "CoreUObject", "Engine", "UnrealEd", "InputCore", "Slate", "SlateCore",
            "LevelEditor", "ToolMenus", "RenderCore", "RHI", "Json", "TraceServices", "TraceAnalysis"
        });
    }
}
