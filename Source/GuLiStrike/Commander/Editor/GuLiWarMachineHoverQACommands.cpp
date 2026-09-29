// Explicit, transient fixture control for the authorized 500-unit hover comparison.
#if WITH_EDITOR
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Gameplay/Building/GuLiBuildingProductionComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

namespace GuLiWarMachineHoverQA
{
void Hold(UWorld* World)
{
	if (!World || World->WorldType != EWorldType::PIE || World->GetNetMode() == NM_Client
		|| !World->GetMapName().Contains(TEXT("LVL_CommanderMassPrototype"))) return;
	auto* Authority = World->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	auto* Tasks = World->GetSubsystem<UGuLiUnitTaskSubsystem>();
	if (!Authority || !Tasks || !Authority->HasSpawnedAuthorityPopulation()) return;
	TArray<FGuLiSoldierStateItem> States;
	Authority->BuildSoldierStateSnapshot(States);
	if (States.Num() != 500 || States.ContainsByPredicate([](const auto& S) { return !S.IsAlive(); }))
	{
		UE_LOG(LogTemp, Warning, TEXT("HoverQA Hold requires a fresh 500-unit PIE population."));
		return;
	}
	// Normal stop orders suppress automatic advance without pausing the Mass simulation,
	// animation clock, presentation upload, or renderer. Ending PIE restores the authored world.
	for (const EGuLiTeam Team : {EGuLiTeam::Red, EGuLiTeam::Blue})
	{
		TArray<FGuLiSoldierId> Ids;
		for (const auto& S : States) if (S.Team == Team) Ids.Add(S.SoldierId);
		FGuLiCommanderSelectionState Selection;
		if (!Authority->SetExplicitSelection(Team, Ids, {}, Selection)) return;
		FActorSpawnParameters Params; Params.ObjectFlags = RF_Transient;
		auto* Owner = World->SpawnActor<AGuLiBattlePlayerState>(Params);
		if (!Owner) return;
		Owner->SetReplicates(false);
		Owner->SetServerRoleAssignment(Team, EGuLiCommanderRole::Commander, int32(Team));
		FGuLiUnitTaskCommand Command;
		Command.CommandId = 1; Command.SelectionRevision = Selection.SelectionRevision;
		Command.Disposition = EGuLiTaskDisposition::Stop;
		FString Message; int32 Accepted = 0, Rejected = 0;
		Tasks->Submit(*Owner, Selection, Command, Message, Accepted, Rejected);
		UE_LOG(LogTemp, Display, TEXT("HoverQA Hold team=%d accepted=%d rejected=%d %s"), int32(Team), Accepted, Rejected, *Message);
	}
	for (TActorIterator<AActor> It(World); It; ++It)
		if (auto* Production = It->FindComponentByClass<UGuLiBuildingProductionComponent>())
			Production->SetComponentTickEnabled(false);
}
FAutoConsoleCommandWithWorld HoldCommand(TEXT("gs.Commander.HoverQA.Hold"),
	TEXT("Mass prototype PIE only: stop the existing 500 units and pause production for hover A/B. End PIE to discard."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&Hold));
}
#endif
