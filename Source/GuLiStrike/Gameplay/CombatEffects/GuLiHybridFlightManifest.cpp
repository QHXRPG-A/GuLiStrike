#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Gameplay/CombatEffects/GuLiLinearFlightContract.h"
#include "GuLiNetGameplayTypes.h"

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
        const FString RuntimeDefinition=TEXT("effect/runtime/")+Damage.Id;
        if (!FGuLiHybridAdapterRegistry::BindDefinition(Data,RuntimeDefinition,TEXT("CombatEffect"),Error)) return false;
        Admission.Id=TEXT("admit/runtime/")+Damage.Id;Admission.TargetDefinition=RuntimeDefinition;
        Admission.SpawnKind=EGuLiHybridObjectKind::Field;
        Settlement.Id=TEXT("settle/")+RuntimeDefinition;Settlement.SourceDefinition=RuntimeDefinition;
        // A pulse may hit several distinct objects. Its stable root is per pulse;
        // retries retain that root, so a repeated target cannot be charged twice.
        // Pending receipts span many effects; they are not a per-pulse limit.
        Settlement.MaximumTargets=GuLiNet::MaximumEffectTargets;
        Retirement.Id=TEXT("retire/")+RuntimeDefinition;
        Retirement.SourceDefinition=Retirement.TargetDefinition=RuntimeDefinition;
        for (auto R:{Admission,Settlement,Retirement})
        {
            if (Ids.Contains(R.Id)) {Error=TEXT("Duplicate combat-effect manifest rule: ")+R.Id;return false;}
            Ids.Add(R.Id);Data.Rules.Add(MoveTemp(R));
        }
    }
    Data.Strings.Add(TEXT("startup/acceptance_flight_adapter"),TEXT("2"));
    return true;
}

static const FGuLiHybridManifestRegistration FlightsManifestProvider({TEXT("Flights"),EGuLiHybridManifestPhase::Derived,{},[](UWorld&,FGuLiHybridManifestData& Data,FString& Error) {return AppendFlightCatalog(Data,Error);}});
