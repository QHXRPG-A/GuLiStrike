// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "StateTreeConditionBase.h"
#include "StateTreeTaskBase.h"
#include "GuLiWingmanBehaviorStateTree.generated.h"

/**
 * Group-level policy states authored in ST_WingmanGroupBehavior.
 *
 * These are deliberately not wire values. The StateTree translates the policy
 * into the existing protocol-v8 per-entity flight modes without extending the
 * Candidate contract.
 */
UENUM(BlueprintType)
enum class EGuLiWingmanBehaviorPolicy : uint8
{
	JoiningEscort,
	EscortOrbit,
	EmergencyAvoid,
	OwnerUnavailable,
	Dead
};

namespace GuLiWingmanBehaviorTags
{
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(JoiningEscort);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(EscortOrbit);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(EmergencyAvoid);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(OwnerUnavailable);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Dead);

	GULISTRIKE_API FGameplayTag GetSignalTag(EGuLiWingmanBehaviorPolicy Policy);
}

USTRUCT()
struct FGuLiWingmanBehaviorPolicyConditionInstanceData
{
	GENERATED_BODY()
};

/** State/transition gate which reads the owning group runner only. */
USTRUCT(meta=(DisplayName="Wingman Policy Is", Category="GuLiStrike|Wingman"))
struct GULISTRIKE_API FGuLiWingmanBehaviorPolicyCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGuLiWingmanBehaviorPolicyConditionInstanceData;

	FGuLiWingmanBehaviorPolicyCondition() = default;
	explicit FGuLiWingmanBehaviorPolicyCondition(EGuLiWingmanBehaviorPolicy InPolicy)
		: Policy(InPolicy)
	{
	}

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup,
		EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif

	UPROPERTY(EditAnywhere, Category="Policy")
	EGuLiWingmanBehaviorPolicy Policy = EGuLiWingmanBehaviorPolicy::EscortOrbit;
};

USTRUCT()
struct FGuLiWingmanBehaviorPolicyTaskInstanceData
{
	GENERATED_BODY()
};

/**
 * Low-frequency StateTree task. It may update FlightMode and thereby request
 * the existing async Guidance/path flow; it never writes Transform, velocity,
 * health, damage, or network protocol state.
 */
USTRUCT(meta=(DisplayName="Apply Wingman Policy", Category="GuLiStrike|Wingman"))
struct GULISTRIKE_API FGuLiWingmanBehaviorPolicyTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGuLiWingmanBehaviorPolicyTaskInstanceData;

	FGuLiWingmanBehaviorPolicyTask();
	explicit FGuLiWingmanBehaviorPolicyTask(EGuLiWingmanBehaviorPolicy InPolicy);

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
		const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;

#if WITH_EDITOR
	virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView,
		const IStateTreeBindingLookup& BindingLookup,
		EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
#endif

	/** Static acceptance-test contract: Integration remains the sole Transform/velocity writer. */
	static constexpr bool WritesMassTransform() { return false; }
	static constexpr bool WritesFlightVelocity() { return false; }
	static constexpr bool WritesHealthOrDamage() { return false; }
	static constexpr bool RequestsGuidanceThroughFlightMode() { return true; }

	UPROPERTY(EditAnywhere, Category="Policy")
	EGuLiWingmanBehaviorPolicy Policy = EGuLiWingmanBehaviorPolicy::EscortOrbit;
};

GULISTRIKE_API FName GuLiWingmanBehaviorPolicyName(EGuLiWingmanBehaviorPolicy Policy);
