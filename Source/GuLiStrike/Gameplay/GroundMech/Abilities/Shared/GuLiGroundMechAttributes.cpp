#include "Gameplay/GroundMech/Abilities/Shared/GuLiGroundMechAttributes.h"
#include "GameplayEffectExtension.h"

namespace
{
FGameplayModifierInfo FuelModifier(FGameplayAttribute Attribute, EGameplayModOp::Type Operation, FName Name)
{
    FGameplayModifierInfo Modifier;
    Modifier.Attribute = Attribute;
    Modifier.ModifierOp = Operation;
    FSetByCallerFloat Caller;
    Caller.DataName = Name;
    Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Caller);
    return Modifier;
}
}

void UGuLiGroundMechAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& Value)
{
    Super::PreAttributeChange(Attribute, Value);
    if (Attribute == GetMaxFuelAttribute()) Value = FMath::Max(0.f, Value);
    if (Attribute == GetFuelAttribute()) Value = FMath::Clamp(Value, 0.f, GetMaxFuel());
}
void UGuLiGroundMechAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
    Super::PostGameplayEffectExecute(Data);
    if (GetFuel() < 0.f || GetFuel() > GetMaxFuel())
        GetOwningAbilitySystemComponent()->SetNumericAttributeBase(GetFuelAttribute(), FMath::Clamp(GetFuel(), 0.f, GetMaxFuel()));
}
UGuLiGE_MechFuelInitialize::UGuLiGE_MechFuelInitialize()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    Modifiers.Add(FuelModifier(UGuLiGroundMechAttributeSet::GetMaxFuelAttribute(), EGameplayModOp::Override, TEXT("MaxFuel")));
    Modifiers.Add(FuelModifier(UGuLiGroundMechAttributeSet::GetFuelAttribute(), EGameplayModOp::Override, TEXT("Fuel")));
}
UGuLiGE_MechFuelDelta::UGuLiGE_MechFuelDelta()
{
    DurationPolicy = EGameplayEffectDurationType::Instant;
    Modifiers.Add(FuelModifier(UGuLiGroundMechAttributeSet::GetFuelAttribute(), EGameplayModOp::Additive, TEXT("FuelDelta")));
}
