#pragma once
#include "Abilities/GameplayAbility.h"
#include "Gameplay/Skills/GuLiSkillTypes.h"
#include "GuLiArmySkillAbility.generated.h"

/** Local synchronous payload: never a client-authored team or an RPC. */
UCLASS()
class UGuLiArmySkillCommandPayload final : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY() FGuLiArmySkillCommand Request;
	mutable bool bExecuted = false;
	mutable bool bSucceeded = false;
	mutable FString Error;
};

/** One server-only GA per player ASC; independent of soldier/skill/source counts. */
UCLASS()
class GULISTRIKE_API UGuLiArmySkillAbility final : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UGuLiArmySkillAbility();
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};
