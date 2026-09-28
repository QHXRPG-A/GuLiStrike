#include "Commander/Behavior/GuLiCommanderStateTreeNodes.h"
#include "Commander/Behavior/GuLiCommanderStateTreeComponent.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Commander/Mass/GuLiCommanderMassFragments.h"
#include "Gameplay/Stronghold/GuLiArmyAdvanceSubsystem.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "MassStateTreeDependency.h"
#include "MassStateTreeExecutionContext.h"
#include "MassEntityManager.h"
#include "Engine/World.h"

namespace
{
	FGuLiTaskUnitId ActorUnit(FStateTreeExecutionContext& Context)
	{
		const auto* Actor = Cast<AActor>(Context.GetOwner());
		const auto* Runner = Actor ? Actor->FindComponentByClass<UGuLiCommanderStateTreeComponent>() : nullptr;
		return Runner ? Runner->GetUnit() : FGuLiTaskUnitId{};
	}
	FGuLiTaskUnitId MassUnit(FStateTreeExecutionContext& Context)
	{
		const auto& Mass = static_cast<FMassStateTreeExecutionContext&>(Context);
		return FGuLiTaskUnitId::Soldier(Mass.GetEntityManager().GetFragmentDataChecked<FGuLiMassIdentityFragment>(Mass.GetEntity()).SoldierId);
	}
	bool Matches(FStateTreeExecutionContext& Context, FGuLiTaskUnitId Unit, uint32 Required, uint32 Forbidden, EGuLiCommanderWorkPhase Phase, EGuLiCommanderWorkResult Result)
	{
		const auto* Tasks = Context.GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>();
		if (!Tasks || !Tasks->HasState(Unit)) return false;
		const uint32 Facts = Tasks->GetBehaviorFacts(Unit);
		return (Facts & Required) == Required && (Facts & Forbidden) == 0
			&& (Phase == EGuLiCommanderWorkPhase::Any || Tasks->GetWorkPhase(Unit) == Phase)
			&& (Result == EGuLiCommanderWorkResult::Any || Tasks->GetWorkResult(Unit) == Result);
	}
	EStateTreeRunStatus Request(FStateTreeExecutionContext& Context, FGuLiTaskUnitId Unit, EGuLiCommanderBehaviorStep Step)
	{
		auto* Tasks = Context.GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>();
		if (!Tasks || !Tasks->HasState(Unit)) return EStateTreeRunStatus::Failed;
		Tasks->RequestBehaviorStep(Unit, Step);
		return EStateTreeRunStatus::Running;
	}
}

bool FGuLiCommanderActorBehaviorCondition::TestCondition(FStateTreeExecutionContext& Context) const
{ return Matches(Context, ActorUnit(Context), Required, Forbidden, Phase, Result); }
bool FGuLiCommanderMassBehaviorCondition::TestCondition(FStateTreeExecutionContext& Context) const
{ return Matches(Context, MassUnit(Context), Required, Forbidden, Phase, Result); }
void FGuLiCommanderMassBehaviorCondition::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadOnly<FGuLiMassIdentityFragment>().AddReadOnly<UGuLiUnitTaskSubsystem>()
		.AddReadOnly<UGuLiArmyAdvanceSubsystem>().AddReadOnly<UGuLiUnitDataSubsystem>();
}

FGuLiCommanderActorBehaviorTask::FGuLiCommanderActorBehaviorTask()
{ bShouldCallTick = true; bShouldStateChangeOnReselect = true; }
FGuLiCommanderActorBehaviorTask::FGuLiCommanderActorBehaviorTask(EGuLiCommanderBehaviorStep InStep) : FGuLiCommanderActorBehaviorTask() { Step = InStep; }
EStateTreeRunStatus FGuLiCommanderActorBehaviorTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{ return Request(Context, ActorUnit(Context), Step); }
EStateTreeRunStatus FGuLiCommanderActorBehaviorTask::Tick(FStateTreeExecutionContext&, float) const
{ return EStateTreeRunStatus::Succeeded; }

FGuLiCommanderMassBehaviorTask::FGuLiCommanderMassBehaviorTask()
{ bShouldCallTick = true; bShouldStateChangeOnReselect = true; }
FGuLiCommanderMassBehaviorTask::FGuLiCommanderMassBehaviorTask(EGuLiCommanderBehaviorStep InStep) : FGuLiCommanderMassBehaviorTask() { Step = InStep; }
EStateTreeRunStatus FGuLiCommanderMassBehaviorTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{ return Request(Context, MassUnit(Context), Step); }
EStateTreeRunStatus FGuLiCommanderMassBehaviorTask::Tick(FStateTreeExecutionContext&, float) const
{ return EStateTreeRunStatus::Succeeded; }
void FGuLiCommanderMassBehaviorTask::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{ Builder.AddReadOnly<FGuLiMassIdentityFragment>().AddReadWrite<UGuLiUnitTaskSubsystem>(); }

namespace
{
	bool CanResume(UGuLiUnitTaskSubsystem& Tasks, FGuLiTaskUnitId Unit, const FGuLiCommanderPersistentOperation& Operation)
	{
		using Result = EGuLiCommanderWorkResult;
		const auto Value = Tasks.GetWorkResult(Unit);
		return Operation.ResumePhases.Contains(Tasks.GetWorkPhase(Unit)) && Value != Result::Failed && Value != Result::OutOfRange
			&& Value != Result::TargetLost && Value != Result::FactoryLost && Value != Result::FactoryUnreachable && Value != Result::NoTarget;
	}

	EStateTreeRunStatus EnterPersistent(FStateTreeExecutionContext& Context, FGuLiTaskUnitId Unit,
		FGuLiCommanderPersistentNodeData& Data, const FGuLiCommanderPersistentOperation& Operation)
	{
		auto* Tasks = Context.GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>();
		if (!Tasks || !Tasks->HasState(Unit)) return EStateTreeRunStatus::Failed;
		const auto Token = Tasks->BeginBehaviorOperation(Unit, Operation.Control);
		Data.Version = Token.Version; Data.ExecutionId = Token.ExecutionId; Data.Serial = Token.Serial;
		Data.bEntryApplied = CanResume(*Tasks, Unit, Operation);
		Tasks->RequestBehaviorOperation(Unit, Token, Data.bEntryApplied ? Operation.WhileRunning : Operation.Entry);
		return EStateTreeRunStatus::Running;
	}

	EStateTreeRunStatus TickPersistent(FStateTreeExecutionContext& Context, FGuLiTaskUnitId Unit,
		FGuLiCommanderPersistentNodeData& Data, const FGuLiCommanderPersistentOperation& Operation)
	{
		auto* Tasks = Context.GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>();
		if (!Tasks || !Tasks->HasState(Unit)) return EStateTreeRunStatus::Failed;
		const auto Token = Data.Token();
		const auto Receipt = Tasks->GetBehaviorOperationReceipt(Unit, Token);
		// Taking/finalizing an order can change its identity. Consume that operation's receipt first.
		if (Operation.bCompleteOnReceipt && Receipt == EGuLiCommanderOperationResult::Applied)
			return EStateTreeRunStatus::Succeeded;
		if (!Tasks->IsBehaviorOperationCurrent(Unit, Token))
		{
			Tasks->WaitBehaviorRound(Unit, Token);
			return EStateTreeRunStatus::Running; // The common parent transition rebinds the new order.
		}
		if (Receipt == EGuLiCommanderOperationResult::Failed) return EStateTreeRunStatus::Failed;
		if (Receipt == EGuLiCommanderOperationResult::Applied) Data.bEntryApplied = true;
		// A resumed shared Mass group may have accepted this phase through another member.
		if (!Data.bEntryApplied && CanResume(*Tasks, Unit, Operation)) Data.bEntryApplied = true;
		Tasks->RequestBehaviorOperation(Unit, Token, Data.bEntryApplied ? Operation.WhileRunning : Operation.Entry);
		return EStateTreeRunStatus::Running;
	}

	void ExitPersistent(FStateTreeExecutionContext& Context, FGuLiTaskUnitId Unit, const FGuLiCommanderPersistentNodeData& Data)
	{
		if (auto* Tasks = Context.GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>()) Tasks->EndBehaviorOperation(Unit, Data.Token());
	}
}

FGuLiCommanderActorPersistentTask::FGuLiCommanderActorPersistentTask()
{ bShouldCallTick = true; bShouldStateChangeOnReselect = true; }
FGuLiCommanderActorPersistentTask::FGuLiCommanderActorPersistentTask(const FGuLiCommanderPersistentOperation& InOperation)
	: FGuLiCommanderActorPersistentTask() { Operation = InOperation; }
EStateTreeRunStatus FGuLiCommanderActorPersistentTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{ return EnterPersistent(Context, ActorUnit(Context), Context.GetInstanceData(*this), Operation); }
EStateTreeRunStatus FGuLiCommanderActorPersistentTask::Tick(FStateTreeExecutionContext& Context, float) const
{ return TickPersistent(Context, ActorUnit(Context), Context.GetInstanceData(*this), Operation); }
void FGuLiCommanderActorPersistentTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{ ExitPersistent(Context, ActorUnit(Context), Context.GetInstanceData(*this)); }

FGuLiCommanderMassPersistentTask::FGuLiCommanderMassPersistentTask()
{ bShouldCallTick = true; bShouldStateChangeOnReselect = true; }
FGuLiCommanderMassPersistentTask::FGuLiCommanderMassPersistentTask(const FGuLiCommanderPersistentOperation& InOperation)
	: FGuLiCommanderMassPersistentTask() { Operation = InOperation; }
EStateTreeRunStatus FGuLiCommanderMassPersistentTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{ return EnterPersistent(Context, MassUnit(Context), Context.GetInstanceData(*this), Operation); }
EStateTreeRunStatus FGuLiCommanderMassPersistentTask::Tick(FStateTreeExecutionContext& Context, float) const
{ return TickPersistent(Context, MassUnit(Context), Context.GetInstanceData(*this), Operation); }
void FGuLiCommanderMassPersistentTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{ ExitPersistent(Context, MassUnit(Context), Context.GetInstanceData(*this)); }
void FGuLiCommanderMassPersistentTask::GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const
{
	Builder.AddReadOnly<FGuLiMassIdentityFragment>().AddReadWrite<UGuLiUnitTaskSubsystem>()
		.AddReadOnly<UGuLiArmyAdvanceSubsystem>().AddReadOnly<UGuLiUnitDataSubsystem>();
}
