#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Gameplay/Data/GuLiGameText.h"
#include "Commander/Orders/GuLiUnitTaskSettings.h"
#include "Commander/Behavior/GuLiCommanderBehaviorSchema.h"
#include "Commander/Behavior/GuLiCommanderAbilityBridge.h"
#include "Commander/Behavior/GuLiCommanderStateTreeComponent.h"
#include "Commander/Behavior/GuLiCommanderStateTreeNodes.h"
#include "Commander/Behavior/GuLiCommanderMassStateTreeProcessor.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "MassEntitySubsystem.h"
#include "MassStateTreeSubsystem.h"
#include "MassExecutor.h"
#include "MassEntityUtils.h"
#include "MassEntityManager.h"
#include "Commander/Mass/Navigation/GuLiNavigationWorkBudget.h"
#include "Commander/Mass/Navigation/GuLiSharedMoveRoutes.h"
#include "Commander/Network/GuLiMoveLatency.h"
#include "MassProcessingContext.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Building/GuLiConstructionWorkComponent.h"
#include "Gameplay/Stronghold/GuLiArmyAdvanceSubsystem.h"
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
	Super::Initialize(Collection); Collection.InitializeDependency<UGuLiUnitDataSubsystem>();
	Collection.InitializeDependency<UMassEntitySubsystem>();
	Collection.InitializeDependency<UMassStateTreeSubsystem>();
	Collection.InitializeDependency<UGuLiBuildingRegistrySubsystem>();
	MassTreeProcessor = NewObject<UGuLiCommanderMassStateTreeProcessor>(this);
	MassTreeProcessor->CallInitialize(GetWorld(), GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager().AsShared());
	auto* Registry = GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>();
	Registry->OnSpawned.AddUObject(this, &ThisClass::WakeAutomaticBuilders);
	Registry->OnCompleted.AddUObject(this, &ThisClass::WakeAutomaticBuilders);
	Registry->OnDestroyed.AddUObject(this, &ThisClass::WakeAutomaticBuilders);
}
void UGuLiUnitTaskSubsystem::Deinitialize()
{
	if (auto* Registry = GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>())
	{ Registry->OnSpawned.RemoveAll(this); Registry->OnCompleted.RemoveAll(this); Registry->OnDestroyed.RemoveAll(this); }
	for (auto& Pair : States) if (auto* Tree = Pair.Value.ActorTree.Get()) Tree->StopLogic(TEXT("World teardown"));
	Planning.Reset(); BehaviorRequests.Reset(); States.Reset(); InitializedUnits.Reset(); Super::Deinitialize();
}
void UGuLiUnitTaskSubsystem::WakeAutomaticBuilders(UGuLiBuildingLifecycleComponent&)
{
	for (auto& Pair : States) if (Pair.Key.bActor) Pair.Value.NextAutomaticTime = 0;
}
TStatId UGuLiUnitTaskSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiUnitTaskSubsystem, STATGROUP_Tickables); }
uint32 UGuLiUnitTaskSubsystem::AllocateExecutionId() { if (!NextExecutionId) ++NextExecutionId; return NextExecutionId++; }
void UGuLiUnitTaskSubsystem::InitializeBehavior(FGuLiUnitTaskState& State)
{
	const auto* Policy = GuLiCommanderBehavior::FindPolicy(*GetWorld(), State.Context.UnitTypeId);
	if (!Policy) { State.Error = TEXT("Unit has no valid Commander StateTree."); return; }
	const bool bAlreadyInitialized = InitializedUnits.Contains(State.Context.Unit);
	InitializedUnits.Add(State.Context.Unit);
	State.AutomaticBehaviors.Add({Policy->GetWireId(), Policy->Lifetime, bAlreadyInitialized && Policy->Lifetime == EGuLiTaskLifetime::InitialOnce});
}
void UGuLiUnitTaskSubsystem::RegisterActor(APawn& Pawn)
{
	const auto* Vehicle = Cast<IGuLiEngineeringVehicle>(&Pawn);
	if (!Vehicle || !Pawn.HasAuthority() || !Vehicle->GetStableActorId().IsValid()) return;
	const auto Id = FGuLiTaskUnitId::Actor(Vehicle->GetStableActorId());
	if (States.Contains(Id)) return;
	auto& State = States.Add(Id); State.Context.Unit = Id; State.Context.Pawn = &Pawn;
	State.Context.Team = Vehicle->GetTeam(); State.Context.UnitTypeId = uint16(Vehicle->GetUnitTypeId());
	State.Context.Location = Pawn.GetActorLocation(); InitializeBehavior(State);
	const auto* Definition = GetWorld()->GetSubsystem<UGuLiUnitDataSubsystem>()->FindDefinition(State.Context.UnitTypeId);
	if (Definition && Definition->StateTreeAsset)
	{
		auto* Tree = Pawn.FindComponentByClass<UGuLiCommanderStateTreeComponent>();
		if (!Tree)
		{
			Tree = NewObject<UGuLiCommanderStateTreeComponent>(&Pawn);
			Pawn.AddInstanceComponent(Tree);
			Tree->Configure(Id, *Definition->StateTreeAsset);
			Tree->RegisterComponent();
		}
		else Tree->Configure(Id, *Definition->StateTreeAsset);
		State.ActorTree = Tree;
	}
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
		InitializeBehavior(State); States.Add(Id, MoveTemp(State));
	}
}
void UGuLiUnitTaskSubsystem::UnregisterActor(FGuLiControllableActorId Id)
{
	const auto Unit = FGuLiTaskUnitId::Actor(Id);
	if (auto* State = States.Find(Unit))
	{
		DiscardPendingMove(*State); ++State->Version; State->Queue.Reset(); State->ConsumeInitialBehaviors();
		State->bStopped = State->bUnregisterPending = true;
		State->bCancelPending = !Cancel(*State);
		if (State->bCancelPending) return; // The same tree must finish factory/transport safe exit before release.
		if (auto* Tree = State->ActorTree.Get()) Tree->StopLogic(TEXT("Unit unregistered"));
	}
	States.Remove(Unit);
}
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
		++State.Version; State.Queue.Reset(); State.ConsumeInitialBehaviors();
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
	else if (auto* Pawn = State.Context.Pawn.Get())
		if (auto* Travel = Pawn->FindComponentByClass<UGuLiEngineeringTravelComponent>()) Travel->CancelReplacementMove();
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
		const auto* Definition = GuLiCommanderBehavior::FindPolicy(*GetWorld(), State.Context.UnitTypeId);
		if (!Definition || Definition->GetWireId() != Command.SpecialTaskId) { Error = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.112")); return false; }
		if (Definition->Lifetime == EGuLiTaskLifetime::InitialOnce)
		{
			const auto* Grant = State.AutomaticBehaviors.FindByPredicate([&](const auto& Candidate) { return Candidate.BehaviorId == Definition->GetWireId(); });
			if (!Grant || Grant->bConsumed) { Error = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.113")); return false; }
		}
		return GuLiCommanderAbilities::Validate(Definition->Behavior, *GetWorld(), State.Context, Command, Error);
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
bool UGuLiUnitTaskSubsystem::AdmitCommand(FGuLiUnitTaskState& State, const FGuLiUnitTaskCommand& Command, FString& Error)
{
	if (State.bUnregisterPending) { Error = TEXT("Unit is unregistering."); return false; }
	if (State.Active.IsSet() && IsUnfinishedMove(*State.Active) && State.Active->bStarted && !State.Active->bPlanning)
		State.Active->Status = Poll(State);
	const bool bMoveReplacement = Command.Kind == EGuLiUnitTaskKind::Move
		&& Command.Disposition == EGuLiTaskDisposition::Replace;
	if (!Validate(State, Command, Error))
	{
		if (bMoveReplacement && (State.PendingMove.IsSet()
			|| (State.Active.IsSet() && IsUnfinishedMove(*State.Active))))
		{
			FGuLiTaskExecution Failed; Failed.Command = Command;
			LogMoveFailure(State, Failed, *Error);
			DiscardPendingMove(State); State.Queue.Reset(); State.Error.Reset();
			return true;
		}
		State.Error = Error; return false;
	}
	if (ReuseMove(State, Command, GetDefault<UGuLiUnitTaskSettings>()->MoveReuseDistanceCentimeters))
	{
		return true;
	}
	if (bMoveReplacement && !State.bStopped && !State.bCancelPending
		&& (State.PendingMove.IsSet() || (State.Active.IsSet() && IsUnfinishedMove(*State.Active))))
	{
		// Retire only scratch planning. The committed route remains live until success.
		if (!State.Context.Unit.bActor)
		{
			const FGuLiSoldierId Id(State.Context.Unit.Id);
			GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->InvalidateTaskSoldierPlans(MakeArrayView(&Id, 1));
		}
		FGuLiTaskExecution Replacement; Replacement.Command = Command;
		Replacement.Version = ++State.Version; Replacement.ExecutionId = AllocateExecutionId();
		State.PendingMove.Emplace(MoveTemp(Replacement));
		State.Queue.Reset(); State.ConsumeInitialBehaviors(); State.Error.Reset();
		if (State.Active.IsSet()) State.Active->bYieldRequested = false;
		return true;
	}
	if (Command.Disposition != EGuLiTaskDisposition::Append) DiscardPendingMove(State);
	if (!Admit(State, Command, FMath::Clamp(GetDefault<UGuLiUnitTaskSettings>()->MaximumManualTasks, 1, 32)))
	{ Error = State.Error = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.121")); return false; }
	if (Command.Disposition != EGuLiTaskDisposition::Append)
	{
		State.bCancelPending = !Cancel(State);
		if (!State.bCancelPending) State.Active.Reset();
	}
	return true;
}

bool UGuLiUnitTaskSubsystem::SubmitActorCommand(APawn& Pawn, const FGuLiUnitTaskCommand& Command)
{
	if (!Pawn.HasAuthority() || !Command.IsWellFormed()) return false;
	const auto* Vehicle = Cast<IGuLiEngineeringVehicle>(&Pawn);
	if (!Vehicle) return false;
	auto* State = States.Find(FGuLiTaskUnitId::Actor(Vehicle->GetStableActorId()));
	if (!State || State->Context.Pawn != &Pawn || !RefreshContext(*State)) return false;
	FString Error;
	return AdmitCommand(*State, Command, Error);
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
	FGuLiUnitTaskCommand AuthorityCommand=Command;
	AuthorityCommand.SharedMoveIntent=Command.Kind==EGuLiUnitTaskKind::Move
		? GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->CreateSharedMoveIntent(Command.Target,Command.CommandId)
		: TSharedPtr<FGuLiSharedMoveIntent>();
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
		if (!AdmitCommand(*State, AuthorityCommand, Error)) { ++Rejected; Message = Error; continue; }
		State->Owner = &Owner; ++Accepted;
		if (!Unit.bActor && AuthorityCommand.SharedMoveIntent) AuthorityCommand.SharedMoveIntent->UnassignedMembers.Add(Unit.Id);
		if (!Unit.bActor) QueueMoveAdmission(Unit);
		if (AcceptedUnits) AcceptedUnits->Add(Unit);
	}
	if (!Accepted && !Rejected) Message = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.122"));
	return Accepted > 0;
}
bool UGuLiUnitTaskSubsystem::StartVehicleMove(FGuLiUnitTaskState& State, FGuLiTaskExecution& Task)
{
	auto* Pawn = State.Context.Pawn.Get();
	auto* Travel = Pawn ? Pawn->FindComponentByClass<UGuLiEngineeringTravelComponent>() : nullptr;
	FGuLiPreparedGroundMove Prepared;
	Task.bPlanning = false;
	if (!Travel) return false;
	const auto Preparation = Travel->PrepareReplacementMove(Task.ExecutionId, Task.Command.Target, 100, Prepared);
	Task.bPlanning = Preparation == EGuLiEngineeringPathPreparation::Pending;
	if (Preparation != EGuLiEngineeringPathPreparation::Ready) return false;
	if (auto* Miner = Cast<AGuLiMiningVehiclePawn>(Pawn))
	{
		FGuLiMiningCommand Command; Command.RequestId = Task.ExecutionId; Command.SelectionRevision = 1;
		Command.Type = EGuLiMiningOrderType::Move; Command.Target = Task.Command.Target;
		return Miner->StartPreparedManagedMove(Command, Prepared);
	}
	Travel->CancelGroundMove();
	return Travel->CommitGroundMove(Prepared);
}
bool UGuLiUnitTaskSubsystem::Cancel(FGuLiUnitTaskState& State)
{
	if (State.Active.IsSet() && State.Active->Command.Kind == EGuLiUnitTaskKind::Special)
		if (const auto* Definition = GuLiCommanderBehavior::FindPolicy(*GetWorld(), State.Context.UnitTypeId))
			return GuLiCommanderAbilities::Cancel(Definition->Behavior, *GetWorld(), State.Context, *State.Active);
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
		const auto* Definition = GuLiCommanderBehavior::FindPolicy(*GetWorld(), State.Context.UnitTypeId);
		return Definition ? GuLiCommanderAbilities::Start(Definition->Behavior, *GetWorld(), State.Context, Task) : EGuLiTaskStatus::Failed;
	}
	auto* Pawn = State.Context.Pawn.Get();
	if (!Pawn) return EGuLiTaskStatus::Waiting; // Mass moves enter the existing batched planner below.
	auto* Travel = Pawn->FindComponentByClass<UGuLiEngineeringTravelComponent>();
	if (UGuLiExternalUnitControlComponent::AreActorActionsLocked(Pawn)) return EGuLiTaskStatus::Waiting;
	if (Task.Command.Kind == EGuLiUnitTaskKind::Move)
		return StartVehicleMove(State, Task) ? EGuLiTaskStatus::Running : Task.bPlanning ? EGuLiTaskStatus::Waiting : EGuLiTaskStatus::Failed;
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
		const auto* Definition = GuLiCommanderBehavior::FindPolicy(*GetWorld(), State.Context.UnitTypeId);
		return Definition ? GuLiCommanderAbilities::Poll(Definition->Behavior, *GetWorld(), State.Context, Task) : EGuLiTaskStatus::Failed;
	}
	if (auto* Pawn = State.Context.Pawn.Get())
	{
		auto* Travel = Pawn->FindComponentByClass<UGuLiEngineeringTravelComponent>();
		if (Travel->IsRouting() || Travel->IsWaitingForPath() || UGuLiExternalUnitControlComponent::AreActorActionsLocked(Pawn)) return EGuLiTaskStatus::Running;
		if (Task.Command.Kind == EGuLiUnitTaskKind::Transit && Task.WorkSerial == 0)
		{
			++Task.WorkSerial;
			if (!Travel->BeginMove(Task.Command.Target, 100)) { Task.Error = GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.124")); return EGuLiTaskStatus::Failed; }
			return EGuLiTaskStatus::Running;
		}
		if (Task.Command.Kind != EGuLiUnitTaskKind::Transit)
			if (auto* Miner = Cast<AGuLiMiningVehiclePawn>(Pawn))
				return !Miner->IsManagedTaskComplete() ? EGuLiTaskStatus::Running : Miner->DidManagedTaskFail() ? EGuLiTaskStatus::Failed : EGuLiTaskStatus::Completed;
		if (Travel->GetMoveStatus() == EGuLiEngineeringMoveStatus::Moving) return EGuLiTaskStatus::Running;
		return Travel->GetMoveStatus() == EGuLiEngineeringMoveStatus::Arrived ? EGuLiTaskStatus::Completed : EGuLiTaskStatus::Failed;
	}
	if (Task.bPlanning) return EGuLiTaskStatus::Running;
	FGuLiSoldierNavigationDebug Nav;
	if (!GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->TryGetSoldierNavigationDebug(FGuLiSoldierId(State.Context.Unit.Id), Nav)) return EGuLiTaskStatus::Failed;
	if (Nav.ActiveOrderId) return EGuLiTaskStatus::Running;
	return Nav.State == EGuLiSoldierNavigationState::Arrived ? EGuLiTaskStatus::Completed : EGuLiTaskStatus::Failed;
}
EGuLiCommanderWorkPhase UGuLiUnitTaskSubsystem::GetWorkPhase(FGuLiTaskUnitId Unit) const
{
	const auto* State = States.Find(Unit); if (!State) return EGuLiCommanderWorkPhase::Any;
	if (!Unit.bActor) return GetWorld()->GetSubsystem<UGuLiArmyAdvanceSubsystem>()->GetBehaviorPhase(FGuLiSoldierId(Unit.Id));
	const auto* Pawn = State->Context.Pawn.Get();
	if (const auto* Miner = Cast<AGuLiMiningVehiclePawn>(Pawn)) return Miner->GetBehaviorPhase();
	const auto* Work = Pawn ? Pawn->FindComponentByClass<UGuLiConstructionWorkComponent>() : nullptr;
	return Work ? Work->GetBehaviorPhase() : EGuLiCommanderWorkPhase::Any;
}

EGuLiCommanderWorkResult UGuLiUnitTaskSubsystem::GetWorkResult(FGuLiTaskUnitId Unit) const
{
	const auto* State = States.Find(Unit); if (!State) return EGuLiCommanderWorkResult::None;
	if (!Unit.bActor) return GetWorld()->GetSubsystem<UGuLiArmyAdvanceSubsystem>()->GetBehaviorResult(FGuLiSoldierId(Unit.Id));
	const auto* Pawn = State->Context.Pawn.Get();
	if (const auto* Miner = Cast<AGuLiMiningVehiclePawn>(Pawn)) return Miner->GetBehaviorResult();
	const auto* Work = Pawn ? Pawn->FindComponentByClass<UGuLiConstructionWorkComponent>() : nullptr;
	return Work ? Work->GetBehaviorResult() : EGuLiCommanderWorkResult::None;
}

uint32 UGuLiUnitTaskSubsystem::GetBehaviorFacts(FGuLiTaskUnitId Unit) const
{
	using namespace GuLiCommanderBehaviorFacts;
	const auto* State = States.Find(Unit); if (!State) return 0;
	uint32 Facts = 0;
	if (State->bCancelPending) Facts |= CancelPending;
	if (State->bStopped) Facts |= Stopped;
	if (State->PendingMove.IsSet()) Facts |= PendingMove;
	if (!State->Queue.IsEmpty()) Facts |= Queued;
	if (State->Active.IsSet())
	{
		const auto& Task = *State->Active;
		Facts |= Active;
		if (Task.bAutomatic) Facts |= Automatic;
		if (Task.bStarted) Facts |= Started;
		if (Task.bWaitingForTarget || Task.Status == EGuLiTaskStatus::Waiting) Facts |= WaitingTarget;
		switch (Task.Command.Kind)
		{
		case EGuLiUnitTaskKind::Move: Facts |= GuLiCommanderBehaviorFacts::Move; break;
		case EGuLiUnitTaskKind::ReturnToFactory: Facts |= Return; break;
		case EGuLiUnitTaskKind::Transit: Facts |= Transit; break;
		case EGuLiUnitTaskKind::Special:
			if (Task.Command.SpecialTaskId == 1) Facts |= Mining;
			else if (Task.Command.SpecialTaskId == 2) Facts |= Construction;
			else if (Task.Command.SpecialTaskId == 3) Facts |= Advance;
			break;
		}
	}
	if (GetWorld()->GetTimeSeconds() >= State->NextAutomaticTime)
		if (const auto* Policy = GuLiCommanderBehavior::FindPolicy(*GetWorld(), State->Context.UnitTypeId); Policy && Policy->bAutoActivate)
			for (const auto& Entry : State->AutomaticBehaviors)
				if (!Entry.bConsumed && Entry.BehaviorId == Policy->GetWireId()) { Facts |= AutomaticReady; break; }
	if (const auto* Miner = Cast<AGuLiMiningVehiclePawn>(State->Context.Pawn.Get()))
	{
		if (Miner->GetCargo().Blue + Miner->GetCargo().Red > 0) Facts |= Cargo;
		if (Miner->IsBehaviorRetryReady()) Facts |= RetryReady;
		if (Miner->IsManagedTaskComplete()) Facts |= WorkComplete;
		if (Miner->NeedsReturnBeforeMining()) Facts |= ReturnFirst;
	}
	if (const auto* Pawn = State->Context.Pawn.Get())
		if (UGuLiExternalUnitControlComponent::AreActorActionsLocked(Pawn)
			|| Pawn->FindComponentByClass<UGuLiEngineeringTravelComponent>()->IsRouting()) Facts |= Suspended;
	return Facts;
}

bool UGuLiUnitTaskSubsystem::NeedsBehaviorStep(FGuLiTaskUnitId Unit) const
{ const auto* State = States.Find(Unit); return State && !State->bBehaviorStepDone; }

void UGuLiUnitTaskSubsystem::RequestBehaviorStep(FGuLiTaskUnitId Unit, EGuLiCommanderBehaviorStep Step)
{
	check(IsInGameThread());
	auto* State = States.Find(Unit);
	if (!State || State->bBehaviorStepDone || State->LastBehaviorRequestRound == BehaviorRound) return;
	State->LastBehaviorRequestRound = BehaviorRound;
	BehaviorRequests.Add({Unit, Step, State->Version});
}

void UGuLiUnitTaskSubsystem::ReportBehaviorError(FGuLiTaskUnitId Unit, const FString& Error)
{
	if (auto* State = States.Find(Unit))
	{
		State->bBehaviorStepDone = true;
		if (State->Error != Error) UE_LOG(LogGuLiMoveOrders, Error, TEXT("StateTree unit=%u: %s"), Unit.Id, *Error);
		State->Error = Error;
	}
}

void UGuLiUnitTaskSubsystem::PollPendingMove(FGuLiUnitTaskState& State)
{
	if (!State.PendingMove.IsSet()) return;
	// The committed route keeps running while its replacement is prepared.
	if (State.Active.IsSet() && State.Active->bStarted && !State.Active->bPlanning)
	{
		State.Active->Status = Poll(State);
		if (!IsUnfinishedMove(*State.Active)) State.Active.Reset();
	}
	if (!State.Context.Unit.bActor) return;
	if (UGuLiExternalUnitControlComponent::AreActorActionsLocked(State.Context.Pawn.Get()))
	{ DiscardPendingMove(State); return; }
	if (StartVehicleMove(State, *State.PendingMove))
	{
		State.PendingMove->bStarted = true; State.PendingMove->Status = EGuLiTaskStatus::Running;
		State.Active = MoveTemp(State.PendingMove); State.PendingMove.Reset();
	}
	else if (!State.PendingMove->bPlanning)
	{ LogMoveFailure(State, *State.PendingMove, TEXT("VehiclePath")); DiscardPendingMove(State); }
}

bool UGuLiUnitTaskSubsystem::RunActiveTask(FGuLiUnitTaskState& State, double Now)
{
	if (!State.Active.IsSet()) return true;
	auto& Task = *State.Active;
	Task.Status = Task.bStarted ? Poll(State) : Start(State);
	if (Task.Status == EGuLiTaskStatus::Waiting) return false;
	Task.bStarted = true;
	if (Task.Status == EGuLiTaskStatus::Running) return false;
	if (Task.Status == EGuLiTaskStatus::WorkUnitComplete && !Task.bAutomatic && State.Queue.IsEmpty()
		&& Task.Command.Kind == EGuLiUnitTaskKind::Special && Task.Command.SpecialTaskId == int32(EGuLiCommanderBehavior::Mining))
	{
		Task.bStarted = false; Task.ExecutionId = AllocateExecutionId(); Task.Status = EGuLiTaskStatus::Waiting; return false;
	}
	if (Task.Status == EGuLiTaskStatus::Failed)
	{
		if (Task.Command.Kind == EGuLiUnitTaskKind::Move) LogMoveFailure(State, Task, TEXT("Execution"));
		else State.Error = Task.Error.IsEmpty() ? GuLiGameText::Text(TEXT("UI.UnitTaskSubsystem.125")) : Task.Error;
	}
	if (Task.bAutomatic)
		for (auto& Entry : State.AutomaticBehaviors) if (Entry.BehaviorId == Task.Command.SpecialTaskId) Entry.ConsumeIfInitial();
	if (!Cancel(State))
    {
        State.bCancelPending = true;
        Task.Status = EGuLiTaskStatus::WaitingSafeExit;
        return false;
    }
    State.Active.Reset();
	State.NextAutomaticTime = Now + (State.Context.Unit.bActor ? .2f : GetDefault<UGuLiUnitTaskSettings>()->AutomaticRetrySeconds);
	return true;
}

void UGuLiUnitTaskSubsystem::TakeManualTask(FGuLiUnitTaskState& State)
{
	if (State.Queue.IsEmpty()) return;
	State.ConsumeInitialBehaviors();
	FGuLiTaskExecution Task; Task.Command = State.Queue[0]; State.Queue.RemoveAt(0);
	Task.Version = ++State.Version; Task.ExecutionId = AllocateExecutionId(); State.Active.Emplace(MoveTemp(Task));
	if (!State.Context.Unit.bActor && !bPumpingAdmissions) QueueMoveAdmission(State.Context.Unit);
}

void UGuLiUnitTaskSubsystem::TakeAutomaticTask(FGuLiUnitTaskState& State, double Now)
{
	State.NextAutomaticTime = Now + FMath::Max(.1f, GetDefault<UGuLiUnitTaskSettings>()->AutomaticRetrySeconds);
	const auto* Policy = GuLiCommanderBehavior::FindPolicy(*GetWorld(), State.Context.UnitTypeId);
	if (!Policy || !Policy->bAutoActivate) return;
	if (State.Context.Unit.bActor) State.NextAutomaticTime=Now+.2;
	for (const auto& Entry : State.AutomaticBehaviors)
	{
		if (Entry.bConsumed || Entry.BehaviorId != Policy->GetWireId()) continue;
		FGuLiTaskExecution Task; Task.bAutomatic = true; Task.Command.Kind = EGuLiUnitTaskKind::Special;
		Task.Command.SpecialTaskId = Policy->GetWireId(); Task.Command.SelectionRevision = 1;
		if (!GuLiCommanderAbilities::BuildAutomatic(Policy->Behavior, *GetWorld(), State.Context, Task.Command)) continue;
		Task.ExecutionId = AllocateExecutionId(); Task.Command.CommandId = Task.ExecutionId; Task.Version = ++State.Version;
		State.Active.Emplace(MoveTemp(Task)); break;
	}
}

bool UGuLiUnitTaskSubsystem::RunWorkAction(FGuLiUnitTaskState& State, EGuLiCommanderBehaviorStep Step, double Now)
{
	using Action = EGuLiMiningBehaviorAction;
	using Operation = EGuLiCommanderBehaviorStep;
	const auto BeforePhase = GetWorkPhase(State.Context.Unit);
	const auto BeforeResult = GetWorkResult(State.Context.Unit);
	auto* Pawn = State.Context.Pawn.Get();
	auto* Miner = Cast<AGuLiMiningVehiclePawn>(Pawn);
	auto* Work = Pawn ? Pawn->FindComponentByClass<UGuLiConstructionWorkComponent>() : nullptr;
	auto* Advance = GetWorld()->GetSubsystem<UGuLiArmyAdvanceSubsystem>();
	const FGuLiSoldierId Soldier(State.Context.Unit.Id);
	const uint16 Cluster = State.Active.IsSet() ? State.Active->Command.ClusterId : 0;
	auto Mine = [&](Action Requested) { if (Miner) Miner->ExecuteBehaviorAction(Requested, Cluster); };
	switch (Step)
	{
	case Operation::MiningSelect: Mine(Action::SelectTarget); break;
	case Operation::MiningMove: Mine(Action::MoveToTarget); break;
	case Operation::MiningExtract: Mine(Action::Extract); break;
	case Operation::MiningFactory: Mine(Action::SelectFactory); break;
	case Operation::MiningReturn: Mine(Action::ReturnToFactory); break;
	case Operation::MiningUnload: Mine(Action::Unload); break;
	case Operation::MiningFinish: Mine(Action::FinishCycle); break;
	case Operation::MiningRetry: Mine(Action::Retry); break;
	case Operation::MiningFail: Mine(Action::Fail); break;
	case Operation::MiningComplete: Mine(Action::Complete); break;
	case Operation::MiningReposition: Mine(Action::Reposition); break;
	case Operation::ConstructionReserve: if (Work) Work->SelectPosition(); break;
	case Operation::ConstructionRetry: if (Work) Work->RetryPosition(); break;
	case Operation::ConstructionMove: if (Work) Work->BeginBehaviorMove(); break;
	case Operation::ConstructionWork: if (Work) Work->BeginBehaviorConstruction(); break;
	case Operation::AdvanceSelect: if (Advance) Advance->SelectBehaviorTarget(Soldier); break;
	case Operation::AdvanceMove: if (Advance) Advance->MoveToBehaviorTarget(Soldier); break;
	case Operation::AdvanceCapture: if (Advance) Advance->WaitForBehaviorCapture(Soldier); break;
	case Operation::AdvanceComplete: if (Advance) Advance->CompleteBehaviorStage(Soldier); break;
	case Operation::AdvanceReject: if (Advance) Advance->RejectBehaviorTarget(Soldier); break;
	case Operation::AdvanceWait: if (Advance) Advance->WaitForBehaviorTarget(Soldier); break;
	default: break;
	}
	if (State.bCancelPending)
	{
		if (!Cancel(State)) { if (State.Active.IsSet()) State.Active->Status = EGuLiTaskStatus::WaitingSafeExit; return false; }
		State.bCancelPending = false; State.Active.Reset(); return true;
	}
	if (RunActiveTask(State, Now)) return true;
	const auto AfterPhase = GetWorkPhase(State.Context.Unit);
	const auto AfterResult = GetWorkResult(State.Context.Unit);
	return (AfterPhase != BeforePhase || AfterResult != BeforeResult)
		&& AfterResult != EGuLiCommanderWorkResult::Running && AfterResult != EGuLiCommanderWorkResult::None;
}

void UGuLiUnitTaskSubsystem::CommitBehaviorRequests()
{
	check(IsInGameThread());
	const double Now = GetWorld()->GetTimeSeconds();
	TArray<FBehaviorRequest> Requests = MoveTemp(BehaviorRequests);
	BehaviorRequests.Reset();
	for (const auto& Request : Requests)
	{
		auto* State = States.Find(Request.Unit);
		if (!State || State->bBehaviorStepDone || State->Version != Request.Version) continue;
		State->bBehaviorStepDone = true;
		switch (Request.Step)
		{
		case EGuLiCommanderBehaviorStep::CancelPending:
			if (!Cancel(*State)) { if (State->Active.IsSet()) State->Active->Status = EGuLiTaskStatus::WaitingSafeExit; }
			else { State->bCancelPending = false; State->Active.Reset(); State->bBehaviorStepDone = false; }
			break;
		case EGuLiCommanderBehaviorStep::ReplaceMove: PollPendingMove(*State); break;
		case EGuLiCommanderBehaviorStep::RunTask: State->bBehaviorStepDone = !RunActiveTask(*State, Now); break;
		case EGuLiCommanderBehaviorStep::TakeManual: TakeManualTask(*State); break;
		case EGuLiCommanderBehaviorStep::TakeAutomatic: TakeAutomaticTask(*State, Now); break;
		case EGuLiCommanderBehaviorStep::Wait: break;
		default: State->bBehaviorStepDone = !RunWorkAction(*State, Request.Step, Now); break;
		}
	}
}

void UGuLiUnitTaskSubsystem::Tick(float DeltaTime)
{
	Accumulator += DeltaTime; if (Accumulator < .1) return;
	const float BehaviorDelta = float(Accumulator); Accumulator = 0;
	TickMoveBatches();
	for (auto It = States.CreateIterator(); It; ++It)
	{
		if (!RefreshContext(It.Value()))
		{
			DiscardPendingMove(It.Value());
			if (auto* Tree = It.Value().ActorTree.Get()) Tree->StopLogic(TEXT("Unit unavailable"));
			It.RemoveCurrent(); continue;
		}
		It.Value().bBehaviorStepDone = false;
	}
	// Drain immediate transitions in the same 10 Hz step: cancel -> select, or finish -> select.
	// The next task still starts on the next step, as before. These zero-time rounds are not extra simulation ticks.
	for (int32 Round = 0; Round < 4; ++Round)
	{
		++BehaviorRound;
		for (auto& Pair : States)
			if (Pair.Key.bActor && !Pair.Value.bBehaviorStepDone)
			{
				if (auto* Tree = Pair.Value.ActorTree.Get()) Tree->AdvanceBehavior(Round == 0 ? BehaviorDelta : 0.f);
				else ReportBehaviorError(Pair.Key, TEXT("Commander Actor StateTree component is unavailable."));
			}
		if (auto* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>(); Mass && MassTreeProcessor)
		{
			UE::Mass::FProcessingContext Context(Mass->GetMutableEntityManager(), Round == 0 ? BehaviorDelta : 0.f);
			UE::Mass::Executor::Run(*MassTreeProcessor, Context);
		}
		CommitBehaviorRequests();
		bool bPending = false;
		for (const auto& Pair : States) bPending |= !Pair.Value.bBehaviorStepDone;
		if (!bPending) break;
	}
	for (auto& Pair : States) if (!Pair.Value.bBehaviorStepDone)
		ReportBehaviorError(Pair.Key, TEXT("Commander StateTree did not produce a bounded operation."));
	for (auto It = States.CreateIterator(); It; ++It)
		if (It.Value().bUnregisterPending && !It.Value().bCancelPending)
		{
			if (auto* Tree = It.Value().ActorTree.Get()) Tree->StopLogic(TEXT("Unit safely unregistered"));
			It.RemoveCurrent();
		}
	// World-frame authority planning consumes the indexed admissions.
}
void UGuLiUnitTaskSubsystem::QueueMoveAdmission(FGuLiTaskUnitId Unit)
{
	const auto* State=States.Find(Unit);
	if (!Unit.bActor && State && State->Owner.IsValid() && !AdmissionQueued.Contains(Unit))
	{
		AdmissionQueued.Add(Unit);
		if (!AdmissionQueues.Contains(State->Owner)) AdmissionOwners.Add(State->Owner);
		AdmissionQueues.FindOrAdd(State->Owner).Units.Add(Unit);
	}
}

void UGuLiUnitTaskSubsystem::PumpMoveAdmissions(FGuLiNavigationWorkBudget& Budget)
{
	if (AdmissionFrame != GFrameCounter) { AdmissionFrame = GFrameCounter; AdmissionRemaining = 128; }
	FGuLiNavigationWorkBudget::FScope Scope(Budget);
	auto* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	auto* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!Authority || !Mass || !MassTreeProcessor) return;
	TGuardValue<bool> PumpGuard(bPumpingAdmissions,true);
	TArray<FGuLiTaskUnitId> Woken;
	TArray<FMassEntityHandle> Entities;
	const int32 Count=FMath::Min3(8,AdmissionRemaining,AdmissionQueued.Num());
	for (int32 N=0; N<Count && Budget.CanWork() && !AdmissionOwners.IsEmpty(); ++N)
	{
		--AdmissionRemaining;
		AdmissionOwnerCursor%=AdmissionOwners.Num();
		const auto Owner=AdmissionOwners[AdmissionOwnerCursor];
		auto& Queue=AdmissionQueues.FindChecked(Owner);
		const auto Unit=Queue.Units[Queue.Cursor++];
		if (Queue.Cursor==Queue.Units.Num()) { AdmissionQueues.Remove(Owner); AdmissionOwners.RemoveAt(AdmissionOwnerCursor); }
		else ++AdmissionOwnerCursor;
		if (!AdmissionQueued.Remove(Unit)) continue;
		auto* State = States.Find(Unit);
		FMassEntityHandle Entity;
		if (!State || !RefreshContext(*State) || !Authority->FindSoldierEntity(FGuLiSoldierId(Unit.Id),Entity)) continue;
		const auto* Waiting=MoveToPlan(*State);
		if (!State->bCancelPending && State->Queue.IsEmpty() && (!Waiting || Waiting->bStarted)) continue;
		State->bBehaviorStepDone = false;
		Woken.Add(Unit); Entities.Add(Entity);
	}
	TArray<FMassArchetypeEntityCollection> Collections;
	UE::Mass::Utils::CreateEntityCollections(Mass->GetEntityManager(), Entities,
		FMassArchetypeEntityCollection::NoDuplicates, Collections);
	for (int32 Round = 0; Round < 4 && !Collections.IsEmpty() && Budget.CanWork(); ++Round)
	{
		++BehaviorRound;
		UE::Mass::FProcessingContext Context(Mass->GetMutableEntityManager(), 0.f);
		UMassProcessor* Processor = MassTreeProcessor;
		UE::Mass::Executor::RunProcessorsView(MakeArrayView(&Processor,1),Context,Collections);
		CommitBehaviorRequests();
		bool Pending = false;
		for (auto Id : Woken) if (auto* State = States.Find(Id)) Pending |= !State->bBehaviorStepDone;
		if (!Pending) break;
	}
	for (auto Id : Woken) if (auto* State = States.Find(Id))
	{
		const auto* Task = MoveToPlan(*State);
		if (!State->bCancelPending && Task && Task->Command.Kind == EGuLiUnitTaskKind::Move && !Task->bStarted)
			ReadyAdmissions.AddUnique(Id);
		else if (State->bCancelPending || !State->bBehaviorStepDone) QueueMoveAdmission(Id);
	}
	StartMoveBatches(Budget);
}

void UGuLiUnitTaskSubsystem::ConsumeMoveProgress()
{
	if (ProgressFrame == GFrameCounter) return;
	ProgressFrame = GFrameCounter;
	TickMoveBatches();
}

void UGuLiUnitTaskSubsystem::StartMoveBatches(FGuLiNavigationWorkBudget& Budget)
{
	auto* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	// This list contains only newly woken requests, never the whole population.
	while (!ReadyAdmissions.IsEmpty() && Budget.CanWork())
	{
		const auto First = ReadyAdmissions[0];
		auto* State = States.Find(First);
		auto* Task = State ? MoveToPlan(*State) : nullptr;
		if (!Task || Task->Command.Kind!=EGuLiUnitTaskKind::Move || Task->bStarted || !State->Owner.IsValid() || State->bStopped || State->bCancelPending)
		{ ReadyAdmissions.RemoveAt(0); continue; }
		FMoveBatch Batch; Batch.Owner = State->Owner; Batch.Id = Task->ExecutionId;
		const uint32 SourceCommand = Task->Command.CommandId;
		const FVector Target = Task->Command.Target;
		const auto Team = State->Context.Team;
		TArray<FGuLiSoldierId> Units;
		for (int32 N = 0; N < ReadyAdmissions.Num() && Units.Num() < 25; )
		{
			auto* Other = States.Find(ReadyAdmissions[N]);
			auto* Next = Other ? MoveToPlan(*Other) : nullptr;
			if (!Next || Next->bStarted || Other->bStopped || Other->bCancelPending)
			{ ReadyAdmissions.RemoveAt(N); continue; }
			if (Other->Owner != Batch.Owner || Next->Command.Kind != EGuLiUnitTaskKind::Move
				|| Next->Command.CommandId != SourceCommand || !FVector(Next->Command.Target).Equals(Target,.01)) { ++N; continue; }
			const auto Id = ReadyAdmissions[N]; Units.Add(FGuLiSoldierId(Id.Id));
			Batch.Versions.Add(Id,Next->Version);
			if (Other->PendingMove.IsSet()) Batch.Replacements.Add(Id);
			ReadyAdmissions.RemoveAt(N);
		}
		FGuLiCommanderSelectionState Frozen;
		if (!Authority->SetExplicitSelection(Team,Units,{},Frozen)) continue;
		Batch.Selection = Frozen;
		FGuLiMoveRequest Request; Request.Target = Target; Request.ClientCommandId = Batch.Id; Request.SelectionRevision = Frozen.SelectionRevision;
		FGuLiCommandAck Ack;
		if (!Authority->BeginMovePlanning(*Batch.Owner,Request,Frozen,Ack,Task->Command.SharedMoveIntent))
		{
			for (const auto& UnitVersion : Batch.Versions) if (auto* Failed = States.Find(UnitVersion.Key))
			{
				if (Ack.Result == EGuLiCommandAckResult::RateLimited) { QueueMoveAdmission(UnitVersion.Key); continue; }
				auto& Execution = Batch.Replacements.Contains(UnitVersion.Key) ? Failed->PendingMove : Failed->Active;
				if (Execution.IsSet() && Execution->Version == UnitVersion.Value)
				{ LogMoveFailure(*Failed,*Execution,TEXT("PlanningAdmission")); Execution.Reset(); }
			}
			continue;
		}
		Authority->SetMovePlanOrigin(*Batch.Owner,Batch.Id,SourceCommand);
		for (const auto& UnitVersion : Batch.Versions)
		{
			auto& Next = *MoveToPlan(States.FindChecked(UnitVersion.Key));
			Next.bStarted = true; Next.bPlanning = true; Next.Status = EGuLiTaskStatus::Running;
		}
		Planning.Add(MoveTemp(Batch));
	}
}
void UGuLiUnitTaskSubsystem::TickMoveBatches()
{
	auto* Authority = GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	for (int32 I = Planning.Num()-1; I >= 0; --I)
	{
		auto& Batch = Planning[I]; FGuLiCommandAck Ack; FGuLiCommanderSelectionState Updated; bool Changed = false;
		FGuLiMovePlanProgress Progress;
		if (Batch.Owner.IsValid() && Authority->ConsumeMovePlanProgress(Authority->FindMovePlan(*Batch.Owner, Batch.Id), Progress))
		{
			auto Apply = [&](FGuLiSoldierId Id, bool bCommitted)
			{
				const auto Key = FGuLiTaskUnitId::Soldier(Id);
				const auto* Version = Batch.Versions.Find(Key);
				auto* State = States.Find(Key);
				if (Version && State && !State->bCancelPending)
				{
					const bool bReplacement = Batch.Replacements.Contains(Key);
					auto& Execution = bReplacement ? State->PendingMove : State->Active;
					if (Execution.IsSet() && Execution->Version == *Version)
					{
						if (bCommitted) { Execution->bPlanning=false; Execution->Status=EGuLiTaskStatus::Running;
							if (bReplacement) { State->Active=MoveTemp(State->PendingMove); State->PendingMove.Reset(); } }
						else { LogMoveFailure(*State,*Execution,TEXT("IncrementalPlanningFailure")); Execution.Reset(); }
					}
				}
				Batch.Versions.Remove(Key); Batch.Replacements.Remove(Key);
			};
			for (auto Id : Progress.Committed) Apply(Id,true);
			for (auto Id : Progress.Failed) Apply(Id,false);
		}
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
	const auto* Definition = GuLiCommanderBehavior::FindPolicy(*GetWorld(), State->Context.UnitTypeId);
	return Definition && Definition->Behavior == EGuLiCommanderBehavior::StrongholdAdvance;
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
	if (Command.Kind != EGuLiUnitTaskKind::Move || Command.bGroundMoveOnly || !GetWorld()->GetSubsystem<UGuLiUnitDataSubsystem>()->IsCatalogValid()) return false;
	const auto Construction = EGuLiCommanderBehavior::Construction;
	bool bHasBuilder = false;
	for (auto Id : Members(Selection)) if (const auto* State = States.Find(Id))
		if (const auto* Policy = GuLiCommanderBehavior::FindPolicy(*GetWorld(), State->Context.UnitTypeId); Policy && Policy->Behavior == Construction) { bHasBuilder = true; break; }
	if (!bHasBuilder) return false;
	TArray<UGuLiBuildingLifecycleComponent*> Buildings; GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Query(Buildings);
	for (const auto* Building : Buildings)
		if (Building->GetState().Phase == EGuLiBuildingPhase::UnderConstruction
			&& FVector::DistSquared2D(Building->GetGroundLocation(), Command.Target) <= FMath::Square(Building->GetDefinition().CollisionExtent.Size2D()))
		{
			for (auto Id : Members(Selection)) if (const auto* State = States.Find(Id))
				if (const auto* Definition = GuLiCommanderBehavior::FindPolicy(*GetWorld(), State->Context.UnitTypeId); Definition && Definition->Behavior == Construction)
				{ Command.Kind = EGuLiUnitTaskKind::Special; Command.SpecialTaskId = Definition->GetWireId(); Command.BuildingId = Building->GetState().InstanceId; return true; }
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
		for (const auto& Grant : State->AutomaticBehaviors) if (!Grant.bConsumed)
			if (const auto* Definition = GuLiCommanderBehavior::FindPolicy(*GetWorld(), State->Context.UnitTypeId); Definition && Definition->bAutoActivate)
			{
				Summary.RecoverableTasks.Add(Definition->DisplayName);
				Key += FString::Printf(TEXT("/grant:%d"), Grant.BehaviorId);
			}
		auto Add = [&](const FGuLiUnitTaskCommand& Command, EGuLiTaskStatus Status, bool Automatic)
		{
			auto& View = Summary.Tasks.AddDefaulted_GetRef(); View.Command = Command; View.Status = Status; View.bAutomatic = Automatic;
			const auto* Definition = Command.Kind == EGuLiUnitTaskKind::Special
				? GuLiCommanderBehavior::FindPolicy(*GetWorld(), State->Context.UnitTypeId) : nullptr;
			View.DisplayName = Definition ? Definition->DisplayName : BasicName(Command.Kind);
			View.Location = Command.Target;
			View.bHasLocation = Command.Kind == EGuLiUnitTaskKind::Move;
			const auto* Resources = GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
			if (Command.Kind == EGuLiUnitTaskKind::Transit && Resources)
				View.bHasLocation = Resources->FindTerritoryIndexById(Command.TerritoryId) != INDEX_NONE;
			if (Command.BuildingId)
				if (const auto* Building = GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Find(Command.BuildingId))
				{ View.Location = Building->GetGroundLocation(); View.bHasLocation = Building->GetState().Phase != EGuLiBuildingPhase::Destroyed; }
			uint16 Cluster = Command.ClusterId;
			if (Automatic && Definition && Definition->Behavior == EGuLiCommanderBehavior::Mining)
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
