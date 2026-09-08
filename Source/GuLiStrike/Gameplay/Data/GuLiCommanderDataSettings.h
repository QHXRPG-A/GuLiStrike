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

	/** Imported DataTable generated from GuLiStrikeCommander.xlsx / Soldiers. */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Data")
	TSoftObjectPtr<UDataTable> SoldierDataTable;

	UPROPERTY(Config, EditAnywhere, Category = "Commander|Data")
	TSoftObjectPtr<UDataTable> SkillDataTable;

	UPROPERTY(Config, EditAnywhere, Category = "Commander|Data")
	TSoftObjectPtr<UDataTable> UnitSkillDataTable;

	/** Imported DataTable generated from GuLiStrikeCommander.xlsx / SpellFields. */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Data")
	TSoftObjectPtr<UDataTable> SpellFieldDataTable;

	/** Imported DataTable generated from GuLiStrikeCommander.xlsx / WeaponMounts. */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Data")
	TSoftObjectPtr<UDataTable> WeaponMountDataTable;

	/** Optional initial-unlock rules keyed by type/slot; weapon candidates remain in UnitSkills. */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Weapons")
	TArray<FGuLiArmyWeaponSlotRule> WeaponSlotRules;

	UPROPERTY(Config, EditAnywhere, Category = "Commander|Weapons", meta=(ClampMin="1", ClampMax="32"))
	int32 MaximumWeaponSlotsPerUnit = 8;

	/** Default archetype for legacy consumers; additional unit types use their own rows. */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Data")
	FName DefaultSoldierRowName = TEXT("DefaultSoldier");
};
