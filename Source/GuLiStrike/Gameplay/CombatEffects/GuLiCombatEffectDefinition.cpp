#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "Gameplay/Data/Generated/GuLiStrikeSecondaryWeaponsTableRows.h"

bool UGuLiSpellFieldDefinition::IsValidDefinition() const
{
	return FMath::IsFinite(Radius) && Radius > 0 && Radius <= 100000
		&& FMath::IsFinite(VisualReferenceRadius) && VisualReferenceRadius > 0
		&& FMath::IsFinite(Delay) && Delay >= 0 && Delay <= 120
		&& FMath::IsFinite(Duration) && Duration >= 0.033f && Duration <= 120
		&& FMath::IsFinite(PulseInterval) && PulseInterval >= 0.033f && PulseInterval <= 120
		&& FMath::IsFinite(DissipationSeconds) && DissipationSeconds >= 0 && DissipationSeconds <= 30;
}

bool UGuLiProjectileEffectDefinition::IsValidDefinition() const
{
	FGuLiProjectileMotionSettings ResolvedMotion;
	return ResolveMotionSettings(ResolvedMotion) && !ImpactField.IsNull() && FMath::IsFinite(VisualScale) && VisualScale > 0
		&& FMath::IsFinite(TrailFadeSeconds) && TrailFadeSeconds >= 0 && TrailFadeSeconds <= 10;
}

bool UGuLiProjectileEffectDefinition::ResolveMotionSettings(FGuLiProjectileMotionSettings& OutMotion) const
{
	OutMotion = {};
	if (MotionProfileRow.IsNull())
	{
		OutMotion = Motion;
		return OutMotion.IsValid();
	}
	const auto* Row = MotionProfileRow.GetRow<FGuLiStrikeSecondaryWeaponsProjectilesRow>(TEXT("Secondary weapon projectile"));
	if (!Row || Row->ProjectileAsset.ToSoftObjectPath() != FSoftObjectPath(this)) return false;
	OutMotion.Speed = Row->SpeedCentimetersPerSecond;
	OutMotion.LiftSeconds = Row->LiftSeconds;
	OutMotion.MinimumLiftHeight = Row->MinimumLiftHeightCentimeters;
	OutMotion.MaximumLiftHeight = Row->MaximumLiftHeightCentimeters;
	OutMotion.LateralOffset = Row->LateralOffsetCentimeters;
	OutMotion.ConvergenceDistance = Row->ConvergenceDistanceCentimeters;
	OutMotion.TurnRate = Row->TurnRateDegreesPerSecond;
	OutMotion.SweepRadius = Row->SweepRadiusCentimeters;
	OutMotion.MaximumLifetime = Row->MaximumLifetimeSeconds;
	return OutMotion.IsValid();
}

const FGuLiWeaponEffectMount* UGuLiCombatEffectCatalog::FindMount(const int32 UnitTypeId, const FName SlotId) const
{
	return Mounts.FindByPredicate([&](const FGuLiWeaponEffectMount& Mount)
	{
		return Mount.UnitTypeId == UnitTypeId && Mount.SlotId == SlotId;
	});
}
