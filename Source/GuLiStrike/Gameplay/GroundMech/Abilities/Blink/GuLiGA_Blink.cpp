#include "Gameplay/GroundMech/Abilities/Blink/GuLiGA_Blink.h"
#include "Gameplay/GroundMech/Abilities/Blink/GuLiGE_BlinkCooldown.h"
#include "Gameplay/GroundMech/Abilities/Blink/GuLiGroundMechBlinkComponent.h"
#include "Gameplay/GroundMech/Abilities/Blink/GuLiGroundMechBlinkTags.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"

namespace
{
UGuLiGroundMechBlinkComponent* Blink(const FGameplayAbilityActorInfo* Info)
{
    const auto* Pawn=Info?Cast<AGuLiGroundMechCharacter>(Info->AvatarActor.Get()):nullptr;
    return Pawn?Pawn->GetBlinkComponent():nullptr;
}
}

UGuLiGA_Blink::UGuLiGA_Blink()
{
    InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::LocalOnly;
    SetAssetTags(FGameplayTagContainer(GuLiBlinkTags::Ability));
    ActivationOwnedTags.AddTag(GuLiBlinkTags::Active);
    CooldownGameplayEffectClass=UGuLiGE_BlinkCooldown::StaticClass();
}

bool UGuLiGA_Blink::CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    const FGameplayTagContainer* Source, const FGameplayTagContainer* Target, FGameplayTagContainer* Relevant) const
{
    const auto* Component=Blink(Info);
    return Component && (Component->IsRestoringAbility() ? Component->CanRunSimulation() : Component->CanActivate())
        && Super::CanActivateAbility(Handle,Info,Source,Target,Relevant);
}

bool UGuLiGA_Blink::CheckCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    FGameplayTagContainer* Relevant) const
{
    const auto* Component=Blink(Info);
    if (Component && Component->IsRestoringAbility()) return true;
    if (!Component || Component->GetCooldownRemaining()>0.f)
    {
        if (Relevant) Relevant->AddTag(GuLiBlinkTags::Cooldown);
        return false;
    }
    return Super::CheckCooldown(Handle,Info,Relevant);
}

void UGuLiGA_Blink::ApplyCooldown(FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo* Info,
    FGameplayAbilityActivationInfo) const
{
    if (auto* Component=Blink(Info)) Component->CommitCooldown();
}

const FGameplayTagContainer* UGuLiGA_Blink::GetCooldownTags() const
{
    static const FGameplayTagContainer Tags(GuLiBlinkTags::Cooldown);
    return &Tags;
}

float UGuLiGA_Blink::GetCooldownTimeRemaining(const FGameplayAbilityActorInfo* Info) const
{
    const auto* Component=Blink(Info);
    return Component ? Component->GetCooldownRemaining() : 0.f;
}

void UGuLiGA_Blink::GetCooldownTimeRemainingAndDuration(FGameplayAbilitySpecHandle,
    const FGameplayAbilityActorInfo* Info, float& Remaining, float& Duration) const
{
    const auto* Component=Blink(Info);
    Remaining=Component ? Component->GetCooldownRemaining() : 0.f;
    Duration=Component ? Component->GetCooldownDuration() : 0.f;
}

void UGuLiGA_Blink::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    FGameplayAbilityActivationInfo Activation, const FGameplayEventData*)
{
    auto* Component=Blink(Info);
    if (!Component || (!Component->IsRestoringAbility() && (!Component->PrepareDestination() || !CommitAbility(Handle,Info,Activation))))
    {
        EndAbility(Handle,Info,Activation,false,true);
        return;
    }
    auto* Wait=UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this,GuLiBlinkTags::Event,nullptr,false,false);
    Wait->EventReceived.AddDynamic(this,&ThisClass::BlinkEvent);
    Wait->ReadyForActivation();
    if (!Component->IsRestoringAbility()) Component->StartFromAbility();
}

void UGuLiGA_Blink::BlinkEvent(FGameplayEventData Payload)
{
    if (Payload.EventTag==GuLiBlinkTags::Completed || Payload.EventTag==GuLiBlinkTags::Interrupted)
        EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,false,Payload.EventTag==GuLiBlinkTags::Interrupted);
}

void UGuLiGA_Blink::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
    FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled)
{
    if (!IsEndAbilityValid(Handle,Info)) return;
    if (auto* Component=Blink(Info)) Component->AbilityEnded(bCancelled);
    Super::EndAbility(Handle,Info,Activation,bReplicate,bCancelled);
}
