#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GuLiGA_RocketJump.generated.h"

UCLASS()
class GULISTRIKE_API UGuLiGA_RocketJump : public UGameplayAbility
{
    GENERATED_BODY()
public:
    UGuLiGA_RocketJump();
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags=nullptr, const FGameplayTagContainer* TargetTags=nullptr,
        FGameplayTagContainer* OptionalRelevantTags=nullptr) const override;
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
private:
    UFUNCTION() void Released(float TimeHeld);
};
