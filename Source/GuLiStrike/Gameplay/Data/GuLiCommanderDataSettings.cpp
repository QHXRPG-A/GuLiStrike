// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Data/GuLiCommanderDataSettings.h"

#include "Engine/DataTable.h"

UGuLiCommanderDataSettings::UGuLiCommanderDataSettings()
{
	SkillDataTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Skills.DT_GuLiStrikeCommander_Skills")));
	UnitSkillDataTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_UnitSkills.DT_GuLiStrikeCommander_UnitSkills")));
	WeaponMountDataTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_WeaponMounts.DT_GuLiStrikeCommander_WeaponMounts")));
}
