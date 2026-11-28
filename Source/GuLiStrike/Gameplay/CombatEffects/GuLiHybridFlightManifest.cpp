#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Gameplay/CombatEffects/GuLiLinearFlightContract.h"
#include "GuLiNetGameplayTypes.h"
#include "Gameplay/CombatEffects/GuLiRuntimeFieldManifest.h"

bool GuLiRuntimeFields::AddRules(FGuLiHybridManifestData& Data,const FGuLiHybridRuleEntry& Damage,FString& Error)
{
    const FString Definition=TEXT("effect/runtime/")+Damage.Id;
    if (!FGuLiHybridAdapterRegistry::BindDefinition(Data,Definition,TEXT("CombatEffect"),Error)) return false;
    FGuLiHybridRuleEntry Admission;
    Admission.Id=TEXT("admit/runtime/")+Damage.Id; Admission.SourceDefinition=Damage.SourceDefinition;
    Admission.Kind=EGuLiHybridRuleKind::Spawn; Admission.TargetDefinition=Definition;
    Admission.SpawnKind=EGuLiHybridObjectKind::Field; Admission.SpawnCost=.001;
    Admission.MaximumLiveChildren=2048; Admission.MaximumFactionObjects=16384; Admission.EffectTicks=Damage.EffectTicks;
    Admission.MaximumTargets=1; Admission.RequiredCapabilityCatalog=Damage.RequiredCapabilityCatalog;
    Admission.RequiredCapabilityGroup=Damage.RequiredCapabilityGroup; Admission.RequiredCapability=Damage.RequiredCapability;
    FGuLiHybridRuleEntry Settlement=Damage;
    Settlement.Id=TEXT("settle/")+Definition; Settlement.SourceDefinition=Definition;
    Settlement.RequiredCapabilityCatalog.Reset(); Settlement.RequiredCapabilityGroup.Reset(); Settlement.RequiredCapability.Reset();
    Settlement.MaximumTargets=GuLiNet::MaximumEffectTargets; Settlement.CooldownTicks=0;
    FGuLiHybridRuleEntry Retirement;
    Retirement.Id=TEXT("retire/")+Definition; Retirement.SourceDefinition=Retirement.TargetDefinition=Definition;
    Retirement.Kind=EGuLiHybridRuleKind::Destroy; Retirement.EffectTicks=Damage.EffectTicks;
    for (const auto& Rule:{Admission,Settlement,Retirement})
    {
        if (Data.Rules.ContainsByPredicate([&](const auto& Existing){return Existing.Id==Rule.Id;}))
        { Error=TEXT("Duplicate combat-effect manifest rule: ")+Rule.Id; return false; }
        Data.Rules.Add(Rule);
    }
    if (const auto* Amount = Data.Numbers.Find(TEXT("air_damage/") + Damage.Id))
    {
        Settlement.Id += TEXT("/air");
        Settlement.Damage = *Amount;
        Data.Rules.Add(Settlement);
    }
    return true;
}

static bool AppendFlightCatalog(FGuLiHybridManifestData& Data,FString& Error)
{
    const auto Original=Data.Rules;
    TSet<FString> Ids; for (const auto& R:Original) Ids.Add(R.Id);
    for (const auto& Damage:Original)
    {
        if (Damage.Kind!=EGuLiHybridRuleKind::Damage || Damage.Damage<=0) continue;
        const FString Definition=TEXT("effect/linear/")+Damage.Id;
        if (!FGuLiHybridAdapterRegistry::BindDefinition(Data,Definition,TEXT("LinearFlight"),Error)) return false;
        FGuLiHybridRuleEntry Admission;
        Admission.Id=TEXT("admit/linear/")+Damage.Id; Admission.SourceDefinition=Damage.SourceDefinition;
        Admission.Kind=EGuLiHybridRuleKind::Spawn; Admission.TargetDefinition=Definition;
        Admission.SpawnKind=EGuLiHybridObjectKind::Flight; Admission.SpawnCost=.001;
        Admission.MaximumLiveChildren=2048; Admission.MaximumFactionObjects=16384;
        Admission.EffectTicks=Damage.EffectTicks; Admission.MaximumTargets=1;
        Admission.RequiredCapabilityCatalog=Damage.RequiredCapabilityCatalog;
        Admission.RequiredCapabilityGroup=Damage.RequiredCapabilityGroup;Admission.RequiredCapability=Damage.RequiredCapability;
        FGuLiHybridRuleEntry Settlement=Damage;
        Settlement.RequiredCapabilityCatalog.Reset();Settlement.RequiredCapabilityGroup.Reset();Settlement.RequiredCapability.Reset();
        Settlement.Id=TEXT("settle/")+Definition; Settlement.SourceDefinition=Definition;
        Settlement.MaximumTargets=1; Settlement.CooldownTicks=0;
        FGuLiHybridRuleEntry Retirement;
        Retirement.Id=TEXT("retire/")+Definition; Retirement.SourceDefinition=Retirement.TargetDefinition=Definition;
        Retirement.Kind=EGuLiHybridRuleKind::Destroy; Retirement.EffectTicks=Damage.EffectTicks;
        for (auto R:{Admission,Settlement,Retirement})
        {
            if (Ids.Contains(R.Id)) { Error=TEXT("Duplicate linear-flight manifest rule: ")+R.Id; return false; }
            Ids.Add(R.Id); Data.Rules.Add(MoveTemp(R));
        }
        if (!GuLiRuntimeFields::AddRules(Data,Damage,Error)) return false;
    }
    Data.Strings.Add(TEXT("startup/acceptance_flight_adapter"),TEXT("2"));
    return true;
}

static const FGuLiHybridManifestRegistration FlightsManifestProvider({TEXT("Flights"),EGuLiHybridManifestPhase::Derived,{},[](UWorld&,FGuLiHybridManifestData& Data,FString& Error) {return AppendFlightCatalog(Data,Error);}});
