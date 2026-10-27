#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GuLiGA_Blink.generated.h"

UCLASS()
class GULISTRIKE_API UGuLiGA_Blink : public UGameplayAbility
{
    GENERATED_BODY()
public:
    UGuLiGA_Blink();
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags=nullptr, const FGameplayTagContainer* TargetTags=nullptr,
        FGameplayTagContainer* OptionalRelevantTags=nullptr) const override;
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
    virtual bool CheckCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayTagContainer* OptionalRelevantTags=nullptr) const override;
    virtual void ApplyCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayAbilityActivationInfo ActivationInfo) const override;
    virtual const FGameplayTagContainer* GetCooldownTags() const override;
    virtual float GetCooldownTimeRemaining(const FGameplayAbilityActorInfo* ActorInfo) const override;
    virtual void GetCooldownTimeRemainingAndDuration(FGameplayAbilitySpecHandle Handle,
        const FGameplayAbilityActorInfo* ActorInfo, float& TimeRemaining, float& CooldownDuration) const override;
private:
    UFUNCTION() void BlinkEvent(FGameplayEventData Payload);
};
