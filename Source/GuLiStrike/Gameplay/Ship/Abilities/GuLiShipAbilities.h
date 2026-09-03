// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Ship/Abilities/GuLiShipGameplayAbility.h"
#include "GuLiShipAbilities.generated.h"

/** Default server-activated, persistent double-ring formation authorization. */
UCLASS()
class GULISTRIKE_API UGuLiShipDoubleRingFormationAbility final : public UGuLiShipGameplayAbility
{
	GENERATED_BODY()

public:
	UGuLiShipDoubleRingFormationAbility();
};

/** Persistent group-wide basic weapon authorization; each Mass entity owns cadence/sequence. */
UCLASS()
class GULISTRIKE_API UGuLiShipBasicAutomaticWeaponAbility final : public UGuLiShipGameplayAbility
{
	GENERATED_BODY()

public:
	UGuLiShipBasicAutomaticWeaponAbility();
};

/** Locally predicted input authorization for one server-validated Flight missile salvo. */
UCLASS()
class GULISTRIKE_API UGuLiShipMissileSalvoAbility final : public UGuLiShipGameplayAbility
{
	GENERATED_BODY()

public:
	UGuLiShipMissileSalvoAbility();
};
