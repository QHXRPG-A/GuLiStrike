// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "StateTreeConditionBase.h"
#include "StateTreeTaskBase.h"
#include "GuLiWingmanMemberBehavior.generated.h"

/** Priority-ordered, per-Pawn behavior states authored in the native StateTree asset. */
UENUM(BlueprintType)
enum class EGuLiWingmanMemberBehavior : uint8
{
	Dead = 0,
	EmergencyAvoid,
	GroundAttack,
	AirAttack,
	Rejoin,
	EscortOrbit
};

namespace GuLiWingmanMemberBehaviorTags
{
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Dead);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(EmergencyAvoid);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(GroundAttack);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(AirAttack);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Rejoin);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(EscortOrbit);

	GULISTRIKE_API FGameplayTag GetSignalTag(EGuLiWingmanMemberBehavior Behavior);
}

USTRUCT()
struct FGuLiWingmanMemberBehaviorConditionInstanceData
{
	GENERATED_BODY()
};

/** Native StateTree condition; it only observes the owning Wingman Pawn. */
USTRUCT(meta=(DisplayName="Wingman Member Behavior Is", Category="GuLiStrike|Wingman"))
struct GULISTRIKE_API FGuLiWingmanMemberBehaviorCondition final
	: public FStateTreeConditionCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FGuLiWingmanMemberBehaviorConditionInstanceData;

	FGuLiWingmanMemberBehaviorCondition() = default;
	explicit FGuLiWingmanMemberBehaviorCondition(
		EGuLiWingmanMemberBehavior InBehavior)
		: Behavior(InBehavior)
	{
	}

	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup,
		EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif

	UPROPERTY(EditAnywhere, Category="Behavior")
	EGuLiWingmanMemberBehavior Behavior = EGuLiWingmanMemberBehavior::EscortOrbit;
};

USTRUCT()
struct FGuLiWingmanMemberBehaviorTaskInstanceData
{
	GENERATED_BODY()
};

/** Native StateTree task that publishes a movement request and never moves the Pawn. */
USTRUCT(meta=(DisplayName="Apply Wingman Member Behavior", Category="GuLiStrike|Wingman"))
struct GULISTRIKE_API FGuLiWingmanMemberBehaviorTask final
	: public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FGuLiWingmanMemberBehaviorTaskInstanceData;

	FGuLiWingmanMemberBehaviorTask();
	explicit FGuLiWingmanMemberBehaviorTask(EGuLiWingmanMemberBehavior InBehavior);
	virtual const UStruct* GetInstanceDataType() const override
	{
		return FInstanceDataType::StaticStruct();
	}
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup,
		EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif

	static constexpr bool WritesTransform() { return false; }
	static constexpr bool WritesVelocity() { return false; }

	UPROPERTY(EditAnywhere, Category="Behavior")
	EGuLiWingmanMemberBehavior Behavior = EGuLiWingmanMemberBehavior::EscortOrbit;
};

GULISTRIKE_API FName GuLiWingmanMemberBehaviorName(EGuLiWingmanMemberBehavior Behavior);
