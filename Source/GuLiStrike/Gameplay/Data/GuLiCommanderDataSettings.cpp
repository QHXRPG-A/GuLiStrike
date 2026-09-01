// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Data/GuLiCommanderDataSettings.h"

#include "Engine/DataTable.h"

UGuLiCommanderDataSettings::UGuLiCommanderDataSettings()
	: SoldierDataTable(FSoftObjectPath(
		TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers.DT_GuLiStrikeCommander_Soldiers")))
{
	SkillDataTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Skills.DT_GuLiStrikeCommander_Skills")));
	UnitSkillDataTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_UnitSkills.DT_GuLiStrikeCommander_UnitSkills")));
}
