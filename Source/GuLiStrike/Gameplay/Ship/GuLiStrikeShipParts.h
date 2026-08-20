// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "GuLiStrikeShipParts.generated.h"

class AGuLiStrikeProjectile;

/**
 *  Base class of every installable ship part.
 *  Concrete parts are Blueprint subclasses that fill in the mesh, stats
 *  and the set of hull sockets they are allowed to plug into.
 */
UCLASS(abstract)
class UGuLiStrikeShipPartComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:

	/** Names of the hull sockets this part may be installed on (one part can fit several sockets) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part")
	TArray<FName> CompatibleSockets;

	/** Transform tweak applied on top of the socket transform after attaching */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part")
	FTransform PartRelativeTransform;

	/** Mass this part adds to the ship when installed */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part", meta=(ClampMin = 0, Units = "kg"))
	float PartMass = 10.0f;

	/** Display name for the loadout/debug UI */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part")
	FText PartDisplayName;

	/** Constructor */
	UGuLiStrikeShipPartComponent();

	/** Returns whether this part is allowed on the given hull socket */
	UFUNCTION(BlueprintPure, Category="Ship Part")
	bool CanAttachToSocket(FName SocketName) const;
};

/**
 *  An engine part: provides thrust and adds mass. The ship's max speed and
 *  acceleration are derived from the total thrust-to-mass ratio.
 */
UCLASS(abstract)
class UGuLiStrikeEnginePart : public UGuLiStrikeShipPartComponent
{
	GENERATED_BODY()

public:

	/** Thrust contributed to the ship */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Engine", meta=(ClampMin = 0))
	float Thrust = 600.0f;
};

/**
 *  A weapon part: fires projectiles from its own transform while installed.
 */
UCLASS(abstract)
class UGuLiStrikeWeaponPart : public UGuLiStrikeShipPartComponent
{
	GENERATED_BODY()

public:

	/** Damage value of the projectiles (exposed for future damage systems and UI) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Weapon", meta=(ClampMin = 0))
	float Damage = 10.0f;

	/** Shots per second while the fire input is held */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Weapon", meta=(ClampMin = 0.01))
	float FireRate = 3.0f;

	/** Projectile to spawn from this part */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Weapon")
	TSubclassOf<AGuLiStrikeProjectile> ProjectileClass;

	/** Muzzle offset relative to this part (part's +X is its firing direction) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ship Part|Weapon")
	FVector MuzzleOffset = FVector(100.0f, 0.0f, 0.0f);

	/** World time of the last shot, used by the ship to respect the fire rate */
	float LastFireTime = -TNumericLimits<float>::Max();
};
