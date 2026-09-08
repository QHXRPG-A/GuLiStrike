// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/Abilities/GuLiShipAbilityDefinitions.h"
#include "Gameplay/Data/Generated/GuLiStrikeShipTableRows.h"

#include "Gameplay/Ship/Abilities/GuLiShipAbilityTypes.h"

namespace
{
	bool Fail(FString* OutError, const TCHAR* Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
		return false;
	}

	bool IsFinitePositive(const float Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0f;
	}

	bool IsFiniteNonNegative(const float Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.0f;
	}
}

UGuLiWingmanFormationDefinition::UGuLiWingmanFormationDefinition() = default;

bool UGuLiWingmanFormationDefinition::IsWellFormed(FString* OutError) const
{
	if (Revision == 0u)
	{
		return Fail(OutError, TEXT("Formation definition revision must be nonzero."));
	}
	if (ExpectedWingmanCount == 0u || ExpectedWingmanCount > 25u)
	{
		return Fail(OutError, TEXT("Formation ExpectedWingmanCount must be in [1, 25]."));
	}
	if (FlightCount == 0u || FlightCount > ExpectedWingmanCount)
	{
		return Fail(OutError, TEXT("Formation FlightCount must be in [1, ExpectedWingmanCount]."));
	}
	if (ExpectedWingmanCount != 25u || FlightCount != 5u)
	{
		return Fail(OutError, TEXT("The current Wingman protocol requires exactly 25 members in five Flights."));
	}
	if (GuidanceAlgorithmVersion == 0u)
	{
		return Fail(OutError, TEXT("Formation GuidanceAlgorithmVersion must be nonzero."));
	}
	if (Model == EGuLiWingmanFormationModel::DoubleRingLegacy)
	{
		if (static_cast<uint32>(InnerRingSlots) + static_cast<uint32>(OuterRingSlots) != ExpectedWingmanCount)
		{
			return Fail(OutError, TEXT("Double-ring slot counts must equal ExpectedWingmanCount."));
		}
		if (!IsFinitePositive(InnerRingRadiusCentimeters)
			|| !IsFinitePositive(OuterRingRadiusCentimeters)
			|| OuterRingRadiusCentimeters <= InnerRingRadiusCentimeters)
		{
			return Fail(OutError, TEXT("Double-ring radii must be finite, positive, and outer must exceed inner."));
		}
		if (!FMath::IsFinite(InnerRingHeightCentimeters)
			|| !FMath::IsFinite(OuterRingHeightCentimeters)
			|| !IsFinitePositive(InnerAngularSpeedRadiansPerSecond)
			|| !IsFinitePositive(OuterAngularSpeedRadiansPerSecond))
		{
			return Fail(OutError, TEXT("Double-ring heights must be finite and angular speeds must be positive."));
		}
	}
	else if (Model == EGuLiWingmanFormationModel::SwarmOrbit)
	{
		if (!SwarmOrbit.IsWellFormed())
		{
			return Fail(OutError, TEXT("SwarmOrbit tuning is outside its finite, ordered domain."));
		}
	}
	else
	{
		return Fail(OutError, TEXT("Formation model is unknown."));
	}
	if (!IsFinitePositive(MinimumFlightSpeedCentimetersPerSecond)
		|| !IsFinitePositive(CruiseFlightSpeedCentimetersPerSecond)
		|| !IsFinitePositive(CatchUpFlightSpeedCentimetersPerSecond)
		|| MinimumFlightSpeedCentimetersPerSecond > CruiseFlightSpeedCentimetersPerSecond
		|| CruiseFlightSpeedCentimetersPerSecond > CatchUpFlightSpeedCentimetersPerSecond)
	{
		return Fail(OutError, TEXT("Formation speeds must be finite, positive, and ordered minimum <= cruise <= catch-up."));
	}
	if (!IsFinitePositive(MaximumTurnRateDegreesPerSecond) || MaximumTurnRateDegreesPerSecond > 180.0f
		|| !IsFinitePositive(MaximumAccelerationCentimetersPerSecondSquared)
		|| !IsFinitePositive(MaximumDecelerationCentimetersPerSecondSquared)
		|| !IsFinitePositive(MaximumBankDegrees) || MaximumBankDegrees > 90.0f)
	{
		return Fail(OutError, TEXT("Flight acceleration, deceleration, turn rate and bank limits are invalid."));
	}
	if (!IsFinitePositive(AgentRadiusCentimeters)
		|| !IsFinitePositive(SeparationRadiusCentimeters)
		|| SeparationRadiusCentimeters < AgentRadiusCentimeters * 2.0f
		|| !IsFinitePositive(ObstacleLookAheadCentimeters))
	{
		return Fail(OutError, TEXT("Formation avoidance distances must be finite and positive."));
	}
	if (!IsFinitePositive(CatchUpDistanceCentimeters)
		|| !IsFinitePositive(RecoveryDistanceCentimeters)
		|| RecoveryDistanceCentimeters <= CatchUpDistanceCentimeters)
	{
		return Fail(OutError, TEXT("Recovery distance must exceed the positive catch-up distance."));
	}
	if (Model == EGuLiWingmanFormationModel::SwarmOrbit
		&& CatchUpDistanceCentimeters <= SwarmOrbit.OuterSoftRadiusCentimeters)
	{
		return Fail(OutError, TEXT("SwarmOrbit catch-up distance must exceed its outer soft radius."));
	}
	return true;
}

bool UGuLiWingmanFormationDefinition::BuildRuntimeConfig(
	const uint32 FormationSeed,
	FGuLiWingmanFormationRuntimeConfig& OutRuntime,
	FString* OutError) const
{
	OutRuntime = FGuLiWingmanFormationRuntimeConfig();
	if (FormationSeed == 0u)
	{
		return Fail(OutError, TEXT("FormationSeed must be nonzero."));
	}
	if (!IsWellFormed(OutError))
	{
		return false;
	}

	OutRuntime.Model = Model;
	OutRuntime.GuidanceAlgorithmVersion = GuidanceAlgorithmVersion;
	OutRuntime.FormationSeed = FormationSeed;
	OutRuntime.SwarmOrbit = SwarmOrbit;
	OutRuntime.InnerRingSlots = InnerRingSlots;
	OutRuntime.OuterRingSlots = OuterRingSlots;
	OutRuntime.InnerRingRadiusCentimeters = InnerRingRadiusCentimeters;
	OutRuntime.OuterRingRadiusCentimeters = OuterRingRadiusCentimeters;
	OutRuntime.InnerRingHeightCentimeters = InnerRingHeightCentimeters;
	OutRuntime.OuterRingHeightCentimeters = OuterRingHeightCentimeters;
	OutRuntime.InnerAngularSpeedRadiansPerSecond = InnerAngularSpeedRadiansPerSecond;
	OutRuntime.OuterAngularSpeedRadiansPerSecond = OuterAngularSpeedRadiansPerSecond;
	OutRuntime.MinimumSpeedCentimetersPerSecond = MinimumFlightSpeedCentimetersPerSecond;
	OutRuntime.CruiseSpeedCentimetersPerSecond = CruiseFlightSpeedCentimetersPerSecond;
	OutRuntime.CatchUpSpeedCentimetersPerSecond = CatchUpFlightSpeedCentimetersPerSecond;
	OutRuntime.MaximumAccelerationCentimetersPerSecondSquared =
		MaximumAccelerationCentimetersPerSecondSquared;
	OutRuntime.MaximumDecelerationCentimetersPerSecondSquared =
		MaximumDecelerationCentimetersPerSecondSquared;
	OutRuntime.MaximumTurnRateDegreesPerSecond = MaximumTurnRateDegreesPerSecond;
	OutRuntime.MaximumBankDegrees = MaximumBankDegrees;
	OutRuntime.AgentRadiusCentimeters = AgentRadiusCentimeters;
	OutRuntime.SeparationRadiusCentimeters = SeparationRadiusCentimeters;
	OutRuntime.ObstacleLookAheadCentimeters = ObstacleLookAheadCentimeters;
	OutRuntime.CatchUpDistanceCentimeters = CatchUpDistanceCentimeters;
	OutRuntime.RecoveryDistanceCentimeters = RecoveryDistanceCentimeters;
	if (!OutRuntime.IsWellFormed())
	{
		return Fail(OutError, TEXT("Formation definition produced an invalid runtime projection."));
	}
	return true;
}

uint64 UGuLiWingmanFormationDefinition::ComputeStableChecksum() const
{
	if (!IsWellFormed())
	{
		return 0u;
	}

	uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
	GuLiShipAbilityHash::AddString(Hash, TEXT("GuLi.WingmanFormation.v2"));
	GuLiShipAbilityHash::AddUInt32(Hash, Revision);
	GuLiShipAbilityHash::AddUInt32(Hash, ExpectedWingmanCount);
	GuLiShipAbilityHash::AddUInt32(Hash, FlightCount);
	GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Model));
	GuLiShipAbilityHash::AddUInt32(Hash, GuidanceAlgorithmVersion);
	if (Model == EGuLiWingmanFormationModel::DoubleRingLegacy)
	{
		GuLiShipAbilityHash::AddUInt32(Hash, InnerRingSlots);
		GuLiShipAbilityHash::AddUInt32(Hash, OuterRingSlots);
		GuLiShipAbilityHash::AddFloat(Hash, InnerRingRadiusCentimeters);
		GuLiShipAbilityHash::AddFloat(Hash, OuterRingRadiusCentimeters);
		GuLiShipAbilityHash::AddFloat(Hash, InnerRingHeightCentimeters);
		GuLiShipAbilityHash::AddFloat(Hash, OuterRingHeightCentimeters);
		GuLiShipAbilityHash::AddFloat(Hash, InnerAngularSpeedRadiansPerSecond);
		GuLiShipAbilityHash::AddFloat(Hash, OuterAngularSpeedRadiansPerSecond);
	}
	else
	{
		SwarmOrbit.AddToStableHash(Hash);
	}
	GuLiShipAbilityHash::AddFloat(Hash, MinimumFlightSpeedCentimetersPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, CruiseFlightSpeedCentimetersPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, CatchUpFlightSpeedCentimetersPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, MaximumTurnRateDegreesPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, MaximumAccelerationCentimetersPerSecondSquared);
	GuLiShipAbilityHash::AddFloat(Hash, MaximumDecelerationCentimetersPerSecondSquared);
	GuLiShipAbilityHash::AddFloat(Hash, MaximumBankDegrees);
	GuLiShipAbilityHash::AddFloat(Hash, AgentRadiusCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, SeparationRadiusCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, ObstacleLookAheadCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, CatchUpDistanceCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, RecoveryDistanceCentimeters);
	return GuLiShipAbilityHash::Finish(Hash);
}

UGuLiWingmanWeaponDefinition::UGuLiWingmanWeaponDefinition() = default;

bool UGuLiWingmanWeaponDefinition::BuildRuntimeConfig(FGuLiWingmanWeaponRuntimeConfig& Out, FString* OutError) const
{
	Out = {};
	Out.Damage = Damage; Out.RangeCentimeters = RangeCentimeters; Out.CooldownSeconds = CooldownSeconds;
	Out.TargetConeHalfAngleDegrees = TargetConeHalfAngleDegrees; Out.bRequiresLineOfSight = bRequiresLineOfSight;
	Out.ProjectileSpeedCentimetersPerSecond = ProjectileSpeedCentimetersPerSecond;
	Out.ProjectileLifetimeSeconds = ProjectileLifetimeSeconds; Out.SweepRadiusCentimeters = SweepRadiusCentimeters;
	Out.MaximumHomingTurnRateDegreesPerSecond = MaximumHomingTurnRateDegreesPerSecond; Out.Attack = Attack;
	if (!AttackProfileRow.IsNull())
	{
		const auto* Row = AttackProfileRow.GetRow<FGuLiStrikeShipWingmanWeaponsRow>(TEXT("Wingman attack profile"));
		if (!Row) return Fail(OutError, TEXT("Wingman weapon requires its authored Ship table row."));
		if (Row->AttackPattern == TEXT("AirDogfight")) Out.Attack.Pattern = EGuLiWingmanAttackPattern::AirDogfight;
		else if (Row->AttackPattern == TEXT("GroundDive")) Out.Attack.Pattern = EGuLiWingmanAttackPattern::GroundDive;
		else return Fail(OutError, TEXT("Unknown Wingman attack pattern in source table."));
		Out.Attack.ExecutorId = FName(*Row->ExecutorId); Out.Attack.FlightSpeed = Row->FlightSpeedCentimetersPerSecond;
		Out.Attack.DiveSeconds = Row->DiveSeconds; Out.Attack.MissileCount = Row->MissileCount;
		Out.Attack.StripLength = Row->StripLengthCentimeters; Out.Attack.PullUpHeight = Row->PullUpHeightCentimeters;
		Out.Attack.ExplosionRadius = Row->ExplosionRadiusCentimeters;
		Out.Attack.BreakawayDistance = Row->BreakawayDistanceCentimeters;
		Out.Attack.RetreatMinimumDistance = Row->RetreatMinimumDistanceCentimeters;
		Out.Attack.RetreatLongitudinalMinFraction = Row->RetreatLongitudinalMinFraction;
		Out.Attack.RetreatLongitudinalMaxFraction = Row->RetreatLongitudinalMaxFraction;
		Out.Attack.RetreatLateralRadius = Row->RetreatLateralRadiusCentimeters;
		Out.Attack.RetreatVerticalRadius = Row->RetreatVerticalRadiusCentimeters;
		Out.Attack.ManeuverArrivalRadius = Row->ManeuverArrivalRadiusCentimeters;
		Out.Attack.TurnYawMinDegrees = Row->TurnYawMinDegrees;
		Out.Attack.TurnYawMaxDegrees = Row->TurnYawMaxDegrees;
		Out.Attack.TurnPitchMaxDegrees = Row->TurnPitchMaxDegrees; Out.Attack.Muzzle = Row->Muzzle;
		Out.Damage = Row->Damage; Out.CooldownSeconds = Row->CooldownSeconds; Out.RangeCentimeters = Row->RangeCentimeters;
		Out.TargetConeHalfAngleDegrees = Row->FireConeHalfAngleDegrees;
		Out.ProjectileSpeedCentimetersPerSecond = Row->ProjectileSpeedCentimetersPerSecond;
		Out.ProjectileLifetimeSeconds = Row->ProjectileLifetimeSeconds; Out.SweepRadiusCentimeters = Row->SweepRadiusCentimeters;
	}
	return Out.IsWellFormed() || Fail(OutError, TEXT("Wingman attack row contains invalid values or exceeds the bounded fire batch."));
}

bool UGuLiWingmanWeaponDefinition::IsWellFormed(FString* OutError) const
{
	if (Revision == 0u)
	{
		return Fail(OutError, TEXT("Weapon definition revision must be nonzero."));
	}
	if (!AttackProfileRow.IsNull() || Attack.Pattern != EGuLiWingmanAttackPattern::Legacy)
	{
		FGuLiWingmanWeaponRuntimeConfig Runtime;
		return Kind == EGuLiWingmanWeaponKind::BasicAutomatic && BuildRuntimeConfig(Runtime, OutError);
	}
	if (!IsFiniteNonNegative(Damage)
		|| !IsFinitePositive(RangeCentimeters)
		|| !IsFinitePositive(CooldownSeconds)
		|| !IsFinitePositive(ProjectileSpeedCentimetersPerSecond)
		|| !IsFinitePositive(ProjectileLifetimeSeconds)
		|| !IsFiniteNonNegative(SweepRadiusCentimeters))
	{
		return Fail(OutError, TEXT("Weapon numeric fields must be finite and inside their positive/non-negative domains."));
	}
	if (!IsFinitePositive(TargetConeHalfAngleDegrees) || TargetConeHalfAngleDegrees > 180.0f)
	{
		return Fail(OutError, TEXT("Weapon target cone half angle must be in (0, 180]."));
	}
	if (!IsFiniteNonNegative(MaximumHomingTurnRateDegreesPerSecond)
		|| MaximumHomingTurnRateDegreesPerSecond > 180.0f)
	{
		return Fail(OutError, TEXT("Weapon homing turn rate must be finite and in [0, 180]."));
	}
	if (Kind == EGuLiWingmanWeaponKind::Missile && MaximumHomingTurnRateDegreesPerSecond <= 0.0f)
	{
		return Fail(OutError, TEXT("Missile definitions require a positive homing turn rate."));
	}
	if (Kind == EGuLiWingmanWeaponKind::BasicAutomatic && MaximumHomingTurnRateDegreesPerSecond != 0.0f)
	{
		return Fail(OutError, TEXT("Basic automatic weapon definitions cannot enable missile homing."));
	}
	return true;
}

uint64 UGuLiWingmanWeaponDefinition::ComputeStableChecksum() const
{
	if (!IsWellFormed())
	{
		return 0u;
	}

	uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
	GuLiShipAbilityHash::AddString(Hash, TEXT("GuLi.WingmanWeapon.v1"));
	GuLiShipAbilityHash::AddUInt32(Hash, Revision);
	GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Kind));
	GuLiShipAbilityHash::AddFloat(Hash, Damage);
	GuLiShipAbilityHash::AddFloat(Hash, RangeCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, CooldownSeconds);
	GuLiShipAbilityHash::AddFloat(Hash, ProjectileSpeedCentimetersPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, ProjectileLifetimeSeconds);
	GuLiShipAbilityHash::AddFloat(Hash, SweepRadiusCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, TargetConeHalfAngleDegrees);
	GuLiShipAbilityHash::AddBool(Hash, bRequiresLineOfSight);
	GuLiShipAbilityHash::AddFloat(Hash, MaximumHomingTurnRateDegreesPerSecond);
	FGuLiWingmanWeaponRuntimeConfig Resolved;
	if (!BuildRuntimeConfig(Resolved)) return 0u;
	Resolved.AddToStableHash(Hash);
	GuLiShipAbilityHash::AddString(Hash, AttackProjectile.ToSoftObjectPath().ToString());
	return GuLiShipAbilityHash::Finish(Hash);
}
