#include "Network/GuLiHybridAdapterRegistry.h"
#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridManifest.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Gameplay/Data/GuLiCommanderSoldierResolver.h"
#include "Engine/DataTable.h"
#include "HAL/IConsoleManager.h"

namespace
{
// Authoring input only. Runtime population is the committed owner capability.
TAutoConsoleVariable<int32> CVarAuthoredArmyPopulationLimit(TEXT("guli.stronghold.TeamUnitCap"),300,
    TEXT("Army population limit exported into the match manifest."));
}
#include "Network/Private/GuLiHybridComponentState.h"
namespace
{
bool Append(UWorld&,FGuLiHybridManifestData& M,FString& Error)
{
    const auto* Table=GetDefault<UGuLiUnitDataSettings>()->SoldierDataTable.LoadSynchronous();
    if (!Table) {Error=TEXT("Missing unit definitions");return false;}
    TSet<uint16> Types;
    const auto* PopulationLimit=IConsoleManager::Get().FindConsoleVariable(TEXT("guli.stronghold.TeamUnitCap"));
    if (!PopulationLimit || PopulationLimit->GetInt()<1 || PopulationLimit->GetInt()>100000) {Error=TEXT("Invalid authored population limit");return false;}
    M.Numbers.Add(TEXT("population/group/army/limit"),PopulationLimit->GetInt());
    for (auto Row:Table->GetRowNames())
    {
        bool Valid=false;const auto D=FGuLiCommanderSoldierResolver::Resolve(Table,Row,FGuLiCommanderSoldierResolver::MakeFallbackDefinition(),Valid);
        if (!Valid || !D.UnitTypeId || Types.Contains(D.UnitTypeId)) {Error=TEXT("Invalid or duplicate unit definition");return false;}Types.Add(D.UnitTypeId);
        const FString Definition=FString::Printf(TEXT("unit/%u"),D.UnitTypeId);
        M.Numbers.Add(TEXT("unit_definition/")+Definition+TEXT("/type_id"),D.UnitTypeId);
        if (D.UsesMass() && !FGuLiHybridAdapterRegistry::BindDefinition(M,Definition,TEXT("Mass"),Error)) return false;
        if (D.UsesMass()) M.Numbers.Add(TEXT("relocation/target/")+Definition,0);
        if (!D.UsesMass() && D.ActorClass && !FGuLiHybridComponentState::BindDefinitionLayout(M,Definition,*D.ActorClass.Get(),Error)) return false;
        if (D.UsesMass() && !D.bSummonOnly)
        {
            M.Strings.Add(TEXT("population/definition/")+Definition,TEXT("army"));
            M.Numbers.Add(TEXT("population/definition/")+Definition+TEXT("/units"),1);
        }
    }
    return true;
}
const FGuLiHybridManifestRegistration Provider({TEXT("UnitDefinitions"),EGuLiHybridManifestPhase::Definitions,{},Append});
}
