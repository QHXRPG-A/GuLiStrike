#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "GuLiGroundMechAbilities.generated.h"

/** Pawn-owned resource: only the server applies effects; CMC carries a rollbackable prediction. */
UCLASS()
class GULISTRIKE_API UGuLiGroundMechAttributeSet : public UAttributeSet
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Fuel, Category="Mech|Rocket Jump")
    FGameplayAttributeData Fuel;
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxFuel, Category="Mech|Rocket Jump")
    FGameplayAttributeData MaxFuel;
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UGuLiGroundMechAttributeSet, Fuel)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(Fuel)
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UGuLiGroundMechAttributeSet, MaxFuel)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(MaxFuel)
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
    virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
private:
    UFUNCTION() void OnRep_Fuel(const FGameplayAttributeData& Previous);
    UFUNCTION() void OnRep_MaxFuel(const FGameplayAttributeData& Previous);
};

UCLASS()
class UGuLiGE_MechFuelInitialize : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UGuLiGE_MechFuelInitialize();
};

UCLASS()
class UGuLiGE_MechFuelDelta : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UGuLiGE_MechFuelDelta();
};

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
