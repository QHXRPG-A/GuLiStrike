#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "Gameplay/Data/Generated/GuLiStrikeSecondaryWeaponsTableRows.h"
#include "Gameplay/Data/Generated/GuLiStrikeMechTableRows.h"

namespace
{
bool IsGroundSkill(const FDataTableRowHandle& Handle)
{
	return Handle.DataTable && Handle.DataTable->GetRowStruct() == FGuLiStrikeMechSkillsRow::StaticStruct();
}
}

bool UGuLiSpellFieldDefinition::IsValidDefinition() const
{
	return FMath::IsFinite(Radius) && Radius > 0 && Radius <= 100000
		&& FMath::IsFinite(VisualReferenceRadius) && VisualReferenceRadius > 0
		&& FMath::IsFinite(Delay) && Delay >= 0 && Delay <= 120
		&& FMath::IsFinite(Duration) && Duration >= 0.033f && Duration <= 120
		&& FMath::IsFinite(PulseInterval) && PulseInterval >= 0.033f && PulseInterval <= 120
		&& FMath::IsFinite(DissipationSeconds) && DissipationSeconds >= 0 && DissipationSeconds <= 30;
}

bool FGuLiProjectileVisualSettings::IsValid() const
{
	return FMath::IsFinite(SmokeInitialWidthCentimeters) && SmokeInitialWidthCentimeters > 0
		&& FMath::IsFinite(SmokeMaximumWidthCentimeters) && SmokeMaximumWidthCentimeters >= SmokeInitialWidthCentimeters
		&& FMath::IsFinite(FlameWidthCentimeters) && FlameWidthCentimeters > 0
		&& FMath::IsFinite(FlameLengthCentimeters) && FlameLengthCentimeters > 0;
}

bool UGuLiProjectileEffectDefinition::UsesMissileClusterRendering() const
{
	if (MotionProfileRow.IsNull() || IsGroundSkill(MotionProfileRow)) return false;
	const auto* Row = MotionProfileRow.GetRow<FGuLiStrikeSecondaryWeaponsProjectilesRow>(TEXT("Projectile presentation routing"));
	return Row && Row->UnitTypeId == 2 && Row->SlotId == TEXT("MissileLauncher")
		&& Row->ProjectileAsset.ToSoftObjectPath() == FSoftObjectPath(this);
}

bool UGuLiProjectileEffectDefinition::ResolveVisualSettings(FGuLiProjectileVisualSettings& OutVisual) const
{
	OutVisual = {};
	if (MotionProfileRow.IsNull() || IsGroundSkill(MotionProfileRow)) return false;
	const auto* Row = MotionProfileRow.GetRow<FGuLiStrikeSecondaryWeaponsProjectilesRow>(TEXT("Secondary weapon visuals"));
	if (!Row || Row->ProjectileAsset.ToSoftObjectPath() != FSoftObjectPath(this)) return false;
	OutVisual.SmokeInitialWidthCentimeters = Row->SmokeInitialWidthCentimeters;
	OutVisual.SmokeMaximumWidthCentimeters = Row->SmokeMaximumWidthCentimeters;
	OutVisual.FlameWidthCentimeters = Row->FlameWidthCentimeters;
	OutVisual.FlameLengthCentimeters = Row->FlameLengthCentimeters;
	return OutVisual.IsValid();
}

bool UGuLiProjectileEffectDefinition::IsValidDefinition() const
{
	FGuLiProjectileMotionSettings ResolvedMotion;
	return ResolveMotionSettings(ResolvedMotion) && !ImpactField.IsNull()
		&& FMath::IsFinite(TrailFadeSeconds) && TrailFadeSeconds >= 0 && TrailFadeSeconds <= 10;
}

FGuLiGroundStrikeFlight UGuLiProjectileEffectDefinition::GetGroundStrikeFlight() const
{
	FGuLiGroundStrikeFlight Flight;
	if (IsGroundSkill(MotionProfileRow))
	{
		const auto* Row = MotionProfileRow.GetRow<FGuLiStrikeMechSkillsRow>(TEXT("Ground strike flight"));
		if (Row && Row->ExecutionType == TEXT("Barrage"))
		{
			Flight.HorizontalRange = Row->HorizontalRangeCentimeters;
			Flight.DiveAcceleration = Row->DiveAccelerationCmPerSecondSquared;
		}
	}
	return Flight;
}

bool UGuLiProjectileEffectDefinition::ResolveMotionSettings(FGuLiProjectileMotionSettings& OutMotion) const
{
	OutMotion = {};
	if (MotionProfileRow.IsNull())
	{
		OutMotion = Motion;
		return OutMotion.IsValid();
	}
	if (IsGroundSkill(MotionProfileRow))
	{
		const auto* Row = MotionProfileRow.GetRow<FGuLiStrikeMechSkillsRow>(TEXT("Ground projectile motion"));
		if (!Row || Row->ExecutionType != TEXT("Barrage") || Row->BulletVfxId != FlightVfxId
			|| !FMath::IsFinite(Row->HorizontalRangeCentimeters) || Row->HorizontalRangeCentimeters <= 0.f
			|| !FMath::IsFinite(Row->DiveAccelerationCmPerSecondSquared) || Row->DiveAccelerationCmPerSecondSquared <= 0.f) return false;
		OutMotion = Motion;
		OutMotion.Speed = Row->ProjectileSpeed;
		OutMotion.SweepRadius = Row->SweepRadius;
		OutMotion.MaximumLifetime = Row->ProjectileLifetime;
		return OutMotion.IsValid();
	}
	const auto* Row = MotionProfileRow.GetRow<FGuLiStrikeSecondaryWeaponsProjectilesRow>(TEXT("Secondary weapon projectile"));
	if (!Row || Row->ProjectileAsset.ToSoftObjectPath() != FSoftObjectPath(this)) return false;
	OutMotion.Speed = Row->SpeedCentimetersPerSecond;
	OutMotion.LiftSeconds = Row->LiftSeconds;
	OutMotion.MinimumLiftHeight = Row->MinimumLiftHeightCentimeters;
	OutMotion.MaximumLiftHeight = Row->MaximumLiftHeightCentimeters;
	OutMotion.LateralOffset = Row->LateralOffsetCentimeters;
	OutMotion.VerticalCurve = Row->VerticalCurveCentimeters;
	OutMotion.LongitudinalCurve = Row->LongitudinalCurveCentimeters;
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
