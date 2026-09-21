#include "Commander/UI/GuLiCommanderUIPresentation.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

FGuLiCommanderUIPresentation BuildGuLiCommanderUIPresentation(UWorld* World,
	const FGuLiCommanderSelectionState& Selection, const AGuLiSoldierStateReplicator* Roster, bool bReady)
{
	FGuLiCommanderUIPresentation View; View.SelectionRevision = Selection.SelectionRevision;
	if (!World) return View;
	if (!bReady) { View.bSyncing = (!Selection.Cohorts.IsEmpty() || !Selection.ActorIds.IsEmpty()); return View; }
	TSet<FGuLiSoldierId> Seen;
	for (const auto& Cohort : Selection.Cohorts) for (auto Id : Cohort.MemberIds)
	{
		if (Seen.Contains(Id)) continue; Seen.Add(Id);
		const auto* State = Roster ? Roster->FindSoldierState(Id) : nullptr;
		if (!State) { View.bSyncing = true; continue; }
		if (!State->IsAlive()) continue;
		auto& Row = View.Members.AddDefaulted_GetRef(); Row.Soldier = Id; Row.Type = State->UnitTypeId;
		Row.Health = State->Health; Row.MaximumHealth = State->MaxHealth; Row.bTransporting = State->bPhased;
	}
	TSet<FGuLiControllableActorId> Pending(Selection.ActorIds);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		const auto* Vehicle = Cast<IGuLiEngineeringVehicle>(*It);
		if (!Vehicle || !Pending.Remove(Vehicle->GetStableActorId())) continue;
		const auto* Health = It->FindComponentByClass<UGuLiCombatHealthComponent>();
		if (!Health || !Health->IsAlive()) continue;
		auto& Row = View.Members.AddDefaulted_GetRef(); Row.Actor = Vehicle->GetStableActorId(); Row.Type = uint16(Vehicle->GetUnitTypeId());
		const auto& State = Health->GetHealthState(); Row.Health = State.Health; Row.MaximumHealth = State.MaxHealth;
		Row.Shield = State.Shield; Row.MaximumShield = State.MaxShield;
		const auto* Travel = It->FindComponentByClass<UGuLiEngineeringTravelComponent>();
		Row.bTransporting = Travel && Travel->IsInTransit();
	}
	View.bSyncing |= !Pending.IsEmpty();
	View.Members.Sort([](const auto& A, const auto& B)
	{
		if (A.Type != B.Type) return A.Type < B.Type;
		if (A.Actor.IsValid() != B.Actor.IsValid()) return !A.Actor.IsValid();
		return A.StableId() < B.StableId();
	});
	for (const auto& Row : View.Members) ++View.Types.FindOrAdd(Row.Type);
	return View;
}
