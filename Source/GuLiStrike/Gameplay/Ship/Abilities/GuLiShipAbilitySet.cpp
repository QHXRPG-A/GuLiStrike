// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/Abilities/GuLiShipAbilitySet.h"

#include "Gameplay/Ship/Abilities/GuLiShipAbilities.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Gameplay/Ship/Abilities/GuLiShipGameplayAbility.h"

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
	if (!AbilityClass)
	{
		return Fail(OutError, FString::Printf(TEXT("Ability %s has no GameplayAbility class."), *AbilityId.ToString()));
	}
	if (AbilityLevel <= 0)
	{
		return Fail(OutError, FString::Printf(TEXT("Ability %s has a non-positive level."), *AbilityId.ToString()));
	}

	const UGuLiShipGameplayAbility* AbilityCDO = AbilityClass->GetDefaultObject<UGuLiShipGameplayAbility>();
	if (!AbilityCDO
		|| AbilityCDO->GetStableAbilityId() != AbilityId
		|| AbilityCDO->GetShipAbilitySlot() != Slot)
	{
		return Fail(OutError, FString::Printf(
			TEXT("Ability %s does not match its class's stable ID/slot metadata."), *AbilityId.ToString()));
	}

	const bool bInputTriggered =
		AbilityCDO->GetShipActivationPolicy() == EGuLiShipAbilityActivationPolicy::OnInputTriggered;
	if (bInputTriggered != InputTag.IsValid())
	{
		return Fail(OutError, FString::Printf(
			TEXT("Ability %s must have an input tag iff it is input-triggered."), *AbilityId.ToString()));
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

	TSet<FGameplayTag> SeenAbilityIds;
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

	TSet<EGuLiShipAbilitySlot> SeenSlots;
	for (const FGameplayTag AbilityId : Loadout.AbilityIds)
	{
		const FGuLiShipAbilityGrant* Grant = FindGrant(AbilityId);
		if (!Grant)
		{
			return Fail(OutError, FString::Printf(
				TEXT("Loadout AbilityId %s is absent from the selected ability set."), *AbilityId.ToString()));
		}
		if (SeenSlots.Contains(Grant->Slot))
		{
			return Fail(OutError, FString::Printf(
				TEXT("Loadout selects more than one ability for slot %s."),
				*GuLiGetShipAbilitySlotTag(Grant->Slot).ToString()));
		}
		SeenSlots.Add(Grant->Slot);
		OutOrderedGrants.Add(*Grant);
	}

	for (const EGuLiShipAbilitySlot RequiredSlot : {
		EGuLiShipAbilitySlot::Formation,
		EGuLiShipAbilitySlot::BasicWeapon,
		EGuLiShipAbilitySlot::Missile })
	{
		if (!SeenSlots.Contains(RequiredSlot))
		{
			OutOrderedGrants.Reset();
			return Fail(OutError, FString::Printf(
				TEXT("Loadout is missing required slot %s."), *GuLiGetShipAbilitySlotTag(RequiredSlot).ToString()));
		}
	}

	OutOrderedGrants.Sort([](const FGuLiShipAbilityGrant& Lhs, const FGuLiShipAbilityGrant& Rhs)
	{
		return SlotSortKey(Lhs.Slot) < SlotSortKey(Rhs.Slot);
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
	GuLiShipAbilityHash::AddString(Hash, TEXT("GuLi.ShipAbilitySet.v1"));
	GuLiShipAbilityHash::AddUInt32(Hash, Revision);
	GuLiShipAbilityHash::AddUInt32(Hash, Loadout.Revision);
	for (const FGuLiShipAbilityGrant& Grant : Ordered)
	{
		GuLiShipAbilityHash::AddTag(Hash, Grant.AbilityId);
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Grant.Slot));
		GuLiShipAbilityHash::AddString(Hash, Grant.AbilityClass->GetPathName());
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Grant.AbilityLevel));
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
	FormationGrant.AbilityClass = UGuLiShipDoubleRingFormationAbility::StaticClass();
	FormationGrant.FormationDefinition = Formation;

	FGuLiShipAbilityGrant& BasicGrant = Set->Grants.AddDefaulted_GetRef();
	BasicGrant.AbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
	BasicGrant.Slot = EGuLiShipAbilitySlot::BasicWeapon;
	BasicGrant.AbilityClass = UGuLiShipBasicAutomaticWeaponAbility::StaticClass();
	BasicGrant.WeaponDefinition = Basic;

	FGuLiShipAbilityGrant& MissileGrant = Set->Grants.AddDefaulted_GetRef();
	MissileGrant.AbilityId = TAG_GuLi_ShipAbility_Weapon_Missile_Salvo;
	MissileGrant.Slot = EGuLiShipAbilitySlot::Missile;
	MissileGrant.AbilityClass = UGuLiShipMissileSalvoAbility::StaticClass();
	MissileGrant.InputTag = TAG_GuLi_Input_Ship_Wingman_Missile;
	MissileGrant.WeaponDefinition = Missile;
	return Set;
}
