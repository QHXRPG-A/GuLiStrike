#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "GameplayEffect.h"
#include "GuLiGroundMechAttributes.generated.h"

/** Simulator-owned fuel; Hybrid pose and private state carry its authoritative value. */
UCLASS()
class GULISTRIKE_API UGuLiGroundMechAttributeSet : public UAttributeSet
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadOnly, Category="Mech|Rocket Jump")
    FGameplayAttributeData Fuel;
    UPROPERTY(BlueprintReadOnly, Category="Mech|Rocket Jump")
    FGameplayAttributeData MaxFuel;
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UGuLiGroundMechAttributeSet, Fuel)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(Fuel)
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UGuLiGroundMechAttributeSet, MaxFuel)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(MaxFuel)
    virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
    virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
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
