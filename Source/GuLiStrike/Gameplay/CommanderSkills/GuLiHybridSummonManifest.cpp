#include "Gameplay/CommanderSkills/GuLiSummonSkill.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"
#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridManifest.h"
#include "GuLiNetObjectBatch.h"
#include "Engine/World.h"
namespace
{
bool Append(UWorld&,FGuLiHybridManifestData& M,FString& Error)
{
    const auto* C=GetDefault<UGuLiCommanderSkillSettings>()->Catalog.LoadSynchronous();
    if (!C) {Error=TEXT("Active skill catalog is unavailable");return false;}
    TArray<FGuLiSoldierDefinition> Units;if (!UGuLiUnitDataSubsystem::ResolveAuthoredDefinitions(Units,Error)) return false;
    for (const auto& [Unit,Id]:C->UnitSkills)
    {
        const auto* Skill=C->FindSkill(Id);const auto* Config=Skill?Cast<UGuLiSummonSkillConfiguration>(Skill->Configuration):nullptr;
        if (!Config) continue;const auto* Target=Units.FindByPredicate([&](const auto& D){return D.UnitTypeId==Config->UnitTypeId;});
        if (!Target || !Target->UsesMass() || !Target->bSummonOnly || !Skill->ExecutorClass
            || !Skill->ExecutorClass->GetDefaultObject<UGuLiCommanderSkillExecutor>()->ValidateDefinition(*Skill,Error)) return false;
        FGuLiHybridRuleEntry R;R.SourceDefinition=FString::Printf(TEXT("unit/%d"),Unit);R.TargetDefinition=FString::Printf(TEXT("unit/%d"),Config->UnitTypeId);
        R.Id=TEXT("summon/")+R.SourceDefinition+TEXT("/")+Id.ToString().ToLower();R.Kind=EGuLiHybridRuleKind::Capability;
        R.Capability=UTF8_TO_TCHAR(GuLiNet::ObjectBatch::Capability);R.CapabilityVersion=GuLiNet::ObjectBatch::Version;R.CapabilityOperation=TEXT("create");
        R.SpawnKind=EGuLiHybridObjectKind::Soldier;R.SpawnHealth=Target->MaxHealth;R.SpawnControllerSeat=0;
        R.MaximumLiveChildren=Config->Count;R.MaximumFactionObjects=10000;R.CooldownTicks=FMath::CeilToInt64(Skill->CooldownSeconds*60.);R.EffectTicks=600;
        M.Rules.Add(MoveTemp(R));M.Numbers.Add(TEXT("capability/object_batch/version"),GuLiNet::ObjectBatch::Version);
    }
    return true;
}
const FGuLiHybridManifestRegistration Provider({TEXT("SummonSkills"),EGuLiHybridManifestPhase::Definitions,{},Append});
}
