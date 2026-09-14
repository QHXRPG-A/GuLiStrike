using UnrealBuildTool;

public class GuLiMapAuthoringEditor : ModuleRules
{
    public GuLiMapAuthoringEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "EditorSubsystem", "GuLiMapAuthoringCore" });
        PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "EditorFramework", "Slate", "SlateCore", "InputCore", "ToolMenus", "LevelEditor", "PropertyEditor", "StructUtilsEditor", "AssetRegistry", "AssetTools", "Projects", "DeveloperSettings", "RenderCore", "RHI", "Json", "Landscape" });
    }
}
