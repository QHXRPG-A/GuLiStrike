// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/Behavior/GuLiWingmanMemberBehavior.h"

#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "StateTreeExecutionContext.h"

#define LOCTEXT_NAMESPACE "GuLiWingmanMemberBehavior"

namespace GuLiWingmanMemberBehaviorTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Dead, "GuLi.Wingman.Member.Dead", "Member died");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(EmergencyAvoid, "GuLi.Wingman.Member.EmergencyAvoid", "Emergency avoidance");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GroundAttack, "GuLi.Wingman.Member.GroundAttack", "Ground attack run");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(AirAttack, "GuLi.Wingman.Member.AirAttack", "Air attack run");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Rejoin, "GuLi.Wingman.Member.Rejoin", "Rejoin carrier formation");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(EscortOrbit, "GuLi.Wingman.Member.EscortOrbit", "Escort orbit");

	FGameplayTag GetSignalTag(const EGuLiWingmanMemberBehavior Behavior)
	{
		switch (Behavior)
		{
		case EGuLiWingmanMemberBehavior::Dead: return Dead;
		case EGuLiWingmanMemberBehavior::EmergencyAvoid: return EmergencyAvoid;
		case EGuLiWingmanMemberBehavior::GroundAttack: return GroundAttack;
		case EGuLiWingmanMemberBehavior::AirAttack: return AirAttack;
		case EGuLiWingmanMemberBehavior::Rejoin: return Rejoin;
		case EGuLiWingmanMemberBehavior::EscortOrbit: return EscortOrbit;
		default: return FGameplayTag();
		}
	}
}

FName GuLiWingmanMemberBehaviorName(const EGuLiWingmanMemberBehavior Behavior)
{
	switch (Behavior)
	{
	case EGuLiWingmanMemberBehavior::Dead: return TEXT("Dead");
	case EGuLiWingmanMemberBehavior::EmergencyAvoid: return TEXT("EmergencyAvoid");
	case EGuLiWingmanMemberBehavior::GroundAttack: return TEXT("GroundAttack");
	case EGuLiWingmanMemberBehavior::AirAttack: return TEXT("AirAttack");
	case EGuLiWingmanMemberBehavior::Rejoin: return TEXT("Rejoin");
	case EGuLiWingmanMemberBehavior::EscortOrbit: return TEXT("EscortOrbit");
	default: return NAME_None;
	}
}

bool FGuLiWingmanMemberBehaviorCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AGuLiWingmanPawn* Pawn = Cast<AGuLiWingmanPawn>(Context.GetOwner());
	return Pawn && Pawn->ShouldSelectBehavior(Behavior);
}

#if WITH_EDITOR
FText FGuLiWingmanMemberBehaviorCondition::GetDescription(
	const FGuid& ID,
	FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup,
	const EStateTreeNodeFormatting Formatting) const
{
	return FText::Format(LOCTEXT("ConditionDescription", "Behavior is {0}"),
		FText::FromName(GuLiWingmanMemberBehaviorName(Behavior)));
}
#endif

FGuLiWingmanMemberBehaviorTask::FGuLiWingmanMemberBehaviorTask()
{
	bShouldCallTick = true;
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
	bShouldStateChangeOnReselect = false;
}

FGuLiWingmanMemberBehaviorTask::FGuLiWingmanMemberBehaviorTask(
	const EGuLiWingmanMemberBehavior InBehavior)
	: FGuLiWingmanMemberBehaviorTask()
{
	Behavior = InBehavior;
}

EStateTreeRunStatus FGuLiWingmanMemberBehaviorTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition) const
{
	AGuLiWingmanPawn* Pawn = Cast<AGuLiWingmanPawn>(Context.GetOwner());
	return Pawn && Pawn->ApplyStateTreeBehavior(Behavior)
		? EStateTreeRunStatus::Running
		: EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FGuLiWingmanMemberBehaviorTask::Tick(
	FStateTreeExecutionContext& Context,
	const float DeltaTime) const
{
	AGuLiWingmanPawn* Pawn = Cast<AGuLiWingmanPawn>(Context.GetOwner());
	if (!Pawn)
	{
		return EStateTreeRunStatus::Failed;
	}
	if (!Pawn->ShouldSelectBehavior(Behavior))
	{
		return EStateTreeRunStatus::Succeeded;
	}
	return Pawn->ApplyStateTreeBehavior(Behavior)
		? EStateTreeRunStatus::Running
		: EStateTreeRunStatus::Failed;
}

#if WITH_EDITOR
FText FGuLiWingmanMemberBehaviorTask::GetDescription(
	const FGuid& ID,
	FStateTreeDataView InstanceDataView,
	const IStateTreeBindingLookup& BindingLookup,
	const EStateTreeNodeFormatting Formatting) const
{
	return FText::Format(LOCTEXT("TaskDescription", "Request {0}"),
		FText::FromName(GuLiWingmanMemberBehaviorName(Behavior)));
}
#endif

#undef LOCTEXT_NAMESPACE
