// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/Abilities/GuLiShipAbilityTypes.h"

#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"

namespace
{
	void SetError(FString* OutError, const TCHAR* Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
	}

	void AddByte(uint64& Hash, const uint8 Byte)
	{
		Hash ^= Byte;
		Hash *= GuLiShipAbilityHash::Prime;
	}
}

FGameplayTag GuLiGetShipAbilitySlotTag(const EGuLiShipAbilitySlot Slot)
{
	switch (Slot)
	{
	case EGuLiShipAbilitySlot::Formation:
		return TAG_GuLi_ShipWingman_Formation;
	case EGuLiShipAbilitySlot::BasicWeapon:
		return TAG_GuLi_ShipWingman_Weapon_Basic;
	case EGuLiShipAbilitySlot::Missile:
		return TAG_GuLi_ShipWingman_Weapon_Missile;
	default:
		return FGameplayTag();
	}
}

bool GuLiIsPersistentShipAbilitySlot(const EGuLiShipAbilitySlot Slot)
{
	return Slot == EGuLiShipAbilitySlot::Formation || Slot == EGuLiShipAbilitySlot::BasicWeapon;
}

FName GuLiGetDefaultWingmanTypeId()
{
	static const FName Name(TEXT("DefaultWingman"));
	return Name;
}

FName GuLiGetDefaultWeaponSlotId(const EGuLiShipAbilitySlot Slot)
{
	switch (Slot)
	{
	case EGuLiShipAbilitySlot::BasicWeapon:
		return FName(TEXT("BasicWeapon"));
	case EGuLiShipAbilitySlot::Missile:
		return FName(TEXT("Missile"));
	default:
		return NAME_None;
	}
}

FName GuLiGetDefaultWeaponSkillId(const FGameplayTag AbilityId)
{
	return AbilityId.IsValid() ? AbilityId.GetTagName() : NAME_None;
}

void FGuLiShipAbilityLoadoutState::Normalize()
{
	AbilityIds.RemoveAll([](const FGameplayTag Tag)
	{
		return !Tag.IsValid();
	});

	AbilityIds.Sort([](const FGameplayTag Lhs, const FGameplayTag Rhs)
	{
		return Lhs.GetTagName().LexicalLess(Rhs.GetTagName());
	});

	for (int32 Index = AbilityIds.Num() - 1; Index > 0; --Index)
	{
		if (AbilityIds[Index] == AbilityIds[Index - 1])
		{
			AbilityIds.RemoveAt(Index, 1, EAllowShrinking::No);
		}
	}

	Revision = FMath::Max(Revision, 1u);
}

bool FGuLiShipAbilityLoadoutState::IsWellFormed(FString* OutError) const
{
	if (Revision == 0u)
	{
		SetError(OutError, TEXT("Ship ability loadout revision must be nonzero."));
		return false;
	}

	if (AbilityIds.IsEmpty())
	{
		SetError(OutError, TEXT("Ship ability loadout is empty."));
		return false;
	}

	TSet<FGameplayTag> Seen;
	for (const FGameplayTag AbilityId : AbilityIds)
	{
		if (!AbilityId.IsValid())
		{
			SetError(OutError, TEXT("Ship ability loadout contains an invalid AbilityId."));
			return false;
		}
		if (Seen.Contains(AbilityId))
		{
			SetError(OutError, TEXT("Ship ability loadout contains a duplicate AbilityId."));
			return false;
		}
		Seen.Add(AbilityId);
	}

	return true;
}

bool FGuLiShipAbilityLoadoutState::Contains(const FGameplayTag AbilityId) const
{
	return AbilityIds.Contains(AbilityId);
}

bool FGuLiShipAbilityLoadoutState::HasSameSelection(const FGuLiShipAbilityLoadoutState& Other) const
{
	FGuLiShipAbilityLoadoutState Lhs = *this;
	FGuLiShipAbilityLoadoutState Rhs = Other;
	Lhs.Normalize();
	Rhs.Normalize();
	return Lhs.AbilityIds == Rhs.AbilityIds;
}

FGuLiShipAbilityLoadoutState FGuLiShipAbilityLoadoutState::MakeNativeV1()
{
	FGuLiShipAbilityLoadoutState Result;
	Result.AbilityIds = {
		TAG_GuLi_ShipAbility_Formation_DoubleRing,
		TAG_GuLi_ShipAbility_Weapon_Basic_Auto,
		TAG_GuLi_ShipAbility_Weapon_Missile_Salvo
	};
	Result.Normalize();
	return Result;
}

FGuLiShipAbilityLoadoutState FGuLiShipAbilityLoadoutState::MakeNativeV3()
{
	FGuLiShipAbilityLoadoutState Result;
	Result.Revision = 3u;
	Result.AbilityIds = { TAG_GuLi_ShipAbility_Formation_SwarmOrbit,
		TAG_GuLi_ShipAbility_Weapon_Wingman_MachineGun, TAG_GuLi_ShipAbility_Weapon_Wingman_GroundMissile };
	Result.Normalize();
	return Result;
}

FGuLiShipAbilityLoadoutState FGuLiShipAbilityLoadoutState::MakeNativeV2()
{
	FGuLiShipAbilityLoadoutState Result;
	Result.Revision = 2u;
	Result.AbilityIds = {
		TAG_GuLi_ShipAbility_Formation_SwarmOrbit,
		TAG_GuLi_ShipAbility_Weapon_Basic_Auto,
		TAG_GuLi_ShipAbility_Weapon_Missile_Salvo
	};
	Result.Normalize();
	return Result;
}

bool FGuLiWingmanSwarmOrbitTuning::IsWellFormed() const
{
	const auto PositiveFinite = [](const float Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0f;
	};
	const auto UnitFinite = [](const float Value)
	{
		return FMath::IsFinite(Value) && Value >= 0.0f && Value <= 1.0f;
	};
	return PositiveFinite(InnerSoftRadiusCentimeters)
		&& PositiveFinite(OuterSoftRadiusCentimeters)
		&& OuterSoftRadiusCentimeters > InnerSoftRadiusCentimeters
		&& PositiveFinite(VerticalHalfExtentCentimeters)
		&& PositiveFinite(HullExclusionRadiusCentimeters)
		&& HullExclusionRadiusCentimeters < InnerSoftRadiusCentimeters
		&& PositiveFinite(SwirlSpeedMinCentimetersPerSecond)
		&& PositiveFinite(SwirlSpeedMaxCentimetersPerSecond)
		&& SwirlSpeedMaxCentimetersPerSecond >= SwirlSpeedMinCentimetersPerSecond
		&& FMath::IsFinite(CurlStrengthCentimetersPerSecond)
		&& CurlStrengthCentimetersPerSecond >= 0.0f
		&& PositiveFinite(NoiseSpatialScaleCentimeters)
		&& PositiveFinite(NoiseTemporalScaleSeconds)
		&& UnitFinite(AxisPrecessionAmount)
		&& FMath::IsFinite(AxisPrecessionRadiansPerSecond)
		&& AxisPrecessionRadiansPerSecond >= 0.0f
		&& PositiveFinite(BoundaryReturnSpeedCentimetersPerSecond)
		&& FMath::IsFinite(PreferredRadiusReturnSpeedCentimetersPerSecond)
		&& PreferredRadiusReturnSpeedCentimetersPerSecond >= 0.0f
		&& PositiveFinite(VerticalReturnSpeedCentimetersPerSecond)
		&& UnitFinite(AlignmentWeight)
		&& UnitFinite(CatchUpStyleWeight)
		&& UnitFinite(RecoveryStyleWeight)
		&& RecoveryStyleWeight <= CatchUpStyleWeight
		&& PositiveFinite(ResponseTimeSeconds);
}

void FGuLiWingmanSwarmOrbitTuning::AddToStableHash(uint64& Hash) const
{
	GuLiShipAbilityHash::AddFloat(Hash, InnerSoftRadiusCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, OuterSoftRadiusCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, VerticalHalfExtentCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, HullExclusionRadiusCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, SwirlSpeedMinCentimetersPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, SwirlSpeedMaxCentimetersPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, CurlStrengthCentimetersPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, NoiseSpatialScaleCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, NoiseTemporalScaleSeconds);
	GuLiShipAbilityHash::AddFloat(Hash, AxisPrecessionAmount);
	GuLiShipAbilityHash::AddFloat(Hash, AxisPrecessionRadiansPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, BoundaryReturnSpeedCentimetersPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, PreferredRadiusReturnSpeedCentimetersPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, VerticalReturnSpeedCentimetersPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, AlignmentWeight);
	GuLiShipAbilityHash::AddFloat(Hash, CatchUpStyleWeight);
	GuLiShipAbilityHash::AddFloat(Hash, RecoveryStyleWeight);
	GuLiShipAbilityHash::AddFloat(Hash, ResponseTimeSeconds);
}

bool FGuLiWingmanFormationRuntimeConfig::IsWellFormed() const
{
	const auto PositiveFinite = [](const float Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0f;
	};
	const bool bCommonValid = GuidanceAlgorithmVersion != 0u && FormationSeed != 0u
		&& PositiveFinite(MinimumSpeedCentimetersPerSecond)
		&& PositiveFinite(CruiseSpeedCentimetersPerSecond)
		&& PositiveFinite(CatchUpSpeedCentimetersPerSecond)
		&& MinimumSpeedCentimetersPerSecond <= CruiseSpeedCentimetersPerSecond
		&& CruiseSpeedCentimetersPerSecond <= CatchUpSpeedCentimetersPerSecond
		&& PositiveFinite(MaximumAccelerationCentimetersPerSecondSquared)
		&& PositiveFinite(MaximumDecelerationCentimetersPerSecondSquared)
		&& PositiveFinite(MaximumTurnRateDegreesPerSecond)
		&& MaximumTurnRateDegreesPerSecond <= 180.0f
		&& PositiveFinite(MaximumBankDegrees) && MaximumBankDegrees <= 90.0f
		&& PositiveFinite(AgentRadiusCentimeters)
		&& PositiveFinite(SeparationRadiusCentimeters)
		&& SeparationRadiusCentimeters >= AgentRadiusCentimeters * 2.0f
		&& PositiveFinite(ObstacleLookAheadCentimeters)
		&& PositiveFinite(CatchUpDistanceCentimeters)
		&& PositiveFinite(RecoveryDistanceCentimeters)
		&& RecoveryDistanceCentimeters > CatchUpDistanceCentimeters;
	if (!bCommonValid)
	{
		return false;
	}

	switch (Model)
	{
	case EGuLiWingmanFormationModel::DoubleRingLegacy:
		return InnerRingSlots > 0u && OuterRingSlots > 0u
			&& static_cast<uint32>(InnerRingSlots) + static_cast<uint32>(OuterRingSlots) == 25u
			&& PositiveFinite(InnerRingRadiusCentimeters)
			&& PositiveFinite(OuterRingRadiusCentimeters)
			&& OuterRingRadiusCentimeters > InnerRingRadiusCentimeters
			&& FMath::IsFinite(InnerRingHeightCentimeters)
			&& FMath::IsFinite(OuterRingHeightCentimeters)
			&& PositiveFinite(InnerAngularSpeedRadiansPerSecond)
			&& PositiveFinite(OuterAngularSpeedRadiansPerSecond);
	case EGuLiWingmanFormationModel::SwarmOrbit:
		return SwarmOrbit.IsWellFormed()
			&& CatchUpDistanceCentimeters > SwarmOrbit.OuterSoftRadiusCentimeters
			&& RecoveryDistanceCentimeters > SwarmOrbit.OuterSoftRadiusCentimeters;
	default:
		return false;
	}
}

void FGuLiWingmanFormationRuntimeConfig::AddToStableHash(uint64& Hash) const
{
	GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Model));
	GuLiShipAbilityHash::AddUInt32(Hash, GuidanceAlgorithmVersion);
	GuLiShipAbilityHash::AddUInt32(Hash, FormationSeed);
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
	GuLiShipAbilityHash::AddFloat(Hash, MinimumSpeedCentimetersPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, CruiseSpeedCentimetersPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, CatchUpSpeedCentimetersPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, MaximumAccelerationCentimetersPerSecondSquared);
	GuLiShipAbilityHash::AddFloat(Hash, MaximumDecelerationCentimetersPerSecondSquared);
	GuLiShipAbilityHash::AddFloat(Hash, MaximumTurnRateDegreesPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, MaximumBankDegrees);
	GuLiShipAbilityHash::AddFloat(Hash, AgentRadiusCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, SeparationRadiusCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, ObstacleLookAheadCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, CatchUpDistanceCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, RecoveryDistanceCentimeters);
}

bool FGuLiWingmanWeaponRuntimeConfig::IsWellFormed() const
{
	return Attack.IsWellFormed() && FMath::IsFinite(Damage) && Damage > 0.0f
		&& FMath::IsFinite(RangeCentimeters) && RangeCentimeters > 0.0f
		&& FMath::IsFinite(CooldownSeconds) && CooldownSeconds > 0.0f
		&& FMath::IsFinite(TargetConeHalfAngleDegrees)
		&& TargetConeHalfAngleDegrees > 0.0f && TargetConeHalfAngleDegrees <= 180.0f
		&& FMath::IsFinite(ProjectileSpeedCentimetersPerSecond)
		&& ProjectileSpeedCentimetersPerSecond > 0.0f
		&& FMath::IsFinite(ProjectileLifetimeSeconds)
		&& ProjectileLifetimeSeconds >= 0.01f && ProjectileLifetimeSeconds <= 120.0f
		&& FMath::IsFinite(SweepRadiusCentimeters) && SweepRadiusCentimeters >= 0.0f
		&& FMath::IsFinite(MaximumHomingTurnRateDegreesPerSecond)
		&& MaximumHomingTurnRateDegreesPerSecond >= 0.0f
		&& MaximumHomingTurnRateDegreesPerSecond <= 180.0f;
}

void FGuLiWingmanWeaponRuntimeConfig::AddToStableHash(uint64& Hash) const
{
	Attack.AddToStableHash(Hash);
	GuLiShipAbilityHash::AddFloat(Hash, Damage);
	GuLiShipAbilityHash::AddFloat(Hash, RangeCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, CooldownSeconds);
	GuLiShipAbilityHash::AddFloat(Hash, TargetConeHalfAngleDegrees);
	GuLiShipAbilityHash::AddBool(Hash, bRequiresLineOfSight);
	GuLiShipAbilityHash::AddFloat(Hash, ProjectileSpeedCentimetersPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, ProjectileLifetimeSeconds);
	GuLiShipAbilityHash::AddFloat(Hash, SweepRadiusCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, MaximumHomingTurnRateDegreesPerSecond);
}

bool FGuLiWingmanWeaponChannelConfig::IsWellFormed() const
{
	if (!Binding.IsWellFormed() || Binding.Domain != EGuLiWeaponDomain::Wingman)
	{
		return false;
	}
	if (!bEnabled)
	{
		return SkillId.IsNone() && !AbilityId.IsValid()
			&& ProfileRevision == 0u && DefinitionRevision == 0u
			&& DefinitionChecksum == 0u;
	}
	return !SkillId.IsNone() && AbilityId.IsValid()
		&& ProfileRevision != 0u && DefinitionRevision != 0u
		&& DefinitionChecksum != 0u && Runtime.IsWellFormed()
		&& (Kind != EGuLiWingmanWeaponKind::Missile || !CooldownGroupId.IsNone());
}

void FGuLiWingmanWeaponChannelConfig::AddToStableHash(uint64& Hash) const
{
	GuLiShipAbilityHash::AddUInt32(Hash, Binding.MatchEpoch);
	GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Binding.Team));
	GuLiShipAbilityHash::AddUInt32(Hash, Binding.OwnerPlayerGuid.A);
	GuLiShipAbilityHash::AddUInt32(Hash, Binding.OwnerPlayerGuid.B);
	GuLiShipAbilityHash::AddUInt32(Hash, Binding.OwnerPlayerGuid.C);
	GuLiShipAbilityHash::AddUInt32(Hash, Binding.OwnerPlayerGuid.D);
	GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Binding.Domain));
	GuLiShipAbilityHash::AddString(Hash, Binding.SubjectId.ToString());
	GuLiShipAbilityHash::AddString(Hash, Binding.SlotId.ToString());
	GuLiShipAbilityHash::AddString(Hash, SkillId.ToString());
	GuLiShipAbilityHash::AddTag(Hash, AbilityId);
	GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Kind));
	GuLiShipAbilityHash::AddString(Hash, CooldownGroupId.ToString());
	GuLiShipAbilityHash::AddBool(Hash, bEnabled);
	GuLiShipAbilityHash::AddUInt32(Hash, ProfileRevision);
	GuLiShipAbilityHash::AddUInt32(Hash, DefinitionRevision);
	GuLiShipAbilityHash::AddUInt64(Hash, DefinitionChecksum);
	Runtime.AddToStableHash(Hash);
}

uint64 FGuLiGroupAbilityConfigSnapshot::ComputeStableHash() const
{
	uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
	GuLiShipAbilityHash::AddUInt32(Hash, ProtocolVersion);
	GuLiShipAbilityHash::AddUInt32(Hash, ShipInstanceId.A);
	GuLiShipAbilityHash::AddUInt32(Hash, ShipInstanceId.B);
	GuLiShipAbilityHash::AddUInt32(Hash, ShipInstanceId.C);
	GuLiShipAbilityHash::AddUInt32(Hash, ShipInstanceId.D);
	GuLiShipAbilityHash::AddUInt32(Hash, MatchEpoch);
	GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Team));
	GuLiShipAbilityHash::AddUInt32(Hash, OwnerPlayerGuid.A);
	GuLiShipAbilityHash::AddUInt32(Hash, OwnerPlayerGuid.B);
	GuLiShipAbilityHash::AddUInt32(Hash, OwnerPlayerGuid.C);
	GuLiShipAbilityHash::AddUInt32(Hash, OwnerPlayerGuid.D);
	GuLiShipAbilityHash::AddString(Hash, WingmanTypeId.ToString());
	GuLiShipAbilityHash::AddUInt32(Hash, ShipGeneration);
	GuLiShipAbilityHash::AddUInt32(Hash, GroupGeneration);
	GuLiShipAbilityHash::AddUInt32(Hash, AbilitySetRevision);
	GuLiShipAbilityHash::AddUInt32(Hash, LoadoutRevision);
	GuLiShipAbilityHash::AddUInt32(Hash, SnapshotRevision);
	GuLiShipAbilityHash::AddBool(Hash, bGroupAbilitiesValid);
	GuLiShipAbilityHash::AddTag(Hash, FormationAbilityId);
	GuLiShipAbilityHash::AddTag(Hash, BasicWeaponAbilityId);
	GuLiShipAbilityHash::AddTag(Hash, MissileAbilityId);
	GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(WeaponChannels.Num()));
	for (const FGuLiWingmanWeaponChannelConfig& Channel : WeaponChannels)
	{
		Channel.AddToStableHash(Hash);
	}
	GuLiShipAbilityHash::AddUInt32(Hash, FormationDefinitionRevision);
	GuLiShipAbilityHash::AddUInt64(Hash, FormationDefinitionChecksum);
	GuLiShipAbilityHash::AddUInt32(Hash, BasicWeaponDefinitionRevision);
	GuLiShipAbilityHash::AddUInt64(Hash, BasicWeaponDefinitionChecksum);
	GuLiShipAbilityHash::AddUInt32(Hash, MissileDefinitionRevision);
	GuLiShipAbilityHash::AddUInt64(Hash, MissileDefinitionChecksum);
	FormationRuntime.AddToStableHash(Hash);
	BasicWeaponRuntime.AddToStableHash(Hash);
	MissileRuntime.AddToStableHash(Hash);
	GuLiShipAbilityHash::AddUInt32(Hash, FormationCommandRevision);
	GuLiShipAbilityHash::AddUInt32(Hash, EffectiveClientSimTick);
	return GuLiShipAbilityHash::Finish(Hash);
}

void FGuLiGroupAbilityConfigSnapshot::RefreshHash()
{
	SnapshotHash = ComputeStableHash();
}

bool FGuLiGroupAbilityConfigSnapshot::HasRequiredV1Abilities() const
{
	// Kept as a source-compatible name for existing protocol gates. Protocol v9
	// requires a formation; a legal loadout may intentionally contain no weapons.
	return FormationAbilityId.IsValid();
}

const FGuLiWingmanWeaponChannelConfig* FGuLiGroupAbilityConfigSnapshot::FindWeaponChannel(
	const FGuLiWeaponBindingKey& Binding) const
{
	return WeaponChannels.FindByPredicate([&Binding](const FGuLiWingmanWeaponChannelConfig& Channel)
	{
		return Channel.Binding == Binding;
	});
}

const FGuLiWingmanWeaponChannelConfig* FGuLiGroupAbilityConfigSnapshot::FindFirstWeaponChannel(
	const EGuLiWingmanWeaponKind Kind) const
{
	return WeaponChannels.FindByPredicate([Kind](const FGuLiWingmanWeaponChannelConfig& Channel)
	{
		return Channel.bEnabled && Channel.Kind == Kind;
	});
}

bool FGuLiGroupAbilityConfigSnapshot::IsWellFormed() const
{
	const bool bBaseFieldsValid = ProtocolVersion == GULI_WINGMAN_PROTOCOL_VERSION
		&& ShipInstanceId.IsValid()
		&& MatchEpoch != 0u
		&& (Team == EGuLiTeam::Red || Team == EGuLiTeam::Blue)
		&& OwnerPlayerGuid.IsValid()
		&& !WingmanTypeId.IsNone()
		&& ShipGeneration != 0u
		&& GroupGeneration != 0u
		&& SnapshotRevision != 0u
		&& SnapshotHash != 0u
		&& SnapshotHash == ComputeStableHash();
	if (!bBaseFieldsValid)
	{
		return false;
	}

	if (bGroupAbilitiesValid)
	{
		if (AbilitySetRevision == 0u || LoadoutRevision == 0u
			|| !HasRequiredV1Abilities()
			|| FormationDefinitionRevision == 0u
			|| FormationDefinitionChecksum == 0u
			|| !FormationRuntime.IsWellFormed()
			|| WeaponChannels.Num() > GULI_MAX_WINGMAN_WEAPON_CHANNELS)
		{
			return false;
		}
		TSet<FGuLiWeaponBindingKey> SeenBindings;
		double AutomaticFireRatePerSecond = 0.0;
		int32 AttackRecordsPerFlight = 0;
		for (const FGuLiWingmanWeaponChannelConfig& Channel : WeaponChannels)
		{
			if (!Channel.IsWellFormed()
				|| Channel.Binding.MatchEpoch != MatchEpoch
				|| Channel.Binding.Team != Team
				|| Channel.Binding.OwnerPlayerGuid != OwnerPlayerGuid
				|| Channel.Binding.SubjectId != WingmanTypeId
				|| SeenBindings.Contains(Channel.Binding))
			{
				return false;
			}
			SeenBindings.Add(Channel.Binding);
			if (Channel.bEnabled && Channel.Kind == EGuLiWingmanWeaponKind::BasicAutomatic)
			{
				if (Channel.Runtime.Attack.Pattern == EGuLiWingmanAttackPattern::Legacy)
					AutomaticFireRatePerSecond += 25.0 / static_cast<double>(Channel.Runtime.CooldownSeconds);
				else
				{
					if (Channel.Runtime.Attack.FlightSpeed < FormationRuntime.MinimumSpeedCentimetersPerSecond
						|| Channel.Runtime.Attack.FlightSpeed > FormationRuntime.CatchUpSpeedCentimetersPerSecond) return false;
                    if (Channel.Runtime.Attack.Pattern == EGuLiWingmanAttackPattern::GroundDive)
                    {
                        FGuLiWingmanGroundRunPath Path;
                        if (!GuLiWingmanAttack::BuildGroundPath(FVector::ZeroVector, FVector::ForwardVector,
                            Channel.Runtime.Attack, FormationRuntime.MaximumTurnRateDegreesPerSecond, Path)) return false;
                    }
					AttackRecordsPerFlight += Channel.Runtime.Attack.Pattern == EGuLiWingmanAttackPattern::GroundDive
						? Channel.Runtime.Attack.MaximumShotsPerFlightBatch()
						: 5 * (1 + FMath::FloorToInt(0.2 / Channel.Runtime.CooldownSeconds + 1.e-6));
				}
			}
		}
		return AttackRecordsPerFlight <= GuLiWingmanAttack::MaximumFireRecordsPerFlight && AutomaticFireRatePerSecond
			<= GULI_WINGMAN_AUTOMATIC_FIRE_BUDGET_PER_SECOND + UE_DOUBLE_SMALL_NUMBER;
	}

	// An invalidation is explicit and unambiguous; stale IDs/checksums may not
	// remain consumable after the group has ended.
	return LoadoutRevision == 0u && WeaponChannels.IsEmpty()
		&& !FormationAbilityId.IsValid()
		&& !BasicWeaponAbilityId.IsValid()
		&& !MissileAbilityId.IsValid()
		&& FormationDefinitionRevision == 0u
		&& FormationDefinitionChecksum == 0u
		&& BasicWeaponDefinitionRevision == 0u
		&& BasicWeaponDefinitionChecksum == 0u
		&& MissileDefinitionRevision == 0u
		&& MissileDefinitionChecksum == 0u;
}

bool FGuLiGroupAbilityConfigSnapshot::HasSameVersion(const FGuLiGroupAbilityConfigSnapshot& Other) const
{
	return ShipInstanceId == Other.ShipInstanceId
		&& ShipGeneration == Other.ShipGeneration
		&& GroupGeneration == Other.GroupGeneration
		&& AbilitySetRevision == Other.AbilitySetRevision
		&& LoadoutRevision == Other.LoadoutRevision
		&& SnapshotRevision == Other.SnapshotRevision
		&& FormationCommandRevision == Other.FormationCommandRevision
		&& SnapshotHash == Other.SnapshotHash;
}

namespace GuLiShipAbilityHash
{
	void AddUInt32(uint64& Hash, const uint32 Value)
	{
		for (uint32 Shift = 0u; Shift < 32u; Shift += 8u)
		{
			AddByte(Hash, static_cast<uint8>((Value >> Shift) & 0xffu));
		}
	}

	void AddUInt64(uint64& Hash, const uint64 Value)
	{
		for (uint32 Shift = 0u; Shift < 64u; Shift += 8u)
		{
			AddByte(Hash, static_cast<uint8>((Value >> Shift) & 0xffull));
		}
	}

	void AddFloat(uint64& Hash, const float Value)
	{
		uint32 Bits = 0u;
		static_assert(sizeof(Bits) == sizeof(Value));
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		AddUInt32(Hash, Bits);
	}

	void AddBool(uint64& Hash, const bool bValue)
	{
		AddByte(Hash, bValue ? 1u : 0u);
	}

	void AddString(uint64& Hash, const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		AddUInt32(Hash, static_cast<uint32>(Utf8.Length()));
		for (int32 Index = 0; Index < Utf8.Length(); ++Index)
		{
			AddByte(Hash, static_cast<uint8>(Utf8.Get()[Index]));
		}
	}

	void AddTag(uint64& Hash, const FGameplayTag Tag)
	{
		AddString(Hash, Tag.IsValid() ? Tag.ToString() : FString());
	}

	uint64 Finish(const uint64 Hash)
	{
		return Hash == 0u ? 1u : Hash;
	}
}
