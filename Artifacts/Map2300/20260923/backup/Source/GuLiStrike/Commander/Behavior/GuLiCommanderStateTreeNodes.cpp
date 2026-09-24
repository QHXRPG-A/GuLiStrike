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
