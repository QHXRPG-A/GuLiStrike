// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/Abilities/GuLiShipAbilities.h"

#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"

UGuLiShipWingmanMachineGunAbility::UGuLiShipWingmanMachineGunAbility()
{
	ConfigureNativeAbility(TAG_GuLi_ShipAbility_Weapon_Wingman_MachineGun,
		EGuLiShipAbilitySlot::BasicWeapon, EGuLiShipAbilityActivationPolicy::WhileGranted);
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

UGuLiShipWingmanGroundMissileAbility::UGuLiShipWingmanGroundMissileAbility()
{
	ConfigureNativeAbility(TAG_GuLi_ShipAbility_Weapon_Wingman_GroundMissile,
		EGuLiShipAbilitySlot::BasicWeapon, EGuLiShipAbilityActivationPolicy::WhileGranted);
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

UGuLiShipDoubleRingFormationAbility::UGuLiShipDoubleRingFormationAbility()
{
	ConfigureNativeAbility(
		TAG_GuLi_ShipAbility_Formation_DoubleRing,
		EGuLiShipAbilitySlot::Formation,
		EGuLiShipAbilityActivationPolicy::WhileGranted);
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

UGuLiShipSwarmOrbitFormationAbility::UGuLiShipSwarmOrbitFormationAbility()
{
	ConfigureNativeAbility(
		TAG_GuLi_ShipAbility_Formation_SwarmOrbit,
		EGuLiShipAbilitySlot::Formation,
		EGuLiShipAbilityActivationPolicy::WhileGranted);
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

UGuLiShipBasicAutomaticWeaponAbility::UGuLiShipBasicAutomaticWeaponAbility()
{
	ConfigureNativeAbility(
		TAG_GuLi_ShipAbility_Weapon_Basic_Auto,
		EGuLiShipAbilitySlot::BasicWeapon,
		EGuLiShipAbilityActivationPolicy::WhileGranted);
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

UGuLiShipMissileSalvoAbility::UGuLiShipMissileSalvoAbility()
{
	ConfigureNativeAbility(
		TAG_GuLi_ShipAbility_Weapon_Missile_Salvo,
		EGuLiShipAbilitySlot::Missile,
		EGuLiShipAbilityActivationPolicy::OnInputTriggered);
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}
