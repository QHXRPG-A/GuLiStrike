#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Commander/Behavior/GuLiCommanderStateTreeNodes.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Engine/World.h"

FGuLiCommanderOperationToken UGuLiUnitTaskSubsystem::BeginBehaviorOperation(FGuLiTaskUnitId Unit, EGuLiCommanderControlState Control)
{
	check(IsInGameThread());
	auto* State = States.Find(Unit);
	if (!State) return {};
	State->ActiveOperation = {State->Version, State->Active.IsSet() ? State->Active->ExecutionId : 0, ++State->NextOperationSerial};
	State->OperationReceipt = {State->ActiveOperation, EGuLiCommanderOperationResult::None};
	State->OperationControl = Control;
	State->LastOperationRequestRound = MAX_uint64;
	State->bBehaviorStepDone = false;
	return State->ActiveOperation;
}

void UGuLiUnitTaskSubsystem::EndBehaviorOperation(FGuLiTaskUnitId Unit, const FGuLiCommanderOperationToken& Token)
{
	check(IsInGameThread());
	if (auto* State = States.Find(Unit); State && State->ActiveOperation == Token) State->ActiveOperation = {};
}

bool UGuLiUnitTaskSubsystem::IsBehaviorOperationCurrent(FGuLiTaskUnitId Unit, const FGuLiCommanderOperationToken& Token) const
{
	const auto* State = States.Find(Unit);
	return State && Token.IsValid() && State->ActiveOperation == Token && State->Version == Token.Version
		&& (State->Active.IsSet() ? State->Active->ExecutionId : 0) == Token.ExecutionId;
}

EGuLiCommanderOperationResult UGuLiUnitTaskSubsystem::GetBehaviorOperationReceipt(FGuLiTaskUnitId Unit, const FGuLiCommanderOperationToken& Token) const
{
	const auto* State = States.Find(Unit);
	return State && State->OperationReceipt.Token == Token ? State->OperationReceipt.Result : EGuLiCommanderOperationResult::Stale;
}

void UGuLiUnitTaskSubsystem::RequestBehaviorOperation(FGuLiTaskUnitId Unit, const FGuLiCommanderOperationToken& Token, EGuLiCommanderBehaviorStep Step)
{
	check(IsInGameThread());
	auto* State = States.Find(Unit);
	if (!State || !IsBehaviorOperationCurrent(Unit, Token) || State->LastOperationRequestRound == BehaviorRound) return;
	State->LastOperationRequestRound = BehaviorRound;
	State->OperationReceipt = {Token, EGuLiCommanderOperationResult::Queued};
	BehaviorRequests.Add({Unit, Step, Token.Version, Token});
	// Transitioning later in this same tick invalidates this token in ExitState. Only the final leaf commits.
}

void UGuLiUnitTaskSubsystem::WaitBehaviorRound(FGuLiTaskUnitId Unit, const FGuLiCommanderOperationToken& Token)
{
	if (auto* State = States.Find(Unit); State && State->ActiveOperation == Token) State->bBehaviorStepDone = true;
}

EGuLiCommanderOperationResult UGuLiUnitTaskSubsystem::CommitBehaviorOperation(FGuLiUnitTaskState& State, EGuLiCommanderBehaviorStep Step, double Now)
{
	using Operation = EGuLiCommanderBehaviorStep;
	using Receipt = EGuLiCommanderOperationResult;
	using Phase = EGuLiCommanderWorkPhase;
	using Result = EGuLiCommanderWorkResult;
	switch (Step)
	{
	case Operation::Wait: return Receipt::Applied;
	case Operation::CancelPending:
		if (!Cancel(State))
		{
			if (State.Active.IsSet()) State.Active->Status = EGuLiTaskStatus::WaitingSafeExit;
			return Receipt::Deferred;
		}
		State.bCancelPending = false; State.Active.Reset(); return Receipt::Applied;
	case Operation::ReplaceMove:
		PollPendingMove(State); return State.PendingMove.IsSet() ? Receipt::Deferred : Receipt::Applied;
	case Operation::TakeManual: TakeManualTask(State); return Receipt::Applied;
	case Operation::TakeAutomatic: TakeAutomaticTask(State, Now); return Receipt::Applied;
	case Operation::FinishOrder: return FinishActiveTask(State, Now) ? Receipt::Applied : Receipt::Deferred;
	default: break;
	}
	if (State.bCancelPending || State.bStopped || State.PendingMove.IsSet()) return Receipt::Deferred;
	if (!State.Active.IsSet()) return Receipt::Stale;
	if (Step == Operation::ObserveOrder || Step == Operation::StartOrder)
	{
		ObserveActiveTask(State, Step == Operation::StartOrder);
		if (Step == Operation::StartOrder && !State.Active->bStarted) return Receipt::Deferred;
		return Receipt::Applied; // Terminal status belongs to the order's explicit finish state.
	}
	if (!State.Active->bStarted || State.Active->bTerminalObserved) return Receipt::Deferred;
	if (const auto* Pawn = State.Context.Pawn.Get())
	{
		const auto* Travel = Pawn->FindComponentByClass<UGuLiEngineeringTravelComponent>();
		if (UGuLiExternalUnitControlComponent::AreActorActionsLocked(Pawn) || (Travel && Travel->IsRouting())) return Receipt::Deferred;
	}
	const auto* Miner = Cast<AGuLiMiningVehiclePawn>(State.Context.Pawn.Get());
	const bool bMiningAction = (Step >= Operation::MiningSelect && Step <= Operation::MiningComplete) || Step == Operation::MiningReposition;
	if (bMiningAction)
	{
		if (!Miner) return Receipt::Failed;
		const auto* Resources = GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
		if (!Resources || !Resources->IsRuntimeReady()) return Receipt::Deferred;
	}
	if (Step == Operation::MiningSelect && Miner && !Miner->IsBehaviorRetryReady()) return Receipt::Deferred;
	if (Step == Operation::MiningFactory && Miner && GetWorkPhase(State.Context.Unit) == Phase::MiningIdle
		&& GetWorkResult(State.Context.Unit) == Result::None && !Miner->IsBehaviorRetryReady()) return Receipt::Deferred;
	if (Step == Operation::RunTask || Step == Operation::MiningEnter || Step == Operation::MiningExit) return Receipt::Failed;
	ExecuteWorkAction(State, Step);
	ObserveActiveTask(State, false);
	const auto CurrentPhase = GetWorkPhase(State.Context.Unit);
	const auto CurrentResult = GetWorkResult(State.Context.Unit);
	// Deferred admission is not a path failure. Retry the entry operation without leaving its state.
	switch (Step)
	{
	case Operation::MiningSelect:
		if (CurrentResult == Result::WaitingPosition || CurrentResult == Result::None || CurrentResult == Result::Running) return Receipt::Deferred;
		break;
	case Operation::AdvanceSelect:
		if (CurrentResult == Result::Running || CurrentResult == Result::None) return Receipt::Deferred;
		break;
	case Operation::AdvanceMove:
		if (CurrentPhase == Phase::AdvanceSelecting && CurrentResult == Result::TargetReady) return Receipt::Deferred;
		break;
	case Operation::ConstructionReserve:
		if (CurrentPhase == Phase::ConstructionPrepared && CurrentResult == Result::None) return Receipt::Deferred;
		break;
	default: break;
	}
	// Expected business outcomes are routed by the asset's result conditions.
	if (CurrentResult == Result::Failed || CurrentResult == Result::TargetLost || CurrentResult == Result::FactoryLost
		|| CurrentResult == Result::FactoryUnreachable || CurrentResult == Result::NoTarget || CurrentResult == Result::OutOfRange)
		return Receipt::Failed;
	return Receipt::Applied;
}
