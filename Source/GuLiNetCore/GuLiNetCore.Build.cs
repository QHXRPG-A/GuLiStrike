using UnrealBuildTool;

public class GuLiNetCore : ModuleRules
{
    public GuLiNetCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;
        // Portable translation units have independent internal helpers.
        bUseUnity = false;
        CppStandard = CppStandardVersion.Cpp20;
        PrivateDefinitions.Add("GULINET_CARD_GM=" + (Target.Configuration == UnrealTargetConfiguration.Shipping ? "0" : "1"));
        if (Target.LinkType == TargetLinkType.Modular)
        {
            PublicDefinitions.Add("GULINETCORE_SHARED=1");
            PrivateDefinitions.Add("GULINETCORE_EXPORTS=1");
        }
        // Only Module.cpp depends on Core. Portable sources also build with CMake.
        PrivateDependencyModuleNames.Add("Core");
    }
}
