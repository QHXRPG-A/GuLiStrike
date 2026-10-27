#include "Gameplay/GroundMech/Abilities/Blink/GuLiGE_BlinkCooldown.h"
#include "Gameplay/GroundMech/Abilities/Blink/GuLiGroundMechBlinkTags.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

UGuLiGE_BlinkCooldown::UGuLiGE_BlinkCooldown()
{
    DurationPolicy=EGameplayEffectDurationType::Infinite;
    auto* TargetTags=CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("BlinkCooldownTags"));
    GEComponents.Add(TargetTags);
    FInheritedTagContainer Tags;
    Tags.AddTag(GuLiBlinkTags::Cooldown);
    TargetTags->SetAndApplyTargetTagChanges(Tags);
}
