#include "Commander/UI/GuLiCommanderUIPresentation.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridAdapterRegistry.h"

FGuLiCommanderUIPresentation BuildGuLiCommanderUIPresentation(UWorld* World,
	const FGuLiCommanderSelectionState& Selection, const AGuLiSoldierStateReplicator* Roster, bool bReady)
{
	FGuLiCommanderUIPresentation View; View.SelectionRevision = Selection.SelectionRevision;
	if (!World) return View;
	if (!bReady) { View.bSyncing = (!Selection.Cohorts.IsEmpty() || !Selection.ActorIds.IsEmpty() || !Selection.Objects.IsEmpty()); return View; }
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
	for (TActorIterator<APawn> It(World); It && !Pending.IsEmpty(); ++It)
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
    if (auto* H=World->GetSubsystem<UGuLiHybridWorldSubsystem>())
    {
        TArray<FName> Definitions;H->GetCommanderDefinitions(Definitions);
        const auto* A=World->GetSubsystem<UGuLiHybridActorWorldSubsystem>();
        for (const auto& K:Selection.Objects)
        {
            FGuLiHybridObjectState O;if (!H->GetObjectView(K,O)) {View.bSyncing=true;continue;}
            if (O.LifeState!=1) continue;
            auto& Row=View.Members.AddDefaulted_GetRef();Row.Object=K;Row.Definition=O.Definition;Row.Ordinal=O.Ordinal;
            Row.Type=65536u+uint32(Definitions.IndexOfByKey(O.Definition));
            Row.Health=O.Health.Health;Row.MaximumHealth=O.Health.MaxHealth;Row.Shield=O.Health.Shield;Row.MaximumShield=O.Health.MaxShield;
            Row.bTransporting=O.bPhased;
            if (A) if (const auto* Label=A->GetManifest().Strings.Find(O.Definition.ToString()+TEXT("/DisplayName"))) Row.DisplayName=*Label;
            if (Row.DisplayName.IsEmpty()) Row.DisplayName=TEXT("已选对象");
        }
    }
	View.Members.Sort([](const auto& A, const auto& B)
	{
		if (A.Type != B.Type) return A.Type < B.Type;
		if (A.Actor.IsValid() != B.Actor.IsValid()) return !A.Actor.IsValid();
		return A.StableId() < B.StableId();
	});
	for (const auto& Row : View.Members) ++View.Types.FindOrAdd(Row.Type);
	return View;
}
