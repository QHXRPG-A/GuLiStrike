#include "Battle/Framework/GuLiBattleGameState.h"
#include "Gameplay/Stronghold/GuLiHybridControlPoint.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Resources/GuLiResourceActors.h"
#include "Gameplay/Stronghold/GuLiStrongholdCaptureComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Economy/GuLiTeamEconomySubsystem.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/GuLiNetworkSettings.h"

namespace
{
	TAutoConsoleVariable<float> CVarStrongholdSample(TEXT("guli.stronghold.SampleSeconds"), .25f, TEXT("Authority ground dominance sample interval."));
	TAutoConsoleVariable<float> CVarStrongholdCapture(TEXT("guli.stronghold.CaptureSeconds"), 20.f, TEXT("Neutral to endpoint seconds; full reversal takes twice this."));
	TAutoConsoleVariable<float> CVarStrongholdUpkeep(TEXT("guli.stronghold.UpkeepSeconds"), 30.f, TEXT("Periodic upkeep, unchanged by ownership transfers."));
	TAutoConsoleVariable<int32> CVarStrongholdBlueCost(TEXT("guli.stronghold.UpkeepBlue"), 1, TEXT("Blue cost per owned Territory per upkeep cycle."));
	TAutoConsoleVariable<int32> CVarStrongholdRedCost(TEXT("guli.stronghold.UpkeepRed"), 1, TEXT("Red cost per owned Territory per upkeep cycle."));
}

FVector UGuLiResourceWorldSubsystem::GetTerritoryGroundLocation(int32 Index) const
{
	return WorldState->GetTerritories()[Index].GroundLocation;
}
int32 UGuLiResourceWorldSubsystem::FindTerritoryIndexById(FName TerritoryId) const
{
	return MapDefinition ? MapDefinition->Territories.IndexOfByPredicate(
		[TerritoryId](const auto& Territory) { return Territory.TerritoryId == TerritoryId; }) : INDEX_NONE;
}
FVector UGuLiResourceWorldSubsystem::GetInitialBaseExit(EGuLiTeam Team) const
{
	check(GuLiResources::IsPlayableTeam(Team));
	return ProjectAnchorToGround(Team == EGuLiTeam::Red ? MapDefinition->SpawnAnchors.RedAssembly : MapDefinition->SpawnAnchors.BlueAssembly);
}
bool UGuLiResourceWorldSubsystem::IsTerritorySupplied(int32 Index) const
{
	return Index == INDEX_NONE || (WorldState && WorldState->GetTerritories().IsValidIndex(Index)
		&& WorldState->GetTerritories()[Index].bSupplied);
}
bool UGuLiResourceWorldSubsystem::CanUseStrongholdTransit(int32 Index, EGuLiTeam Team) const
{
	if (!WorldState || !WorldState->GetTerritories().IsValidIndex(Index)) return false;
	const auto& State = WorldState->GetTerritories()[Index];
	return GuLiResources::IsPlayableTeam(Team) && State.Owner == Team;
}
int32 UGuLiResourceWorldSubsystem::GetTransportRegion(EGuLiTeam Team, int32 TerritoryIndex) const
{
	const auto* Regions=TransportRegions.Find(Team);
	return Regions && Regions->IsValidIndex(TerritoryIndex) ? (*Regions)[TerritoryIndex] : INDEX_NONE;
}
void UGuLiResourceWorldSubsystem::RefreshEncirclement()
{
    if (!WorldState || !MapDefinition || Outposts.Num()!=MapDefinition->Territories.Num()
        || Outposts.ContainsByPredicate([](const auto& P){return !IsValid(P);})) return;
	auto Owner = [this](int32 Index) { return WorldState->GetTerritoryOwner(Index); };
	const TArray<bool> Red = StrongholdTopology.FindEncircled(EGuLiTeam::Red, Owner);
	const TArray<bool> Blue = StrongholdTopology.FindEncircled(EGuLiTeam::Blue, Owner);
	TransportRegions.Add(EGuLiTeam::Red,StrongholdTopology.FindTransportRegions(EGuLiTeam::Red, Owner));
	TransportRegions.Add(EGuLiTeam::Blue,StrongholdTopology.FindTransportRegions(EGuLiTeam::Blue, Owner));
	for (int32 Index = 0; Index < Red.Num(); ++Index)
		WorldState->ApplyTerritoryEncirclement(Index, Red[Index] || Blue[Index]);
	TArray<FGuLiTransportNode> Nodes;
	for (int32 Index = 0; Index < Outposts.Num(); ++Index)
	{
		const EGuLiTeam Team = Owner(Index);
		if (!CanUseStrongholdTransit(Index,Team)) continue;
		const auto& Definition = Outposts[Index]->FindComponentByClass<UGuLiBuildingLifecycleComponent>()->GetDefinition();
		Nodes.Add({Index,MapDefinition->Territories[Index].TerritoryId,Team,GetTerritoryGroundLocation(Index),Definition.TransitFieldId,
			GetTransportRegion(Team,Index)});
	}
	if (TransportNetwork.Rebuild(MoveTemp(Nodes))) WorldState->ApplyTransportNetwork(TransportNetwork.GetSnapshot());
}
bool UGuLiResourceWorldSubsystem::RegisterHybridOutpost(AGuLiTerritoryOutpostActor& Outpost,int32 Index)
{
    if (!MapDefinition || !MapDefinition->Territories.IsValidIndex(Index)) return false;
    Outposts.SetNum(MapDefinition->Territories.Num());
    if (IsValid(Outposts[Index]) && Outposts[Index]!=&Outpost) return false;
    Outposts[Index]=&Outpost;RefreshEncirclement();return true;
}
void UGuLiResourceWorldSubsystem::UnregisterHybridOutpost(AGuLiTerritoryOutpostActor& Outpost,int32 Index)
{if (Outposts.IsValidIndex(Index) && Outposts[Index]==&Outpost) Outposts[Index]=nullptr;}
void UGuLiResourceWorldSubsystem::ApplyHybridControlPoint(const FGuLiHybridObjectState& State,bool Effects)
{
    const auto C=GuLiHybridControlPoint::Read(State);
    if (!WorldState || !Outposts.IsValidIndex(C.Index) || !Outposts[C.Index]) return;
    const EGuLiTeam Previous=WorldState->GetTerritoryOwner(uint8(C.Index));
    const bool Changed=Previous!=C.Owner || IsTerritorySupplied(C.Index)!=State.bSupplied;
    if (!WorldState->ApplyHybridTerritory(C.Index,C.Owner,State.bSupplied)) return;
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();auto* P=Outposts[C.Index].Get();
    P->ApplyHybridOwner(C.Owner,H?H->GetLogicalSeconds():0);
    auto* Capture=P->FindComponentByClass<UGuLiStrongholdCaptureComponent>();Capture->ApplyHybridCapture(C.Progress,int32(C.Red),int32(C.Blue));
    if (Changed) RefreshEncirclement();
    if (Effects && Previous!=C.Owner && H && H->HasSimulation(State.Key,true))
    {
        OnTerritoryOwnershipChanged.Broadcast(C.Index,Previous,C.Owner);
        Capture->OnCaptureRewardRequested.Broadcast(C.Index,C.Owner);
    }
}
FIntPoint UGuLiResourceWorldSubsystem::GetHybridCaptureCounts(uint64 Tick,int32 Territory)
{
    const auto* Battle=GetWorld()->GetGameState<AGuLiBattleGameState>();const uint32 Epoch=Battle?Battle->GetMatchEpoch():0;
    if (HybridCaptureTick!=Tick || HybridCaptureEpoch!=Epoch)
    {
        HybridCaptureTick=Tick;HybridCaptureEpoch=Epoch;HybridCaptureCounts.Init(FIntPoint::ZeroValue,MapDefinition?MapDefinition->Territories.Num():0);
        TArray<FGuLiCombatTargetSnapshot> Targets;GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>()->GetTargetSnapshots(Targets);
        auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        for (const auto& Target:Targets)
        {
            if (!Target.bAlive || (Target.Handle.Kind!=EGuLiTargetKind::CommanderSoldier && Target.Handle.Kind!=EGuLiTargetKind::GroundActor)) continue;
            if (const auto* A=Target.CollisionActor.Get())
            {
                if (A->FindComponentByClass<UGuLiBuildingLifecycleComponent>()) continue;
                const auto* Control=A->FindComponentByClass<UGuLiExternalUnitControlComponent>();if (Control && Control->IsPhased()) continue;
            }
            const int32 Index=FindTerritoryIndex(Target.Location);if (!HybridCaptureCounts.IsValidIndex(Index)) continue;
            FGuLiHybridObjectKey Key;FGuLiHybridObjectState State;
            if (!H || !H->ResolveTarget(Target.Handle,Key) || !H->GetObject(Key,State) || State.bPhased) continue;
            if (Target.Team==EGuLiTeam::Red) ++HybridCaptureCounts[Index].X;else if (Target.Team==EGuLiTeam::Blue) ++HybridCaptureCounts[Index].Y;
        }
    }
    return HybridCaptureCounts.IsValidIndex(Territory)?HybridCaptureCounts[Territory]:FIntPoint::ZeroValue;
}
