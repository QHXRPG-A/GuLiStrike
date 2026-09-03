// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/Abilities/GuLiShipGameplayAbility.h"

#include "Gameplay/Ship/Abilities/GuLiShipAbilitySystemComponent.h"

UGuLiShipGameplayAbility::UGuLiShipGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGuLiShipGameplayAbility::ConfigureNativeAbility(
	const FGameplayTag InStableAbilityId,
	const EGuLiShipAbilitySlot InSlot,
	const EGuLiShipAbilityActivationPolicy InActivationPolicy)
{
	StableAbilityId = InStableAbilityId;
	AbilitySlot = InSlot;
	ActivationPolicy = InActivationPolicy;
}

bool UGuLiShipGameplayAbility::CanActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const UGuLiShipAbilitySystemComponent* ShipASC = ActorInfo
		? Cast<UGuLiShipAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get())
		: nullptr;
	return ShipASC && ShipASC->CanActivateConfiguredAbility(Handle, StableAbilityId, AbilitySlot, ActivationPolicy);
}

void UGuLiShipGameplayAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	UGuLiShipAbilitySystemComponent* ShipASC = ActorInfo
		? Cast<UGuLiShipAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get())
		: nullptr;
	if (!ShipASC || !ShipASC->HandleConfiguredAbilityActivated(Handle, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (ActivationPolicy == EGuLiShipAbilityActivationPolicy::WhileGranted)
	{
		bPersistentProjectionPublished = true;
		return;
	}

	// The local predicted and authoritative executions both emit a local-only
	// authorization signal. Target acquisition and the server FireIntent gate are
	// deliberately outside GAS and key off stable ID + AbilitySetRevision.
	ShipASC->BroadcastTriggeredAbilityAuthorization(Handle, StableAbilityId, ActivationInfo);
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UGuLiShipGameplayAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility,
	const bool bWasCancelled)
{
	if (bPersistentProjectionPublished && ActorInfo)
	{
		if (UGuLiShipAbilitySystemComponent* ShipASC =
			Cast<UGuLiShipAbilitySystemComponent>(ActorInfo->AbilitySystemComponent.Get()))
		{
			ShipASC->HandleConfiguredAbilityEnded(Handle);
		}
		bPersistentProjectionPublished = false;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
