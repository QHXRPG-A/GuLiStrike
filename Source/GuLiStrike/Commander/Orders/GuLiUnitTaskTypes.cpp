#include "Commander/Orders/GuLiUnitTaskTypes.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/GuLiNetworkSettings.h"
#include "Engine/World.h"

FGuLiTaskUnitId FGuLiTaskUnitId::Soldier(FGuLiSoldierId Id,const UWorld* World)
{
    FGuLiTaskUnitId V; V.Id=Id.Value;
    if (World && UGuLiNetworkSettings::IsHybrid(World))
    {
        const auto* Authority=World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
        V.Generation=Authority?Authority->MakeSoldierTargetHandle(Id).Generation:0;
    }
    return V;
}
FGuLiTaskUnitId FGuLiTaskUnitId::Actor(FGuLiControllableActorId Id,const UWorld* World)
{
    FGuLiTaskUnitId V; V.Id=Id.Value; V.bActor=true;
    if (World && UGuLiNetworkSettings::IsHybrid(World))
    {
        const auto* Hybrid=World->GetSubsystem<UGuLiHybridWorldSubsystem>();
        V.Generation=Hybrid?Hybrid->GetActorTaskGeneration(Id.Value):0;
    }
    return V;
}

bool FGuLiUnitTaskCommand::IsWellFormed() const
{
	if (!CommandId || !SelectionRevision || uint8(Disposition) > uint8(EGuLiTaskDisposition::Stop)
		|| uint8(Kind) > uint8(EGuLiUnitTaskKind::Transit) || Target.ContainsNaN()) return false;
	if (Disposition == EGuLiTaskDisposition::Stop) return true;
	if (Kind == EGuLiUnitTaskKind::Special) return SpecialTaskId > 0;
	return Kind != EGuLiUnitTaskKind::Transit || !TerritoryId.IsNone();
}
