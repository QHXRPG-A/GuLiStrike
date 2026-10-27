#include "Gameplay/GroundMech/Abilities/RocketJump/GuLiGA_RocketJump.h"
#include "Gameplay/GroundMech/Abilities/RocketJump/GuLiGroundMechRocketComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "NativeGameplayTags.h"
#include "GameFramework/Actor.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_RocketJump,"Ability.GroundMech.RocketJump");
namespace
{
UGuLiGroundMechRocketComponent* Rocket(const FGameplayAbilityActorInfo* Info)
{
    return Info && Info->AvatarActor.IsValid() ? Info->AvatarActor->FindComponentByClass<UGuLiGroundMechRocketComponent>() : nullptr;
}
}

UGuLiGA_RocketJump::UGuLiGA_RocketJump()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
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
