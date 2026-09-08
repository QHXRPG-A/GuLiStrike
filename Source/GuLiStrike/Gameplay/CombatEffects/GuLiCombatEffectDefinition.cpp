#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"

bool UGuLiSpellFieldDefinition::IsValidDefinition() const
{
	return FMath::IsFinite(Radius) && Radius > 0 && Radius <= 100000
		&& FMath::IsFinite(Delay) && Delay >= 0 && Delay <= 120
		&& FMath::IsFinite(Duration) && Duration >= 0.033f && Duration <= 120
		&& FMath::IsFinite(PulseInterval) && PulseInterval >= 0.033f && PulseInterval <= 120
		&& FMath::IsFinite(DissipationSeconds) && DissipationSeconds >= 0 && DissipationSeconds <= 30;
}

bool UGuLiProjectileEffectDefinition::IsValidDefinition() const
{
	return Motion.IsValid() && !ImpactField.IsNull() && FMath::IsFinite(VisualScale) && VisualScale > 0
		&& FMath::IsFinite(TrailFadeSeconds) && TrailFadeSeconds >= 0 && TrailFadeSeconds <= 10;
}

const FGuLiWeaponEffectMount* UGuLiCombatEffectCatalog::FindMount(const int32 UnitTypeId, const FName SlotId) const
{
	return Mounts.FindByPredicate([&](const FGuLiWeaponEffectMount& Mount)
	{
		return Mount.UnitTypeId == UnitTypeId && Mount.SlotId == SlotId;
	});
}
