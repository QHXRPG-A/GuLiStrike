using UnrealBuildTool;

public class GuLiNetCore : ModuleRules
{
    public GuLiNetCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        if (Target.LinkType == TargetLinkType.Modular)
        {
            PublicDefinitions.Add("GULINETCORE_SHARED=1");
            PrivateDefinitions.Add("GULINETCORE_EXPORTS=1");
        }
        // Only Module.cpp depends on Core. Portable sources also build with CMake.
        PrivateDependencyModuleNames.Add("Core");
    }
}
