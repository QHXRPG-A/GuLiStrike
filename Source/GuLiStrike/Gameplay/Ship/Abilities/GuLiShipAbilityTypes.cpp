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

bool FGuLiWingmanFormationRuntimeConfig::IsWellFormed() const
{
	const auto PositiveFinite = [](const float Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0f;
	};
	return InnerRingSlots > 0u && OuterRingSlots > 0u
		&& static_cast<uint32>(InnerRingSlots) + static_cast<uint32>(OuterRingSlots) == 25u
		&& PositiveFinite(InnerRingRadiusCentimeters)
		&& PositiveFinite(OuterRingRadiusCentimeters)
		&& OuterRingRadiusCentimeters > InnerRingRadiusCentimeters
		&& FMath::IsFinite(InnerRingHeightCentimeters)
		&& FMath::IsFinite(OuterRingHeightCentimeters)
		&& PositiveFinite(InnerAngularSpeedRadiansPerSecond)
		&& PositiveFinite(OuterAngularSpeedRadiansPerSecond)
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
}

void FGuLiWingmanFormationRuntimeConfig::AddToStableHash(uint64& Hash) const
{
	GuLiShipAbilityHash::AddUInt32(Hash, InnerRingSlots);
	GuLiShipAbilityHash::AddUInt32(Hash, OuterRingSlots);
	GuLiShipAbilityHash::AddFloat(Hash, InnerRingRadiusCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, OuterRingRadiusCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, InnerRingHeightCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, OuterRingHeightCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, InnerAngularSpeedRadiansPerSecond);
	GuLiShipAbilityHash::AddFloat(Hash, OuterAngularSpeedRadiansPerSecond);
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
	return FMath::IsFinite(RangeCentimeters) && RangeCentimeters > 0.0f
		&& FMath::IsFinite(CooldownSeconds) && CooldownSeconds > 0.0f
		&& FMath::IsFinite(TargetConeHalfAngleDegrees)
		&& TargetConeHalfAngleDegrees > 0.0f && TargetConeHalfAngleDegrees <= 180.0f;
}

void FGuLiWingmanWeaponRuntimeConfig::AddToStableHash(uint64& Hash) const
{
	GuLiShipAbilityHash::AddFloat(Hash, RangeCentimeters);
	GuLiShipAbilityHash::AddFloat(Hash, CooldownSeconds);
	GuLiShipAbilityHash::AddFloat(Hash, TargetConeHalfAngleDegrees);
	GuLiShipAbilityHash::AddBool(Hash, bRequiresLineOfSight);
}

uint64 FGuLiGroupAbilityConfigSnapshot::ComputeStableHash() const
{
	uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
	GuLiShipAbilityHash::AddUInt32(Hash, ProtocolVersion);
	GuLiShipAbilityHash::AddUInt32(Hash, ShipInstanceId.A);
	GuLiShipAbilityHash::AddUInt32(Hash, ShipInstanceId.B);
	GuLiShipAbilityHash::AddUInt32(Hash, ShipInstanceId.C);
	GuLiShipAbilityHash::AddUInt32(Hash, ShipInstanceId.D);
	GuLiShipAbilityHash::AddUInt32(Hash, ShipGeneration);
	GuLiShipAbilityHash::AddUInt32(Hash, GroupGeneration);
	GuLiShipAbilityHash::AddUInt32(Hash, AbilitySetRevision);
	GuLiShipAbilityHash::AddUInt32(Hash, SnapshotRevision);
	GuLiShipAbilityHash::AddBool(Hash, bGroupAbilitiesValid);
	GuLiShipAbilityHash::AddTag(Hash, FormationAbilityId);
	GuLiShipAbilityHash::AddTag(Hash, BasicWeaponAbilityId);
	GuLiShipAbilityHash::AddTag(Hash, MissileAbilityId);
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
	return FormationAbilityId.IsValid() && BasicWeaponAbilityId.IsValid() && MissileAbilityId.IsValid();
}

bool FGuLiGroupAbilityConfigSnapshot::IsWellFormed() const
{
	const bool bBaseFieldsValid = ProtocolVersion == GULI_WINGMAN_PROTOCOL_VERSION
		&& ShipInstanceId.IsValid()
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
		return AbilitySetRevision != 0u
			&& HasRequiredV1Abilities()
			&& FormationDefinitionRevision != 0u
			&& FormationDefinitionChecksum != 0u
			&& BasicWeaponDefinitionRevision != 0u
			&& BasicWeaponDefinitionChecksum != 0u
			&& MissileDefinitionRevision != 0u
			&& MissileDefinitionChecksum != 0u
			&& FormationRuntime.IsWellFormed()
			&& BasicWeaponRuntime.IsWellFormed()
			&& MissileRuntime.IsWellFormed();
	}

	// An invalidation is explicit and unambiguous; stale IDs/checksums may not
	// remain consumable after the group has ended.
	return !FormationAbilityId.IsValid()
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
