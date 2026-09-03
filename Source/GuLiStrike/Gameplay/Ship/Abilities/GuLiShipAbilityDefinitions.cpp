// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/Abilities/GuLiShipAbilityDefinitions.h"

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
	return true;
}

uint64 UGuLiWingmanFormationDefinition::ComputeStableChecksum() const
{
	if (!IsWellFormed())
	{
		return 0u;
	}

	uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
	GuLiShipAbilityHash::AddString(Hash, TEXT("GuLi.WingmanFormation.DoubleRing.v1"));
	GuLiShipAbilityHash::AddUInt32(Hash, Revision);
	GuLiShipAbilityHash::AddUInt32(Hash, ExpectedWingmanCount);
	GuLiShipAbilityHash::AddUInt32(Hash, FlightCount);
	GuLiShipAbilityHash::AddUInt32(Hash, InnerRingSlots);
	GuLiShipAbilityHash::AddUInt32(Hash, OuterRingSlots);
	GuLiShipAbilityHash::AddFloat(Hash, InnerRingRadiusCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, OuterRingRadiusCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, InnerRingHeightCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, OuterRingHeightCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, InnerAngularSpeedRadiansPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, OuterAngularSpeedRadiansPerSecond);
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

bool UGuLiWingmanWeaponDefinition::IsWellFormed(FString* OutError) const
{
	if (Revision == 0u)
	{
		return Fail(OutError, TEXT("Weapon definition revision must be nonzero."));
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
	return GuLiShipAbilityHash::Finish(Hash);
}
