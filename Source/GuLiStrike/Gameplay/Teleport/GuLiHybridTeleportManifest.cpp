#include "Gameplay/CommanderSkills/GuLiTeleportSkill.h"
#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/Private/GuLiHybridEntityState.h"
#include "GuLiNetRelocation.h"
#include "Engine/World.h"
namespace
{
const FGuLiHybridManifestRegistration Teleport({TEXT("Relocation"),EGuLiHybridManifestPhase::Derived,{TEXT("ArmyState"),TEXT("UnitDefinitions"),TEXT("Ship")},
    [](UWorld&,FGuLiHybridManifestData& M,FString& Error)
    {
        const auto* Catalog=GetDefault<UGuLiCommanderSkillSettings>()->Catalog.LoadSynchronous();
        if (!Catalog) {Error=TEXT("Missing relocation skill catalogue");return false;}
        bool Any=false;
        for (FName Skill:Catalog->GlobalSkills)
        {
            const auto* D=Catalog->FindSkill(Skill);
            if (!D||!D->ExecutorClass||!D->ExecutorClass->IsChildOf(UGuLiTeleportSkillExecutor::StaticClass())) continue;
            Any=true;
            for (int32 Level=1;Level<=D->MaximumLevel;++Level)
            {
                FGuLiTeleportFieldConfig Config;if (!UGuLiTeleportSkillExecutor::ResolveAuthoredConfiguration(*D,Level,Config)) {Error=TEXT("Missing relocation tier: ")+Skill.ToString();return false;}
                const auto* C=&Config;
                const auto Key=FString::Printf(TEXT("relocation/profile/%s/%d"),*Skill.ToString(),Level);M.Strings.Add(Key,Skill.ToString());const auto P=Key+TEXT("/");
                M.Numbers.Add(P+TEXT("level"),Level);M.Numbers.Add(P+TEXT("radius"),C->RadiusCentimeters);M.Numbers.Add(P+TEXT("height"),C->MaxShipHeightCentimeters);M.Numbers.Add(P+TEXT("beam"),C->BeamHeightCentimeters);
                M.Numbers.Add(P+TEXT("windup"),FMath::CeilToDouble(C->WindupSeconds*60.));M.Numbers.Add(P+TEXT("wait"),FMath::CeilToDouble(C->MaxTargetWaitSeconds*60.));
                M.Numbers.Add(P+TEXT("recovery"),FMath::CeilToDouble(C->RecoverySeconds*60.));M.Numbers.Add(P+TEXT("cooldown"),FMath::CeilToDouble(D->CooldownSeconds*60.));M.Numbers.Add(P+TEXT("vehicles"),C->bAllowPlayerVehicles?1:0);
            }
        }
        if (!Any) return true;
        const auto Root=M.Strings.FindRef(TEXT("army/root_definition"));if (Root.IsEmpty()) {Error=TEXT("Missing global ability authority root");return false;}
        M.Strings.Add(TEXT("relocation/caster/")+Root,TEXT("RelocationCast"));if (!FGuLiHybridEntityState::Bind(M,Root,TEXT("RelocationCast"),Error)) return false;
        const auto Add=[&](const FString& D,bool Ack)
        {
            auto& R=M.Rules.AddDefaulted_GetRef();R.Id=(Ack?TEXT("relocation/ack/"):TEXT("relocation/cast/"))+D;R.SourceDefinition=D;
            R.Kind=EGuLiHybridRuleKind::Capability;R.Capability=Ack?TEXT("relocation_body"):TEXT("relocation_cast");R.CapabilityVersion=GuLiNet::Relocation::Version;
            R.CapabilityOperation=Ack?TEXT("ack"):TEXT("mutate");R.EffectTicks=36000;R.MaximumTargets=1;
        };
        Add(Root,false);TArray<FString> Definitions;
        for (const auto& P:M.Numbers) if (P.Key.StartsWith(TEXT("relocation/target/"))) Definitions.Add(P.Key.RightChop(18));
        Definitions.Sort();for (const auto& D:Definitions)
        {
            if (!M.Strings.Contains(TEXT("adapter/definition/")+D)||!FGuLiHybridEntityState::Bind(M,D,TEXT("RelocationBody"),Error)) return false;
            Add(D,true);
        }
        return true;
    }});
}
