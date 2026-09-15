// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/Abilities/GuLiShipAbilitySet.h"

#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"

namespace
{
	bool Fail(FString* OutError, const FString& Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
		return false;
	}

	int32 SlotSortKey(const EGuLiShipAbilitySlot Slot)
	{
		switch (Slot)
		{
		case EGuLiShipAbilitySlot::Formation: return 0;
		case EGuLiShipAbilitySlot::BasicWeapon: return 1;
		case EGuLiShipAbilitySlot::Missile: return 2;
		default: return MAX_int32;
		}
	}
}

bool FGuLiShipAbilityGrant::IsWellFormed(FString* OutError) const
{
	if (!AbilityId.IsValid())
	{
		return Fail(OutError, TEXT("Ability grant has an invalid stable AbilityId."));
	}
	if (Slot == EGuLiShipAbilitySlot::None || !GuLiGetShipAbilitySlotTag(Slot).IsValid())
	{
		return Fail(OutError, FString::Printf(TEXT("Ability %s has an invalid slot."), *AbilityId.ToString()));
	}
	bool bReticleTagConflict = false;
	const EGuLiShipReticleMode ReticleMode = GuLiShipReticle::ResolveMode(
		PresentationTags, &bReticleTagConflict);
	if (bReticleTagConflict)
	{
		return Fail(OutError, FString::Printf(
			TEXT("Ability %s declares both Ship reticle mode tags."), *AbilityId.ToString()));
	}
	if (ReticleMode == EGuLiShipReticleMode::Bounded
		&& !ReticleConfig.IsValid())
	{
		return Fail(OutError, FString::Printf(
			TEXT("Ability %s has an invalid bounded reticle configuration."), *AbilityId.ToString()));
	}

	if ((Slot == EGuLiShipAbilitySlot::Missile) != InputTag.IsValid())
	{
		return Fail(OutError, TEXT("Only triggered missile actions require an input tag."));
	}

	if (Slot == EGuLiShipAbilitySlot::Formation)
	{
		if (!FormationDefinition || WeaponDefinition || !FormationDefinition->IsWellFormed(OutError))
		{
			return Fail(OutError, FString::Printf(
				TEXT("Formation ability %s requires only a valid formation definition."), *AbilityId.ToString()));
		}
	}
	else
	{
		if (GetEffectiveWeaponSlotId().IsNone() || GetEffectiveSkillId().IsNone()
			|| ProfileRevision == 0u)
		{
			return Fail(OutError, FString::Printf(
				TEXT("Weapon ability %s requires stable slot/skill/profile identities."), *AbilityId.ToString()));
		}
		if (FormationDefinition || !WeaponDefinition || !WeaponDefinition->IsWellFormed(OutError))
		{
			return Fail(OutError, FString::Printf(
				TEXT("Weapon ability %s requires only a valid weapon definition."), *AbilityId.ToString()));
		}
		const EGuLiWingmanWeaponKind ExpectedKind = Slot == EGuLiShipAbilitySlot::Missile
			? EGuLiWingmanWeaponKind::Missile
			: EGuLiWingmanWeaponKind::BasicAutomatic;
		if (WeaponDefinition->Kind != ExpectedKind)
		{
			return Fail(OutError, FString::Printf(
				TEXT("Weapon ability %s definition kind does not match its slot."), *AbilityId.ToString()));
		}
	}

	return true;
}

FName FGuLiShipAbilityGrant::GetEffectiveWeaponSlotId() const
{
	return !WeaponSlotId.IsNone() ? WeaponSlotId : GuLiGetDefaultWeaponSlotId(Slot);
}

FName FGuLiShipAbilityGrant::GetEffectiveSkillId() const
{
	return !SkillId.IsNone() ? SkillId : GuLiGetDefaultWeaponSkillId(AbilityId);
}

FName FGuLiShipAbilityGrant::GetEffectiveCooldownGroupId() const
{
	if (!CooldownGroupId.IsNone())
	{
		return CooldownGroupId;
	}
	return Slot == EGuLiShipAbilitySlot::Missile
		? FName(TEXT("WingmanMissileSalvo")) : NAME_None;
}

uint32 FGuLiShipAbilityGrant::GetDefinitionRevision() const
{
	return FormationDefinition ? FormationDefinition->Revision
		: WeaponDefinition ? WeaponDefinition->Revision
		: 0u;
}

uint64 FGuLiShipAbilityGrant::GetDefinitionChecksum() const
{
	return FormationDefinition ? FormationDefinition->ComputeStableChecksum()
		: WeaponDefinition ? WeaponDefinition->ComputeStableChecksum()
		: 0u;
}

const FGuLiShipAbilityGrant* UGuLiShipAbilitySet::FindGrant(const FGameplayTag AbilityId) const
{
	return Grants.FindByPredicate([AbilityId](const FGuLiShipAbilityGrant& Grant)
	{
		return Grant.AbilityId == AbilityId;
	});
}

bool UGuLiShipAbilitySet::IsWellFormed(FString* OutError) const
{
	if (Revision == 0u)
	{
		return Fail(OutError, TEXT("Ship ability set revision must be nonzero."));
	}
	if (Grants.IsEmpty())
	{
		return Fail(OutError, TEXT("Ship ability set contains no grants."));
	}
	if (WingmanTypeId.IsNone())
	{
		return Fail(OutError, TEXT("Ship ability set requires a stable WingmanTypeId."));
	}

	TSet<FGameplayTag> SeenAbilityIds;
	TSet<FName> CatalogWeaponSlots;
	for (const FGuLiShipAbilityGrant& Grant : Grants)
	{
		FString GrantError;
		if (!Grant.IsWellFormed(&GrantError))
		{
			return Fail(OutError, GrantError);
		}
		if (SeenAbilityIds.Contains(Grant.AbilityId))
		{
			return Fail(OutError, FString::Printf(
				TEXT("Ship ability set contains duplicate AbilityId %s."), *Grant.AbilityId.ToString()));
		}
		SeenAbilityIds.Add(Grant.AbilityId);
		if (Grant.Slot != EGuLiShipAbilitySlot::Formation)
		{
			CatalogWeaponSlots.Add(Grant.GetEffectiveWeaponSlotId());
			if (CatalogWeaponSlots.Num() > GULI_MAX_WINGMAN_WEAPON_CHANNELS)
			{
				return Fail(OutError, TEXT("Ship ability set exceeds the distinct Wingman weapon-slot limit."));
			}
		}
	}
	return true;
}

bool UGuLiShipAbilitySet::ResolveLoadout(
	const FGuLiShipAbilityLoadoutState& Loadout,
	TArray<FGuLiShipAbilityGrant>& OutOrderedGrants,
	FString* OutError) const
{
	OutOrderedGrants.Reset();
	FString Error;
	if (!IsWellFormed(&Error))
	{
		return Fail(OutError, Error);
	}
	if (!Loadout.IsWellFormed(&Error))
	{
		return Fail(OutError, Error);
	}

	bool bFormationSelected = false;
	TSet<FName> SeenWeaponSlots;
	int32 WeaponChannelCount = 0;
	for (const FGameplayTag AbilityId : Loadout.AbilityIds)
	{
		const FGuLiShipAbilityGrant* Grant = FindGrant(AbilityId);
		if (!Grant)
		{
			return Fail(OutError, FString::Printf(
				TEXT("Loadout AbilityId %s is absent from the selected ability set."), *AbilityId.ToString()));
		}
		if (Grant->Slot == EGuLiShipAbilitySlot::Formation)
		{
			if (bFormationSelected)
			{
				return Fail(OutError, TEXT("Loadout selects more than one formation ability."));
			}
			bFormationSelected = true;
		}
		else
		{
			const FName WeaponSlot = Grant->GetEffectiveWeaponSlotId();
			if (SeenWeaponSlots.Contains(WeaponSlot))
			{
				return Fail(OutError, FString::Printf(
					TEXT("Loadout selects more than one weapon for binding slot %s."), *WeaponSlot.ToString()));
			}
			SeenWeaponSlots.Add(WeaponSlot);
			if (++WeaponChannelCount > GULI_MAX_WINGMAN_WEAPON_CHANNELS)
			{
				return Fail(OutError, TEXT("Loadout exceeds the Wingman weapon-channel limit."));
			}
		}
		OutOrderedGrants.Add(*Grant);
	}

	if (!bFormationSelected)
	{
		OutOrderedGrants.Reset();
		return Fail(OutError, TEXT("Loadout is missing its required formation ability."));
	}

	OutOrderedGrants.Sort([](const FGuLiShipAbilityGrant& Lhs, const FGuLiShipAbilityGrant& Rhs)
	{
		const int32 LhsKey = SlotSortKey(Lhs.Slot);
		const int32 RhsKey = SlotSortKey(Rhs.Slot);
		return LhsKey != RhsKey ? LhsKey < RhsKey
			: Lhs.GetEffectiveWeaponSlotId().LexicalLess(Rhs.GetEffectiveWeaponSlotId());
	});
	return true;
}

uint64 UGuLiShipAbilitySet::ComputeLoadoutChecksum(const FGuLiShipAbilityLoadoutState& Loadout) const
{
	TArray<FGuLiShipAbilityGrant> Ordered;
	if (!ResolveLoadout(Loadout, Ordered))
	{
		return 0u;
	}

	uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
	GuLiShipAbilityHash::AddString(Hash, TEXT("GuLi.ShipAbilitySet.v2"));
	GuLiShipAbilityHash::AddString(Hash, WingmanTypeId.ToString());
	GuLiShipAbilityHash::AddUInt32(Hash, Revision);
	GuLiShipAbilityHash::AddUInt32(Hash, Loadout.Revision);
	for (const FGuLiShipAbilityGrant& Grant : Ordered)
	{
		GuLiShipAbilityHash::AddTag(Hash, Grant.AbilityId);
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Grant.Slot));
		GuLiShipAbilityHash::AddString(Hash, Grant.GetEffectiveWeaponSlotId().ToString());
		GuLiShipAbilityHash::AddString(Hash, Grant.GetEffectiveSkillId().ToString());
		GuLiShipAbilityHash::AddUInt32(Hash, Grant.ProfileRevision);
		GuLiShipAbilityHash::AddString(Hash, Grant.GetEffectiveCooldownGroupId().ToString());
		GuLiShipAbilityHash::AddTag(Hash, Grant.InputTag);
		GuLiShipAbilityHash::AddUInt32(Hash, Grant.GetDefinitionRevision());
		GuLiShipAbilityHash::AddUInt64(Hash, Grant.GetDefinitionChecksum());
	}
	return GuLiShipAbilityHash::Finish(Hash);
}

UGuLiShipAbilitySet* UGuLiShipAbilitySet::CreateNativeV1Transient(UObject* Outer)
{
	Outer = Outer ? Outer : GetTransientPackage();
	UGuLiShipAbilitySet* Set = NewObject<UGuLiShipAbilitySet>(Outer, NAME_None, RF_Transient);
	if (!Set)
	{
		return nullptr;
	}

	UGuLiWingmanFormationDefinition* Formation =
		NewObject<UGuLiWingmanFormationDefinition>(Set, NAME_None, RF_Transient);
	UGuLiWingmanWeaponDefinition* Basic =
		NewObject<UGuLiWingmanWeaponDefinition>(Set, NAME_None, RF_Transient);
	UGuLiWingmanWeaponDefinition* Missile =
		NewObject<UGuLiWingmanWeaponDefinition>(Set, NAME_None, RF_Transient);
	if (!Formation || !Basic || !Missile)
	{
		return nullptr;
	}

	Basic->Kind = EGuLiWingmanWeaponKind::BasicAutomatic;
	Basic->Damage = 10.0f;
	Basic->RangeCentimeters = 150000.0f;
	Basic->CooldownSeconds = 2.0f;
	Basic->ProjectileSpeedCentimetersPerSecond = 120000.0f;
	Basic->ProjectileLifetimeSeconds = 1.5f;
	Basic->SweepRadiusCentimeters = 45.0f;
	Basic->TargetConeHalfAngleDegrees = 20.0f;
	Basic->MaximumHomingTurnRateDegreesPerSecond = 0.0f;

	Missile->Kind = EGuLiWingmanWeaponKind::Missile;
	Missile->Damage = 100.0f;
	Missile->RangeCentimeters = 250000.0f;
	Missile->CooldownSeconds = 8.0f;
	Missile->ProjectileSpeedCentimetersPerSecond = 45000.0f;
	Missile->ProjectileLifetimeSeconds = 8.0f;
	Missile->SweepRadiusCentimeters = 150.0f;
	Missile->TargetConeHalfAngleDegrees = 8.0f;
	Missile->MaximumHomingTurnRateDegreesPerSecond = 45.0f;

	FGuLiShipAbilityGrant& FormationGrant = Set->Grants.AddDefaulted_GetRef();
	FormationGrant.AbilityId = TAG_GuLi_ShipAbility_Formation_DoubleRing;
	FormationGrant.Slot = EGuLiShipAbilitySlot::Formation;
	FormationGrant.FormationDefinition = Formation;

	FGuLiShipAbilityGrant& BasicGrant = Set->Grants.AddDefaulted_GetRef();
	BasicGrant.AbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
	BasicGrant.Slot = EGuLiShipAbilitySlot::BasicWeapon;
	BasicGrant.WeaponSlotId = TEXT("BasicWeapon");
	BasicGrant.SkillId = TEXT("Wingman.Basic.Auto");
	BasicGrant.WeaponDefinition = Basic;

	FGuLiShipAbilityGrant& MissileGrant = Set->Grants.AddDefaulted_GetRef();
	MissileGrant.AbilityId = TAG_GuLi_ShipAbility_Weapon_Missile_Salvo;
	MissileGrant.Slot = EGuLiShipAbilitySlot::Missile;
	MissileGrant.WeaponSlotId = TEXT("Missile");
	MissileGrant.SkillId = TEXT("Wingman.Missile.Salvo");
	MissileGrant.CooldownGroupId = TEXT("WingmanMissileSalvo");
	MissileGrant.InputTag = TAG_GuLi_Input_Ship_Wingman_Missile;
	MissileGrant.WeaponDefinition = Missile;
	return Set;
}

UGuLiShipAbilitySet* UGuLiShipAbilitySet::CreateNativeV3Transient(UObject* Outer)
{
	UGuLiShipAbilitySet* Set = CreateNativeV2Transient(Outer);
	if (!Set) return nullptr;
	Set->Revision = 4u;
	for (bool bGround : { false, true })
	{
		UGuLiWingmanWeaponDefinition* Weapon = NewObject<UGuLiWingmanWeaponDefinition>(Set, NAME_None, RF_Transient);
		Weapon->Revision = bGround ? 2u : 3u;
		Weapon->Kind = EGuLiWingmanWeaponKind::BasicAutomatic;
		Weapon->Damage = bGround ? 30.0f : 10.0f;
		Weapon->CooldownSeconds = bGround ? 8.0f : 0.2f;
		Weapon->RangeCentimeters = 150000.0f;
		Weapon->ProjectileSpeedCentimetersPerSecond = bGround ? 6000.0f : 80000.0f;
		Weapon->ProjectileLifetimeSeconds = bGround ? 8.0f : 1.875f;
		Weapon->SweepRadiusCentimeters = bGround ? 30.0f : 45.0f;
		Weapon->TargetConeHalfAngleDegrees = 20.0f;
		Weapon->Attack.Pattern = bGround ? EGuLiWingmanAttackPattern::GroundDive : EGuLiWingmanAttackPattern::AirBurstOrbit;
		Weapon->Attack.ExecutorId = bGround ? TEXT("WingmanGroundMissile") : TEXT("WingmanMachineGun");
		Weapon->Attack.FlightSpeed = 9000.0f;
		if (!bGround)
		{
			Weapon->Attack.AirFireStartDistance = 10000.0f;
			Weapon->Attack.AirFireStopDistance = 5000.0f;
			Weapon->Attack.AirBurstDurationSeconds = 5.0f;
			Weapon->Attack.AirOrbitCooldownSeconds = 3.0f;
		}
		if (bGround)
		{
			Weapon->Attack.ExplosionRadius = 4000.0f;
			Weapon->AttackProjectile = FSoftObjectPath(TEXT(
				"/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundMissile.DA_WingmanGroundMissile"));
		}
		FGuLiShipAbilityGrant& Grant = Set->Grants.AddDefaulted_GetRef();
		Grant.AbilityId = bGround ? TAG_GuLi_ShipAbility_Weapon_Wingman_GroundMissile : TAG_GuLi_ShipAbility_Weapon_Wingman_MachineGun;
		Grant.Slot = EGuLiShipAbilitySlot::BasicWeapon;
		Grant.WeaponSlotId = bGround ? TEXT("GroundWeapon") : TEXT("AirWeapon");
		Grant.SkillId = bGround ? TEXT("Wingman.GroundMissile") : TEXT("Wingman.MachineGun");
		Grant.WeaponDefinition = Weapon;
	}
	return Set;
}

UGuLiShipAbilitySet* UGuLiShipAbilitySet::CreateNativeV2Transient(UObject* Outer)
{
	Outer = Outer ? Outer : GetTransientPackage();
	UGuLiShipAbilitySet* Set = NewObject<UGuLiShipAbilitySet>(Outer, NAME_None, RF_Transient);
	if (!Set)
	{
		return nullptr;
	}
	Set->Revision = 2u;

	UGuLiWingmanFormationDefinition* LegacyFormation =
		NewObject<UGuLiWingmanFormationDefinition>(Set, NAME_None, RF_Transient);
	UGuLiWingmanFormationDefinition* SwarmFormation =
		NewObject<UGuLiWingmanFormationDefinition>(Set, NAME_None, RF_Transient);
	UGuLiWingmanWeaponDefinition* Basic =
		NewObject<UGuLiWingmanWeaponDefinition>(Set, NAME_None, RF_Transient);
	UGuLiWingmanWeaponDefinition* Missile =
		NewObject<UGuLiWingmanWeaponDefinition>(Set, NAME_None, RF_Transient);
	if (!LegacyFormation || !SwarmFormation || !Basic || !Missile)
	{
		return nullptr;
	}

	LegacyFormation->Revision = 4u;
	LegacyFormation->Model = EGuLiWingmanFormationModel::DoubleRingLegacy;
	LegacyFormation->GuidanceAlgorithmVersion = 1u;

	SwarmFormation->Revision = 3u;
	SwarmFormation->Model = EGuLiWingmanFormationModel::SwarmOrbit;
	SwarmFormation->GuidanceAlgorithmVersion = 1u;
	SwarmFormation->CatchUpDistanceCentimeters = 60000.0f;
	SwarmFormation->RecoveryDistanceCentimeters = 90000.0f;

	Basic->Kind = EGuLiWingmanWeaponKind::BasicAutomatic;
	Basic->Damage = 10.0f;
	Basic->RangeCentimeters = 150000.0f;
	Basic->CooldownSeconds = 2.0f;
	Basic->ProjectileSpeedCentimetersPerSecond = 120000.0f;
	Basic->ProjectileLifetimeSeconds = 1.5f;
	Basic->SweepRadiusCentimeters = 45.0f;
	Basic->TargetConeHalfAngleDegrees = 20.0f;
	Basic->MaximumHomingTurnRateDegreesPerSecond = 0.0f;

	Missile->Kind = EGuLiWingmanWeaponKind::Missile;
	Missile->Damage = 100.0f;
	Missile->RangeCentimeters = 250000.0f;
	Missile->CooldownSeconds = 8.0f;
	Missile->ProjectileSpeedCentimetersPerSecond = 45000.0f;
	Missile->ProjectileLifetimeSeconds = 8.0f;
	Missile->SweepRadiusCentimeters = 150.0f;
	Missile->TargetConeHalfAngleDegrees = 8.0f;
	Missile->MaximumHomingTurnRateDegreesPerSecond = 45.0f;

	FGuLiShipAbilityGrant& LegacyGrant = Set->Grants.AddDefaulted_GetRef();
	LegacyGrant.AbilityId = TAG_GuLi_ShipAbility_Formation_DoubleRing;
	LegacyGrant.Slot = EGuLiShipAbilitySlot::Formation;
	LegacyGrant.FormationDefinition = LegacyFormation;

	FGuLiShipAbilityGrant& SwarmGrant = Set->Grants.AddDefaulted_GetRef();
	SwarmGrant.AbilityId = TAG_GuLi_ShipAbility_Formation_SwarmOrbit;
	SwarmGrant.Slot = EGuLiShipAbilitySlot::Formation;
	SwarmGrant.FormationDefinition = SwarmFormation;

	FGuLiShipAbilityGrant& BasicGrant = Set->Grants.AddDefaulted_GetRef();
	BasicGrant.AbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
	BasicGrant.Slot = EGuLiShipAbilitySlot::BasicWeapon;
	BasicGrant.WeaponSlotId = TEXT("BasicWeapon");
	BasicGrant.SkillId = TEXT("Wingman.Basic.Auto");
	BasicGrant.WeaponDefinition = Basic;

	FGuLiShipAbilityGrant& MissileGrant = Set->Grants.AddDefaulted_GetRef();
	MissileGrant.AbilityId = TAG_GuLi_ShipAbility_Weapon_Missile_Salvo;
	MissileGrant.Slot = EGuLiShipAbilitySlot::Missile;
	MissileGrant.WeaponSlotId = TEXT("Missile");
	MissileGrant.SkillId = TEXT("Wingman.Missile.Salvo");
	MissileGrant.CooldownGroupId = TEXT("WingmanMissileSalvo");
	MissileGrant.InputTag = TAG_GuLi_Input_Ship_Wingman_Missile;
	MissileGrant.WeaponDefinition = Missile;
	return Set;
}
