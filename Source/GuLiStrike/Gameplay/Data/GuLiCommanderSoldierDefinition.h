// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GuLiCommanderSoldierDefinition.generated.h"

class UStaticMesh;

/**
 * Validated runtime values for one Commander Soldier type.
 *
 * This is the common boundary consumed by authority simulation, presentation and
 * runtime tuning. Values copied here have already passed DataTable validation.
 */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiSoldierDefinition
{
	GENERATED_BODY()

	/** Stable Soldiers table Id; zero is reserved for invalid definitions. */
	UPROPERTY(VisibleAnywhere, Category = "Commander|Soldier")
	uint16 UnitTypeId = 1u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Soldier", meta = (Units = "cm/s"))
	float MovementSpeedCmPerSecond = 3600.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Soldier")
	float MaxHealth = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Soldier")
	TObjectPtr<UStaticMesh> Model = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Commander|Soldier")
	float Defense = 0.0f;
};
