// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GuLiCommanderSoldierDefinition.generated.h"

class UStaticMesh;

/**
 * Validated runtime values for the single Commander Soldier archetype.
 *
 * This is the common boundary consumed by authority simulation, presentation and
 * runtime tuning. Values copied here have already passed DataTable validation.
 */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiSoldierDefinition
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Soldier", meta = (Units = "cm/s"))
	float MovementSpeedCmPerSecond = 3600.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Soldier")
	uint8 MaxHealth = 100u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Soldier")
	TObjectPtr<UStaticMesh> Model = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Soldier")
	float AttackPower = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Soldier")
	float Defense = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Soldier", meta = (Units = "cm"))
	float AttackRangeCentimeters = 0.0f;
};
