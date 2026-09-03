// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/Behavior/GuLiWingmanBehaviorStateTree.h"

#include "Gameplay/Wingman/Behavior/GuLiWingmanGroupBehaviorRunner.h"
#include "StateTreeExecutionContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GuLiWingmanBehaviorStateTree)

#define LOCTEXT_NAMESPACE "GuLiWingmanBehaviorStateTree"

namespace GuLiWingmanBehaviorTags
{
	UE_DEFINE_GAMEPLAY_TAG(JoiningEscort, "Wingman.Behavior.JoiningEscort");
	UE_DEFINE_GAMEPLAY_TAG(EscortOrbit, "Wingman.Behavior.EscortOrbit");
	UE_DEFINE_GAMEPLAY_TAG(EmergencyAvoid, "Wingman.Behavior.EmergencyAvoid");
	UE_DEFINE_GAMEPLAY_TAG(OwnerUnavailable, "Wingman.Behavior.OwnerUnavailable");
	UE_DEFINE_GAMEPLAY_TAG(Dead, "Wingman.Behavior.Dead");

	FGameplayTag GetSignalTag(const EGuLiWingmanBehaviorPolicy Policy)
	{
		switch (Policy)
		{
		case EGuLiWingmanBehaviorPolicy::JoiningEscort: return JoiningEscort;
		case EGuLiWingmanBehaviorPolicy::EscortOrbit: return EscortOrbit;
		case EGuLiWingmanBehaviorPolicy::EmergencyAvoid: return EmergencyAvoid;
		case EGuLiWingmanBehaviorPolicy::OwnerUnavailable: return OwnerUnavailable;
		case EGuLiWingmanBehaviorPolicy::Dead: return Dead;
		default: return FGameplayTag();
		}
	}
}

FName GuLiWingmanBehaviorPolicyName(const EGuLiWingmanBehaviorPolicy Policy)
{
	switch (Policy)
	{
	case EGuLiWingmanBehaviorPolicy::JoiningEscort: return TEXT("JoiningEscort");
	case EGuLiWingmanBehaviorPolicy::EscortOrbit: return TEXT("EscortOrbit");
	case EGuLiWingmanBehaviorPolicy::EmergencyAvoid: return TEXT("EmergencyAvoid");
	case EGuLiWingmanBehaviorPolicy::OwnerUnavailable: return TEXT("OwnerUnavailable");
	case EGuLiWingmanBehaviorPolicy::Dead: return TEXT("Dead");
	default: return NAME_None;
	}
}

bool FGuLiWingmanBehaviorPolicyCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AGuLiWingmanGroupBehaviorRunner* Runner =
		Cast<AGuLiWingmanGroupBehaviorRunner>(Context.GetOwner());
	return Runner && Runner->ShouldSelectPolicy(Policy);
}

#if WITH_EDITOR
FText FGuLiWingmanBehaviorPolicyCondition::GetDescription(
	const FGuid& ID,
	FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup,
	const EStateTreeNodeFormatting Formatting) const
{
	return FText::Format(LOCTEXT("PolicyConditionDescription", "Policy is {0}"),
		FText::FromName(GuLiWingmanBehaviorPolicyName(Policy)));
}
#endif

FGuLiWingmanBehaviorPolicyTask::FGuLiWingmanBehaviorPolicyTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
	bShouldStateChangeOnReselect = false;
}

FGuLiWingmanBehaviorPolicyTask::FGuLiWingmanBehaviorPolicyTask(
	const EGuLiWingmanBehaviorPolicy InPolicy)
	: FGuLiWingmanBehaviorPolicyTask()
{
	Policy = InPolicy;
}

EStateTreeRunStatus FGuLiWingmanBehaviorPolicyTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	AGuLiWingmanGroupBehaviorRunner* Runner =
		Cast<AGuLiWingmanGroupBehaviorRunner>(Context.GetOwner());
	return Runner && Runner->ApplyStateTreePolicy(Policy, 0.0f)
		? EStateTreeRunStatus::Running
		: EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FGuLiWingmanBehaviorPolicyTask::Tick(
	FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	AGuLiWingmanGroupBehaviorRunner* Runner =
		Cast<AGuLiWingmanGroupBehaviorRunner>(Context.GetOwner());
	if (!Runner)
	{
		return EStateTreeRunStatus::Failed;
	}
	if (!Runner->ShouldSelectPolicy(Policy))
	{
		// The root's event transition normally switches policy first. This
		// completion path is a deterministic recovery if an event was coalesced.
		return EStateTreeRunStatus::Succeeded;
	}
	return Runner->ApplyStateTreePolicy(Policy, DeltaTime)
		? EStateTreeRunStatus::Running
		: EStateTreeRunStatus::Failed;
}

#if WITH_EDITOR
FText FGuLiWingmanBehaviorPolicyTask::GetDescription(
	const FGuid& ID,
	FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup,
	const EStateTreeNodeFormatting Formatting) const
{
	return FText::Format(LOCTEXT("PolicyTaskDescription", "Apply {0} mode policy"),
		FText::FromName(GuLiWingmanBehaviorPolicyName(Policy)));
}
#endif

#undef LOCTEXT_NAMESPACE

