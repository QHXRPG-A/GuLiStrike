// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTypes.h"
#include "Gameplay/Ship/Aiming/GuLiShipReticleTypes.h"
#include "GuLiShipGameplayAbility.generated.h"

/**
 * Base class for Pawn-owned Ship abilities. It publishes group authorization;
 * it never reads/writes ship numeric attributes and never creates an AttributeSet.
 */
UCLASS(Abstract, Blueprintable)
class GULISTRIKE_API UGuLiShipGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UGuLiShipGameplayAbility();

	FGameplayTag GetStableAbilityId() const { return StableAbilityId; }
	EGuLiShipAbilitySlot GetShipAbilitySlot() const { return AbilitySlot; }
	EGuLiShipAbilityActivationPolicy GetShipActivationPolicy() const { return ActivationPolicy; }

	/** Screen-space parameters used only when AssetTags contains Ship.Ability.Reticle.Bounded. */
	UFUNCTION(BlueprintPure, Category = "Ship|Abilities|Reticle")
	FGuLiShipReticleConfig GetReticleConfig() const { return ReticleConfig; }

	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

protected:
	void ConfigureNativeAbility(
		FGameplayTag InStableAbilityId,
		EGuLiShipAbilitySlot InSlot,
		EGuLiShipAbilityActivationPolicy InActivationPolicy);

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Ship|Abilities")
	FGameplayTag StableAbilityId;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Ship|Abilities")
	EGuLiShipAbilitySlot AbilitySlot = EGuLiShipAbilitySlot::None;

	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Ship|Abilities")
	EGuLiShipAbilityActivationPolicy ActivationPolicy = EGuLiShipAbilityActivationPolicy::WhileGranted;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ship|Abilities|Reticle")
	FGuLiShipReticleConfig ReticleConfig;

private:
	bool bPersistentProjectionPublished = false;
};
