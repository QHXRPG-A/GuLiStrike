// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

// Stable ability identifiers. These are persisted in the PlayerState loadout;
// GameplayAbilitySpecHandle is deliberately never part of that contract.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GuLi_ShipAbility_Formation_DoubleRing);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GuLi_ShipAbility_Formation_SwarmOrbit);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GuLi_ShipAbility_Weapon_Basic_Auto);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GuLi_ShipAbility_Weapon_Missile_Salvo);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GuLi_ShipAbility_Weapon_Wingman_MachineGun);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GuLi_ShipAbility_Weapon_Wingman_GroundMissile);

// Optional, mutually-exclusive local reticle presentation modes declared as Ability AssetTags.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GuLi_ShipAbility_Reticle_Omni);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GuLi_ShipAbility_Reticle_Bounded);

// Mutually exclusive group-level slots projected from the Ship ASC.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GuLi_ShipWingman_Formation);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GuLi_ShipWingman_Weapon_Basic);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GuLi_ShipWingman_Weapon_Missile);

// Player input and non-numeric GAS state. No ship AttributeSet is introduced.
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GuLi_Input_Ship_Wingman_Missile);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GuLi_ShipAbility_State_MissileCooldown);
