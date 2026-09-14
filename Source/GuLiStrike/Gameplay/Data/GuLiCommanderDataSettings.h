// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Gameplay/Skills/GuLiWeaponChannelTypes.h"
#include "GuLiCommanderDataSettings.generated.h"

class UDataTable;

/** Project configuration locating Commander data without hard-wiring a Blueprint CDO. */
UCLASS(Config = Game, DefaultConfig)
class GULISTRIKE_API UGuLiCommanderDataSettings final : public UObject
{
	GENERATED_BODY()

public:
	UGuLiCommanderDataSettings();

	UPROPERTY(Config, EditAnywhere, Category = "Commander|Data")
	TSoftObjectPtr<UDataTable> SkillDataTable;

	UPROPERTY(Config, EditAnywhere, Category = "Commander|Data")
	TSoftObjectPtr<UDataTable> UnitSkillDataTable;

	/** Stable DataTable identity; authored in GuLiStrikeSecondaryWeapons.xlsx / WeaponMounts. */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Data")
	TSoftObjectPtr<UDataTable> WeaponMountDataTable;

	/** Optional initial-unlock rules keyed by type/slot; weapon candidates remain in UnitSkills. */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Weapons")
	TArray<FGuLiArmyWeaponSlotRule> WeaponSlotRules;

	UPROPERTY(Config, EditAnywhere, Category = "Commander|Weapons", meta=(ClampMin="1", ClampMax="32"))
	int32 MaximumWeaponSlotsPerUnit = 8;

};
