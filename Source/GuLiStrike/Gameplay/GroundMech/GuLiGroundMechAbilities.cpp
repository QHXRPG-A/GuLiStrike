#include "Gameplay/GroundMech/GuLiGroundMechAbilities.h"
#include "Gameplay/GroundMech/GuLiGroundMechRocketComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "GameplayEffectExtension.h"
#include "NativeGameplayTags.h"
#include "Net/UnrealNetwork.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_RocketJump, "Ability.GroundMech.RocketJump");

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
UGuLiGroundMechRocketComponent* Rocket(const FGameplayAbilityActorInfo* Info)
{
    return Info && Info->AvatarActor.IsValid() ? Info->AvatarActor->FindComponentByClass<UGuLiGroundMechRocketComponent>() : nullptr;
}
}

void UGuLiGroundMechAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION_NOTIFY(UGuLiGroundMechAttributeSet, Fuel, COND_OwnerOnly, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UGuLiGroundMechAttributeSet, MaxFuel, COND_OwnerOnly, REPNOTIFY_Always);
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
void UGuLiGroundMechAttributeSet::OnRep_Fuel(const FGameplayAttributeData& Previous)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGuLiGroundMechAttributeSet, Fuel, Previous);
    if (auto* Owner = GetOwningActor())
        if (auto* Component = Owner->FindComponentByClass<UGuLiGroundMechRocketComponent>()) Component->ReceiveInitialFuel(GetFuel());
}
void UGuLiGroundMechAttributeSet::OnRep_MaxFuel(const FGameplayAttributeData& Previous)
{
    GAMEPLAYATTRIBUTE_REPNOTIFY(UGuLiGroundMechAttributeSet, MaxFuel, Previous);
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
UGuLiGA_RocketJump::UGuLiGA_RocketJump()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
    FGameplayTagContainer Tags;
    Tags.AddTag(TAG_RocketJump);
    SetAssetTags(Tags);
}
bool UGuLiGA_RocketJump::CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    const FGameplayTagContainer* Source, const FGameplayTagContainer* Target, FGameplayTagContainer* Relevant) const
{
    const auto* Component = Rocket(Info);
    return Component && Component->CanActivateRocket() && Super::CanActivateAbility(Handle, Info, Source, Target, Relevant);
}
void UGuLiGA_RocketJump::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event)
{
    if (!CommitAbility(Handle, Info, Activation)) { EndAbility(Handle, Info, Activation, true, true); return; }
    Rocket(Info)->SetAbilityActive(true);
    auto* Release = UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
    Release->OnRelease.AddDynamic(this, &ThisClass::Released);
    Release->ReadyForActivation();
}
void UGuLiGA_RocketJump::Released(float)
{
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
void UGuLiGA_RocketJump::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled)
{
    if (auto* Component = Rocket(Info)) Component->SetAbilityActive(false);
    Super::EndAbility(Handle, Info, Activation, bReplicate, bCancelled);
}
