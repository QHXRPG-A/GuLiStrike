#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Gameplay/Data/GuLiGameText.h"
#include "Commander/Orders/GuLiSpecialTaskCatalog.h"
#include "Commander/Orders/GuLiSpecialTaskExecutor.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Building/GuLiConstructionWorkComponent.h"
#include "Gameplay/Building/GuLiBuildingRegistrySubsystem.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AIController.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiMoveOrders, Log, All);

namespace
{
	bool IsUnfinishedMove(const FGuLiTaskExecution& Task)
	{
		return !Task.bAutomatic && Task.Command.Kind == EGuLiUnitTaskKind::Move
			&& (Task.Status == EGuLiTaskStatus::Waiting || Task.Status == EGuLiTaskStatus::Running);
	}
	FGuLiTaskExecution* MoveToPlan(FGuLiUnitTaskState& State)
	{
		return State.PendingMove.IsSet() ? &*State.PendingMove : State.Active.IsSet() ? &*State.Active : nullptr;
	}
	TArray<FGuLiTaskUnitId> Members(const FGuLiCommanderSelectionState& Selection)
	{
		TArray<FGuLiTaskUnitId> Result;
		for (const auto& Cohort : Selection.Cohorts) for (auto Id : Cohort.MemberIds) Result.Add(FGuLiTaskUnitId::Soldier(Id));
		for (auto Id : Selection.ActorIds) Result.Add(FGuLiTaskUnitId::Actor(Id));
		return Result;
	}
	FString BasicName(EGuLiUnitTaskKind Kind)
	{
		switch (Kind) { case EGuLiUnitTaskKind::Move: return GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.110")); case EGuLiUnitTaskKind::ReturnToFactory: return GuLiGameText::Text(TEXT("UI.ConsoleLayout.018"));
		case EGuLiUnitTaskKind::Transit: return GuLiGameText::Text(TEXT("UI.ConsoleLayout.020")); default: return GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.111")); }
	}
}
bool UGuLiUnitTaskSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}
void UGuLiUnitTaskSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection); Collection.InitializeDependency<UGuLiSpecialTaskCatalog>();
	Collection.InitializeDependency<UGuLiBuildingRegistrySubsystem>();
	Catalog = GetWorld()->GetSubsystem<UGuLiSpecialTaskCatalog>();
	auto* Registry = GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>();
	Registry->OnSpawned.AddUObject(this, &ThisClass::WakeAutomaticBuilders);
	Registry->OnCompleted.AddUObject(this, &ThisClass::WakeAutomaticBuilders);
	Registry->OnDestroyed.AddUObject(this, &ThisClass::WakeAutomaticBuilders);
}
void UGuLiUnitTaskSubsystem::Deinitialize()
{
	if (auto* Registry = GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>())
	{ Registry->OnSpawned.RemoveAll(this); Registry->OnCompleted.RemoveAll(this); Registry->OnDestroyed.RemoveAll(this); }
	Planning.Reset(); States.Reset(); GrantedUnits.Reset(); Super::Deinitialize();
}
void UGuLiUnitTaskSubsystem::WakeAutomaticBuilders(UGuLiBuildingLifecycleComponent&)
{
	for (auto& Pair : States) if (Pair.Key.bActor) Pair.Value.NextAutomaticTime = 0;
}
TStatId UGuLiUnitTaskSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiUnitTaskSubsystem, STATGROUP_Tickables); }
uint32 UGuLiUnitTaskSubsystem::AllocateExecutionId() { if (!NextExecutionId) ++NextExecutionId; return NextExecutionId++; }
void UGuLiUnitTaskSubsystem::Grant(FGuLiUnitTaskState& State)
{
	if (!Catalog || !Catalog->IsValidCatalog()) return;
	const bool bAlreadyGranted = GrantedUnits.Contains(State.Context.Unit);
	GrantedUnits.Add(State.Context.Unit);
	for (const auto& Definition : Catalog->GetDefinitions())
		if (Definition.UnitTypes.Contains(State.Context.UnitTypeId))
			State.Grants.Add({Definition.Id, Definition.Lifetime, bAlreadyGranted && Definition.Lifetime == EGuLiTaskLifetime::InitialOnce});
}
void UGuLiUnitTaskSubsystem::RegisterActor(APawn& Pawn)
{
	const auto* Vehicle = Cast<IGuLiEngineeringVehicle>(&Pawn);
	if (!Vehicle || !Pawn.HasAuthority() || !Vehicle->GetStableActorId().IsValid()) return;
	const auto Id = FGuLiTaskUnitId::Actor(Vehicle->GetStableActorId());
	if (States.Contains(Id)) return;
	auto& State = States.Add(Id); State.Context.Unit = Id; State.Context.Pawn = &Pawn;
	State.Context.Team = Vehicle->GetTeam(); State.Context.UnitTypeId = uint16(Vehicle->GetUnitTypeId());
	State.Context.Location = Pawn.GetActorLocation(); Grant(State);
}
void UGuLiUnitTaskSubsystem::RegisterSoldiers(EGuLiTeam Team, TConstArrayView<FGuLiSoldierId> Soldiers, int32 SourceTerritory)
{
	auto* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	for (auto Soldier : Soldiers)
	{
		const auto Id = FGuLiTaskUnitId::Soldier(Soldier);
		if (auto* Existing = States.Find(Id)) { if (Existing->Context.SourceTerritory == INDEX_NONE) Existing->Context.SourceTerritory = SourceTerritory; continue; }
		FGuLiUnitTaskState State; State.Context.Unit = Id; State.Context.SourceTerritory = SourceTerritory;
		if (!Authority->GetTaskSoldierInfo(Soldier, State.Context.Team, State.Context.UnitTypeId, State.Context.Location) || State.Context.Team != Team) continue;
		Grant(State); States.Add(Id, MoveTemp(State));
	}
}
void UGuLiUnitTaskSubsystem::UnregisterActor(FGuLiControllableActorId Id) { States.Remove(FGuLiTaskUnitId::Actor(Id)); }
bool UGuLiUnitTaskSubsystem::RefreshContext(FGuLiUnitTaskState& State) const
{
	if (!State.Context.Unit.bActor)
		return GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->GetTaskSoldierInfo(FGuLiSoldierId(State.Context.Unit.Id),
			State.Context.Team, State.Context.UnitTypeId, State.Context.Location);
	auto* Pawn = State.Context.Pawn.Get(); if (!Pawn) return false;
	const auto* Vehicle = Cast<IGuLiEngineeringVehicle>(Pawn);
	const auto* Health = Pawn->FindComponentByClass<UGuLiCombatHealthComponent>();
	if (!Vehicle || !Health || !Health->IsAlive()) return false;
	State.Context.Team = Vehicle->GetTeam(); State.Context.Location = Pawn->GetActorLocation(); return true;
}
bool UGuLiUnitTaskSubsystem::Admit(FGuLiUnitTaskState& State, const FGuLiUnitTaskCommand& Command, int32 Capacity)
{
	if (Command.Disposition == EGuLiTaskDisposition::Append && State.ManualTaskCount() >= Capacity) return false;
	if (Command.Disposition != EGuLiTaskDisposition::Append)
	{
		State.PendingMove.Reset();
		++State.Version; State.Queue.Reset(); State.ConsumeInitialGrants();
		State.bCancelPending = State.Active.IsSet();
	}
	State.bStopped = Command.Disposition == EGuLiTaskDisposition::Stop;
	if (!State.bStopped) State.Queue.Add(Command);
	if (Command.Disposition == EGuLiTaskDisposition::Append && State.Active.IsSet()) State.Active->bYieldRequested = true;
	State.Error.Reset(); State.NextAutomaticTime = 0; return true;
}
bool UGuLiUnitTaskSubsystem::ReuseMove(FGuLiUnitTaskState& State,
	const FGuLiUnitTaskCommand& Command, float RadiusCentimeters)
{
	if (State.bStopped || Command.Disposition != EGuLiTaskDisposition::Replace
		|| Command.Kind != EGuLiUnitTaskKind::Move || Command.Target.ContainsNaN()
		|| !FMath::IsFinite(RadiusCentimeters) || RadiusCentimeters < 0) return false;
	const FGuLiUnitTaskCommand* Previous = nullptr;
	bool bQueued = false;
	if (State.PendingMove.IsSet() && IsUnfinishedMove(*State.PendingMove)) Previous = &State.PendingMove->Command;
	else if (!State.bCancelPending && State.Active.IsSet() && IsUnfinishedMove(*State.Active)) Previous = &State.Active->Command;
	else if ((State.bCancelPending || !State.Active.IsSet()) && !State.Queue.IsEmpty()
		&& State.Queue[0].Kind == EGuLiUnitTaskKind::Move)
	{ Previous = &State.Queue[0]; bQueued = true; }
	if (!Previous || FVector::DistSquared2D(Previous->Target, Command.Target)
		> FMath::Square(double(RadiusCentimeters))) return false;
	// Keep the original anchor and execution identity, including an in-flight planner.
	if (bQueued) State.Queue.SetNum(1); else State.Queue.Reset();
	if (State.Active.IsSet() && !State.bCancelPending) State.Active->bYieldRequested = false;
	State.Error.Reset();
	return true;
}
void UGuLiUnitTaskSubsystem::LogMoveFailure(const FGuLiUnitTaskState& State,
	const FGuLiTaskExecution& Task, const TCHAR* Reason) const
{
	UE_LOG(LogGuLiMoveOrders, Log, TEXT("Move failed command=%u unit=%s:%u target=%s retained=%s reason=%s"),
		Task.Command.CommandId, State.Context.Unit.bActor ? TEXT("Actor") : TEXT("Soldier"), State.Context.Unit.Id,
		*FVector(Task.Command.Target).ToString(), State.Active.IsSet() ? *FVector(State.Active->Command.Target).ToString() : TEXT("none"), Reason);
}
void UGuLiUnitTaskSubsystem::DiscardPendingMove(FGuLiUnitTaskState& State)
{
	if (!State.PendingMove.IsSet()) return;
	if (!State.Context.Unit.bActor)
	{
		const FGuLiSoldierId Id(State.Context.Unit.Id);
		GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->InvalidateTaskSoldierPlans(MakeArrayView(&Id, 1));
	}
	State.PendingMove.Reset();
}
void UGuLiUnitTaskSubsystem::CancelPendingMove(FGuLiTaskUnitId Unit)
{
	if (auto* State = States.Find(Unit)) DiscardPendingMove(*State);
}
bool UGuLiUnitTaskSubsystem::Validate(const FGuLiUnitTaskState& State, const FGuLiUnitTaskCommand& Command, FString& Error) const
{
	if (Command.Disposition == EGuLiTaskDisposition::Stop) return true;
	if (Command.Kind == EGuLiUnitTaskKind::Special)
	{
		const auto* Definition = Catalog->Find(Command.SpecialTaskId);
		if (!Definition || !Definition->UnitTypes.Contains(State.Context.UnitTypeId)) { Error = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.112")); return false; }
		if (Definition->Lifetime == EGuLiTaskLifetime::InitialOnce)
		{
			const auto* Grant = State.Grants.FindByPredicate([&](const auto& Candidate) { return Candidate.TaskId == Definition->Id; });
			if (!Grant || Grant->bConsumed) { Error = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.113")); return false; }
		}
		return Definition->Executor->Validate(*GetWorld(), State.Context, Command, Error);
	}
	if (Command.Kind == EGuLiUnitTaskKind::ReturnToFactory)
	{
		if (Cast<AGuLiMiningVehiclePawn>(State.Context.Pawn.Get())) return true;
		Error = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.114")); return false;
	}
	if (Command.Kind == EGuLiUnitTaskKind::Transit)
	{
		auto* Pawn = State.Context.Pawn.Get(); if (!Pawn) { Error = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.115")); return false; }
		FGuLiStrongholdTransitOrder Order; Order.RequestId = 1; Order.SelectionRevision = 1;
		Order.TerritoryId = Command.TerritoryId; Order.ClickLocation = Command.Target;
		FGuLiPreparedTransit Prepared;
		auto* Travel = Pawn->FindComponentByClass<UGuLiEngineeringTravelComponent>();
		if (Travel->IsInTransit())
		{
			const auto* Resources = GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
			const int32 Target = Resources->FindTerritoryIndexById(Command.TerritoryId);
			if (Target != INDEX_NONE && Resources->GetResourceWorldState()->GetTerritoryOwner(Target) == State.Context.Team
				&& Resources->CanUseStrongholdTransit(Target, State.Context.Team)) return true;
			Error = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.116")); return false;
		}
		if (Travel->PrepareTransport(Order, Prepared) == EGuLiTransitOrderResult::Accepted) return true;
		Error = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.117")); return false;
	}
	const auto* Resources = GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
	if (Resources && Resources->IsRuntimeReady() && !Resources->GetPlayableBounds().IsInside(FVector2D(Command.Target)))
	{ Error = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.118")); return false; }
	return true;
}
bool UGuLiUnitTaskSubsystem::Submit(AGuLiBattlePlayerState& Owner, const FGuLiCommanderSelectionState& Selection,
	const FGuLiUnitTaskCommand& Command, FString& Message, int32& Accepted, int32& Rejected, TSet<FGuLiTaskUnitId>* AcceptedUnits)
{
	Accepted = Rejected = 0;
	if (AcceptedUnits) AcceptedUnits->Reset();
	if (!Owner.IsCommander() || !Command.IsWellFormed() || Command.SelectionRevision != Selection.SelectionRevision)
	{ Message = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.119")); return false; }
	// Adopt a plan that committed since the last 10 Hz task tick before superseding it.
	TickMoveBatches();
	for (auto Unit : Members(Selection))
	{
		if (!States.Contains(Unit) && !Unit.bActor) { const FGuLiSoldierId Id(Unit.Id); RegisterSoldiers(Owner.GetTeam(), MakeArrayView(&Id, 1)); }
		auto* State = States.Find(Unit); FString Error;
		if (!State || !RefreshContext(*State) || State->Context.Team != Owner.GetTeam())
		{
			++Rejected;
			if (Error.IsEmpty()) Error = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.120"));
			Message = Error;
			if (State && State->Context.Team == Owner.GetTeam()) State->Error = Error;
			continue;
		}
		if (State->Active.IsSet() && IsUnfinishedMove(*State->Active) && State->Active->bStarted && !State->Active->bPlanning)
			State->Active->Status = Poll(*State);
		const bool bMoveReplacement = Command.Kind == EGuLiUnitTaskKind::Move
			&& Command.Disposition == EGuLiTaskDisposition::Replace;
		if (!Validate(*State, Command, Error))
		{
			if (bMoveReplacement && (State->PendingMove.IsSet()
				|| (State->Active.IsSet() && IsUnfinishedMove(*State->Active))))
			{
				FGuLiTaskExecution Failed; Failed.Command = Command;
				LogMoveFailure(*State, Failed, *Error);
				DiscardPendingMove(*State); State->Queue.Reset(); State->Error.Reset();
				++Accepted; if (AcceptedUnits) AcceptedUnits->Add(Unit);
				continue;
			}
			++Rejected; Message = State->Error = Error; continue;
		}
		if (ReuseMove(*State, Command, GetDefault<UGuLiUnitTaskSettings>()->MoveReuseDistanceCentimeters))
		{
			State->Owner = &Owner; ++Accepted; if (AcceptedUnits) AcceptedUnits->Add(Unit);
			continue;
		}
		if (bMoveReplacement && !State->bStopped && !State->bCancelPending
			&& (State->PendingMove.IsSet() || (State->Active.IsSet() && IsUnfinishedMove(*State->Active))))
		{
			// Retire only scratch planning. The committed route remains live until success.
			if (!Unit.bActor)
			{
				const FGuLiSoldierId Id(Unit.Id);
				GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->InvalidateTaskSoldierPlans(MakeArrayView(&Id, 1));
			}
			FGuLiTaskExecution Replacement; Replacement.Command = Command;
			Replacement.Version = ++State->Version; Replacement.ExecutionId = AllocateExecutionId();
			State->PendingMove.Emplace(MoveTemp(Replacement));
			State->Queue.Reset(); State->ConsumeInitialGrants(); State->Error.Reset();
			if (State->Active.IsSet()) State->Active->bYieldRequested = false;
			State->Owner = &Owner; ++Accepted; if (AcceptedUnits) AcceptedUnits->Add(Unit);
			continue;
		}
		if (Command.Disposition != EGuLiTaskDisposition::Append) DiscardPendingMove(*State);
		if (!Admit(*State, Command, FMath::Clamp(GetDefault<UGuLiUnitTaskSettings>()->MaximumManualTasks, 1, 32)))
		{ ++Rejected; Message = State->Error = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.121")); continue; }
		State->Owner = &Owner; ++Accepted;
		if (AcceptedUnits) AcceptedUnits->Add(Unit);
		if (Command.Disposition != EGuLiTaskDisposition::Append)
		{
			State->bCancelPending = !Cancel(*State);
			if (!State->bCancelPending) State->Active.Reset();
		}
	}
	if (!Accepted && !Rejected) Message = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.122"));
	return Accepted > 0;
}
bool UGuLiUnitTaskSubsystem::StartVehicleMove(FGuLiUnitTaskState& State, FGuLiTaskExecution& Task)
{
	auto* Pawn = State.Context.Pawn.Get();
	auto* Travel = Pawn ? Pawn->FindComponentByClass<UGuLiEngineeringTravelComponent>() : nullptr;
	FGuLiPreparedGroundMove Prepared;
	if (!Travel || !Travel->PrepareGroundMove(Task.Command.Target, 100, Prepared)) return false;
	if (auto* Miner = Cast<AGuLiMiningVehiclePawn>(Pawn))
	{
		FGuLiMiningCommand Command; Command.RequestId = Task.ExecutionId; Command.SelectionRevision = 1;
		Command.Type = EGuLiMiningOrderType::Move; Command.Target = Task.Command.Target;
		return Miner->StartPreparedManagedMove(Command, Prepared);
	}
	return Travel->CommitGroundMove(Prepared);
}
bool UGuLiUnitTaskSubsystem::Cancel(FGuLiUnitTaskState& State)
{
	if (State.Active.IsSet() && State.Active->Command.Kind == EGuLiUnitTaskKind::Special)
		if (const auto* Definition = Catalog->Find(State.Active->Command.SpecialTaskId))
			return Definition->Executor->Cancel(*GetWorld(), State.Context, *State.Active);
	if (auto* Pawn = State.Context.Pawn.Get())
	{
		if (auto* Miner = Cast<AGuLiMiningVehiclePawn>(Pawn)) return Miner->StopManagedTask();
		if (auto* Work = Pawn->FindComponentByClass<UGuLiConstructionWorkComponent>()) Work->StopWork();
		return Pawn->FindComponentByClass<UGuLiEngineeringTravelComponent>()->StopAtSafePoint();
	}
	const FGuLiSoldierId Id(State.Context.Unit.Id);
	GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->StopTaskSoldiers(MakeArrayView(&Id, 1)); return true;
}
EGuLiTaskStatus UGuLiUnitTaskSubsystem::Start(FGuLiUnitTaskState& State)
{
	auto& Task = *State.Active;
	if (Task.Command.Kind == EGuLiUnitTaskKind::Special)
	{
		const auto* Definition = Catalog->Find(Task.Command.SpecialTaskId);
		return Definition ? Definition->Executor->Start(*GetWorld(), State.Context, Task) : EGuLiTaskStatus::Failed;
	}
	auto* Pawn = State.Context.Pawn.Get();
	if (!Pawn) return EGuLiTaskStatus::Waiting; // Mass moves enter the existing batched planner below.
	auto* Travel = Pawn->FindComponentByClass<UGuLiEngineeringTravelComponent>();
	if (UGuLiExternalUnitControlComponent::AreActorActionsLocked(Pawn)) return EGuLiTaskStatus::Waiting;
	if (Task.Command.Kind == EGuLiUnitTaskKind::Move)
		return StartVehicleMove(State, Task) ? EGuLiTaskStatus::Running : EGuLiTaskStatus::Failed;
	if (Task.Command.Kind == EGuLiUnitTaskKind::Transit)
	{
		FGuLiStrongholdTransitOrder Order; Order.RequestId = int32(Task.ExecutionId); Order.SelectionRevision = 1;
		Order.TerritoryId = Task.Command.TerritoryId; Order.ClickLocation = Task.Command.Target;
		FGuLiPreparedTransit Prepared;
		if (Travel->PrepareTransport(Order, Prepared) != EGuLiTransitOrderResult::Accepted) { Task.Error = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.123")); return EGuLiTaskStatus::Failed; }
		Travel->BeginTransport(Prepared); return EGuLiTaskStatus::Running;
	}
	if (auto* Miner = Cast<AGuLiMiningVehiclePawn>(Pawn))
	{
		FGuLiMiningCommand Command; Command.RequestId = Task.ExecutionId; Command.SelectionRevision = 1; Command.Target = Task.Command.Target;
		Command.Type = Task.Command.Kind == EGuLiUnitTaskKind::ReturnToFactory ? EGuLiMiningOrderType::ReturnToFactory : EGuLiMiningOrderType::Move;
		return Miner->StartManagedTask(Command, false) ? EGuLiTaskStatus::Running : EGuLiTaskStatus::Waiting;
	}
	return Travel->BeginMove(Task.Command.Target, 100) ? EGuLiTaskStatus::Running : EGuLiTaskStatus::Failed;
}
EGuLiTaskStatus UGuLiUnitTaskSubsystem::Poll(FGuLiUnitTaskState& State)
{
	auto& Task = *State.Active;
	if (Task.Command.Kind == EGuLiUnitTaskKind::Special)
	{
		const auto* Definition = Catalog->Find(Task.Command.SpecialTaskId);
		return Definition ? Definition->Executor->Poll(*GetWorld(), State.Context, Task) : EGuLiTaskStatus::Failed;
	}
	if (auto* Pawn = State.Context.Pawn.Get())
	{
		auto* Travel = Pawn->FindComponentByClass<UGuLiEngineeringTravelComponent>();
		if (Travel->IsRouting() || UGuLiExternalUnitControlComponent::AreActorActionsLocked(Pawn)) return EGuLiTaskStatus::Running;
		if (Task.Command.Kind == EGuLiUnitTaskKind::Transit && Task.WorkSerial == 0)
		{
			++Task.WorkSerial;
			if (!Travel->BeginMove(Task.Command.Target, 100)) { Task.Error = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.124")); return EGuLiTaskStatus::Failed; }
			return EGuLiTaskStatus::Running;
		}
		if (Task.Command.Kind != EGuLiUnitTaskKind::Transit)
			if (auto* Miner = Cast<AGuLiMiningVehiclePawn>(Pawn))
				return !Miner->IsManagedTaskComplete() ? EGuLiTaskStatus::Running : Miner->DidManagedTaskFail() ? EGuLiTaskStatus::Failed : EGuLiTaskStatus::Completed;
		const auto* AI = Cast<AAIController>(Pawn->GetController());
		if (AI && AI->GetMoveStatus() != EPathFollowingStatus::Idle) return EGuLiTaskStatus::Running;
		return FVector::Dist2D(State.Context.Location, Task.Command.Target) <= 140 ? EGuLiTaskStatus::Completed : EGuLiTaskStatus::Failed;
	}
	if (Task.bPlanning) return EGuLiTaskStatus::Running;
	FGuLiSoldierNavigationDebug Nav;
	if (!GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->TryGetSoldierNavigationDebug(FGuLiSoldierId(State.Context.Unit.Id), Nav)) return EGuLiTaskStatus::Failed;
	if (Nav.ActiveOrderId) return EGuLiTaskStatus::Running;
	return Nav.State == EGuLiSoldierNavigationState::Arrived ? EGuLiTaskStatus::Completed : EGuLiTaskStatus::Failed;
}
void UGuLiUnitTaskSubsystem::Advance(FGuLiUnitTaskState& State, double Now)
{
	if (State.bCancelPending)
	{
		if (!Cancel(State)) { if (State.Active.IsSet()) State.Active->Status = EGuLiTaskStatus::WaitingSafeExit; return; }
		State.bCancelPending = false; State.Active.Reset();
	}
	if (State.bStopped) return;
	if (State.PendingMove.IsSet())
	{
		// An old route may arrive naturally while planning. Do not let its cleanup
		// cancel the replacement or start a later queued task before it resolves.
		if (State.Active.IsSet() && State.Active->bStarted && !State.Active->bPlanning)
		{
			State.Active->Status = Poll(State);
			if (!IsUnfinishedMove(*State.Active)) State.Active.Reset();
		}
		if (State.Context.Unit.bActor)
		{
			if (UGuLiExternalUnitControlComponent::AreActorActionsLocked(State.Context.Pawn.Get()))
			{ DiscardPendingMove(State); return; }
			if (StartVehicleMove(State, *State.PendingMove))
			{
				State.PendingMove->bStarted = true; State.PendingMove->Status = EGuLiTaskStatus::Running;
				State.Active = MoveTemp(State.PendingMove); State.PendingMove.Reset();
			}
			else { LogMoveFailure(State, *State.PendingMove, TEXT("VehiclePath")); State.PendingMove.Reset(); }
		}
		return;
	}
	if (State.Active.IsSet())
	{
		auto& Task = *State.Active;
		Task.Status = Task.bStarted ? Poll(State) : Start(State);
		if (Task.Status == EGuLiTaskStatus::Waiting) return;
		Task.bStarted = true;
		if (Task.Status == EGuLiTaskStatus::Running) return;
		const auto* Definition = Catalog->Find(Task.Command.SpecialTaskId);
		if (Task.Status == EGuLiTaskStatus::WorkUnitComplete && !Task.bAutomatic && State.Queue.IsEmpty()
			&& Definition && Definition->Executor->RepeatsWhenLast())
		{
			Task.bStarted = false; Task.ExecutionId = AllocateExecutionId(); Task.Status = EGuLiTaskStatus::Waiting; return;
		}
		if (Task.Status == EGuLiTaskStatus::Failed)
		{
			if (Task.Command.Kind == EGuLiUnitTaskKind::Move) LogMoveFailure(State, Task, TEXT("Execution"));
			else State.Error = Task.Error.IsEmpty() ? GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.125")) : Task.Error;
		}
		if (Task.bAutomatic)
			for (auto& Grant : State.Grants) if (Grant.TaskId == Task.Command.SpecialTaskId) Grant.ConsumeIfInitial();
		Cancel(State); State.Active.Reset();
		State.NextAutomaticTime = Now + GetDefault<UGuLiUnitTaskSettings>()->AutomaticRetrySeconds;
	}
	if (!State.Queue.IsEmpty())
	{
		State.ConsumeInitialGrants();
		FGuLiTaskExecution Task; Task.Command = State.Queue[0]; State.Queue.RemoveAt(0);
		Task.Version = ++State.Version; Task.ExecutionId = AllocateExecutionId(); State.Active.Emplace(MoveTemp(Task)); return;
	}
	if (Now < State.NextAutomaticTime || !Catalog->IsValidCatalog()) return;
	State.NextAutomaticTime = Now + FMath::Max(.1f, GetDefault<UGuLiUnitTaskSettings>()->AutomaticRetrySeconds);
	for (const auto& Grant : State.Grants)
	{
		const auto* Definition = Catalog->Find(Grant.TaskId);
		if (Grant.bConsumed || !Definition || !Definition->bAutoActivate) continue;
		FGuLiTaskExecution Task; Task.bAutomatic = true; Task.Command.Kind = EGuLiUnitTaskKind::Special;
		Task.Command.SpecialTaskId = Definition->Id; Task.Command.SelectionRevision = 1;
		if (!Definition->Executor->BuildAutomatic(*GetWorld(), State.Context, Task.Command)) continue;
		Task.ExecutionId = AllocateExecutionId(); Task.Command.CommandId = Task.ExecutionId; Task.Version = ++State.Version;
		State.Active.Emplace(MoveTemp(Task)); break;
	}
}
void UGuLiUnitTaskSubsystem::Tick(float DeltaTime)
{
	Accumulator += DeltaTime; if (Accumulator < .1) return; Accumulator = 0;
	TickMoveBatches();
	const double Now = GetWorld()->GetTimeSeconds();
	for (auto It = States.CreateIterator(); It; ++It)
	{
		if (!RefreshContext(It.Value())) { DiscardPendingMove(It.Value()); It.RemoveCurrent(); continue; }
		Advance(It.Value(), Now);
	}
	StartMoveBatches();
}
void UGuLiUnitTaskSubsystem::StartMoveBatches()
{
	auto* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	TSet<EGuLiTeam> Busy;
	for (const auto& Batch : Planning) if (Batch.Owner.IsValid()) Busy.Add(Batch.Owner->GetTeam());
	for (auto& Pair : States)
	{
		auto& State = Pair.Value;
		if (State.Context.Unit.bActor || State.bCancelPending || State.bStopped || !State.Owner.IsValid()) continue;
		auto* CandidateTask = MoveToPlan(State); if (!CandidateTask) continue;
		auto& Task = *CandidateTask;
		if (Task.Command.Kind != EGuLiUnitTaskKind::Move || Task.bStarted || Busy.Contains(State.Context.Team)) continue;
		TArray<FGuLiSoldierId> Units; FMoveBatch Batch; Batch.Owner = State.Owner; Batch.Id = Task.ExecutionId;
		for (auto& Candidate : States)
		{
			auto& Other = Candidate.Value;
			if (Other.Context.Unit.bActor || Other.Owner != State.Owner || Other.bStopped || Other.bCancelPending) continue;
			const auto* NextTask = MoveToPlan(Other); if (!NextTask) continue;
			const auto& Next = *NextTask;
			if (Next.bStarted || Next.Command.Kind != EGuLiUnitTaskKind::Move || Next.Command.CommandId != Task.Command.CommandId
				|| !FVector(Next.Command.Target).Equals(Task.Command.Target, .01)) continue;
			Units.Add(FGuLiSoldierId(Candidate.Key.Id)); Batch.Versions.Add(Candidate.Key, Next.Version);
			if (Other.PendingMove.IsSet()) Batch.Replacements.Add(Candidate.Key);
		}
		FGuLiCommanderSelectionState Frozen;
		if (!Authority->SetExplicitSelection(State.Context.Team, Units, {}, Frozen)) continue;
		Batch.Selection = Frozen;
		FGuLiMoveRequest Request; Request.Target = Task.Command.Target; Request.ClientCommandId = Batch.Id; Request.SelectionRevision = Frozen.SelectionRevision;
		FGuLiCommandAck Ack;
		if (!Authority->BeginMovePlanning(*State.Owner, Request, Frozen, Ack))
		{
			if (Ack.Result != EGuLiCommandAckResult::RateLimited)
				for (const auto& Unit : Batch.Versions) if (auto* Failed = States.Find(Unit.Key))
				{
					auto& Execution = Batch.Replacements.Contains(Unit.Key) ? Failed->PendingMove : Failed->Active;
					if (Execution.IsSet() && Execution->Version == Unit.Value)
					{ LogMoveFailure(*Failed, *Execution, TEXT("PlanningAdmission")); Execution.Reset(); }
				}
			Busy.Add(State.Context.Team); continue;
		}
		for (const auto& Unit : Batch.Versions)
		{
			auto& Next = *MoveToPlan(States.FindChecked(Unit.Key));
			Next.bStarted = true; Next.bPlanning = true; Next.Status = EGuLiTaskStatus::Running;
		}
		Planning.Add(MoveTemp(Batch)); Busy.Add(State.Context.Team);
	}
}
void UGuLiUnitTaskSubsystem::TickMoveBatches()
{
	auto* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	for (int32 I = Planning.Num()-1; I >= 0; --I)
	{
		auto& Batch = Planning[I]; FGuLiCommandAck Ack; FGuLiCommanderSelectionState Updated; bool Changed = false;
		const auto Result = Batch.Owner.IsValid() ? Authority->PollMovePlanning(*Batch.Owner, Batch.Id, Ack, Updated, Changed) : EGuLiMovePlanningStatus::NotFound;
		if (Result == EGuLiMovePlanningStatus::Pending) continue;
		TSet<FGuLiTaskUnitId> Accepted;
		if (Result == EGuLiMovePlanningStatus::Completed)
			for (const auto& Cohort : Batch.Selection.Cohorts)
				if (const auto* Receipt = Ack.CohortResults.FindByPredicate([&](const auto& Entry) { return Entry.CohortId == Cohort.CohortId; }))
					for (int32 M = 0; M < Cohort.MemberIds.Num() && M < 32; ++M)
						if (Receipt->AcceptedMemberMask & (1u << M)) Accepted.Add(FGuLiTaskUnitId::Soldier(Cohort.MemberIds[M]));
		for (const auto& Member : Batch.Versions)
		{
			auto* State = States.Find(Member.Key);
			if (!State || State->bCancelPending) continue;
			const bool bReplacement = Batch.Replacements.Contains(Member.Key);
			auto& Execution = bReplacement ? State->PendingMove : State->Active;
			if (!Execution.IsSet() || Execution->Version != Member.Value) continue;
			if (Accepted.Contains(Member.Key))
			{
				Execution->bPlanning = false; Execution->Status = EGuLiTaskStatus::Running;
				if (bReplacement) { State->Active = MoveTemp(State->PendingMove); State->PendingMove.Reset(); }
			}
			else
			{
				const FString Reason = FString::Printf(TEXT("PlanningResult:%d/Ack:%d"), int(Result), int(Ack.Result));
				LogMoveFailure(*State, *Execution, *Reason); Execution.Reset();
			}
		}
		Planning.RemoveAt(I);
	}
}
bool UGuLiUnitTaskSubsystem::MayAdvance(FGuLiSoldierId Unit) const
{
	const auto* State = States.Find(FGuLiTaskUnitId::Soldier(Unit));
	if (!State || State->bStopped || State->bCancelPending || !State->Active.IsSet() || !State->Active->bAutomatic || State->Active->WorkSerial) return false;
	const auto* Definition = Catalog->Find(State->Active->Command.SpecialTaskId);
	return Definition && Definition->Tag.GetTagName() == TEXT("Task.Special.StrongholdAdvance");
}
bool UGuLiUnitTaskSubsystem::WantsAdvanceYield(FGuLiSoldierId Unit) const
{ const auto* State = States.Find(FGuLiTaskUnitId::Soldier(Unit)); return State && !State->Queue.IsEmpty(); }
void UGuLiUnitTaskSubsystem::NotifyAdvanceStage(FGuLiSoldierId Unit)
{ if (auto* State = States.Find(FGuLiTaskUnitId::Soldier(Unit)); State && MayAdvance(Unit) && WantsAdvanceYield(Unit)) ++State->Active->WorkSerial; }
void UGuLiUnitTaskSubsystem::UpdateAdvanceTarget(FGuLiSoldierId Unit, int32 Territory, const FVector& Target)
{
	if (auto* State = States.Find(FGuLiTaskUnitId::Soldier(Unit)); State && MayAdvance(Unit))
	{
		State->Active->CurrentObjective = Territory; State->Active->Command.Target = Target;
		State->Active->bWaitingForTarget = Territory == INDEX_NONE;
	}
}

bool UGuLiUnitTaskSubsystem::BuildContextCommand(const FGuLiCommanderSelectionState& Selection, FGuLiUnitTaskCommand& Command) const
{
	if (Command.Kind != EGuLiUnitTaskKind::Move || Command.bGroundMoveOnly || !Catalog->IsValidCatalog()) return false;
	const FGameplayTag Construction = FGameplayTag::RequestGameplayTag(TEXT("Task.Special.Construction"));
	bool bHasBuilder = false;
	for (auto Id : Members(Selection)) if (const auto* State = States.Find(Id))
		if (Catalog->Find(Construction, State->Context.UnitTypeId)) { bHasBuilder = true; break; }
	if (!bHasBuilder) return false;
	TArray<UGuLiBuildingLifecycleComponent*> Buildings; GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Query(Buildings);
	for (const auto* Building : Buildings)
		if (Building->GetState().Phase == EGuLiBuildingPhase::UnderConstruction
			&& FVector::DistSquared2D(Building->GetGroundLocation(), Command.Target) <= FMath::Square(Building->GetDefinition().CollisionExtent.Size2D()))
		{
			for (auto Id : Members(Selection)) if (const auto* State = States.Find(Id))
				if (const auto* Definition = Catalog->Find(Construction, State->Context.UnitTypeId))
				{ Command.Kind = EGuLiUnitTaskKind::Special; Command.SpecialTaskId = Definition->Id; Command.BuildingId = Building->GetState().InstanceId; return true; }
		}
	return false;
}
void UGuLiUnitTaskSubsystem::BuildSummary(const FGuLiCommanderSelectionState& Selection, TArray<FGuLiUnitTaskSummary>& Out) const
{
	Out.Reset(); TMap<FString,int32> Groups;
	for (auto Id : Members(Selection))
	{
		const auto* State = States.Find(Id); if (!State) continue;
		FGuLiUnitTaskSummary Summary; Summary.UnitCount = 1; Summary.bStopped = State->bStopped;
		Summary.ManualTaskCount = State->ManualTaskCount();
		Summary.bWaitingSafeExit = State->bCancelPending; Summary.Error = State->Error;
		FString Key = FString::Printf(TEXT("%d/%d/%s"), State->bStopped, State->bCancelPending, *State->Error);
		for (const auto& Grant : State->Grants) if (!Grant.bConsumed)
			if (const auto* Definition = Catalog->Find(Grant.TaskId); Definition && Definition->bAutoActivate)
			{
				Summary.RecoverableTasks.Add(Definition->DisplayName);
				Key += FString::Printf(TEXT("/grant:%d"), Grant.TaskId);
			}
		auto Add = [&](const FGuLiUnitTaskCommand& Command, EGuLiTaskStatus Status, bool Automatic)
		{
			auto& View = Summary.Tasks.AddDefaulted_GetRef(); View.Command = Command; View.Status = Status; View.bAutomatic = Automatic;
			const auto* Definition = Catalog->Find(Command.SpecialTaskId); View.DisplayName = Definition ? Definition->DisplayName : BasicName(Command.Kind);
			View.Location = Command.Target;
			View.bHasLocation = Command.Kind == EGuLiUnitTaskKind::Move;
			const auto* Resources = GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
			if (Command.Kind == EGuLiUnitTaskKind::Transit && Resources)
				View.bHasLocation = Resources->FindTerritoryIndexById(Command.TerritoryId) != INDEX_NONE;
			if (Command.BuildingId)
				if (const auto* Building = GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Find(Command.BuildingId))
				{ View.Location = Building->GetGroundLocation(); View.bHasLocation = Building->GetState().Phase != EGuLiBuildingPhase::Destroyed; }
			uint16 Cluster = Command.ClusterId;
			if (Automatic && Definition && Definition->Tag.GetTagName() == TEXT("Task.Special.Mining"))
				if (const auto* Miner = Cast<AGuLiMiningVehiclePawn>(State->Context.Pawn.Get())) Cluster = uint16(FMath::Max(0, Miner->GetTargetClusterId()));
			if (Cluster && Resources && Resources->GetMapDefinition())
				if (const auto* Ore = Resources->GetMapDefinition()->FindCluster(Cluster))
				{ View.Location = Ore->Center; View.bHasLocation = true; }
			if (Automatic && State->Active.IsSet() && State->Active->CurrentObjective != INDEX_NONE)
				View.bHasLocation = true;
			View.bHasLocation &= !FVector(View.Location).ContainsNaN();
			Key += FString::Printf(TEXT("|%d,%d,%d,%d,%u,%u,%s,%s"), int(Command.Kind), Command.SpecialTaskId, int(Status), Automatic,
				Command.BuildingId, Command.ClusterId, *Command.TerritoryId.ToString(), *FVector(Command.Target).ToString());
			Key += FString::Printf(TEXT("/location:%d,%s"), View.bHasLocation, *FVector(View.Location).ToString());
		};
		if (State->PendingMove.IsSet()) Add(State->PendingMove->Command, EGuLiTaskStatus::Waiting, false);
		else if (State->Active.IsSet()) Add(State->Active->Command, State->Active->Status, State->Active->bAutomatic);
		for (const auto& Command : State->Queue) Add(Command, EGuLiTaskStatus::Waiting, false);
		if (const auto* Index = Groups.Find(Key)) ++Out[*Index].UnitCount;
		else { Groups.Add(Key, Out.Num()); Out.Add(MoveTemp(Summary)); }
	}
}
