// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Data/GuLiCommanderDataSettings.h"

#include "Engine/DataTable.h"

UGuLiCommanderDataSettings::UGuLiCommanderDataSettings()
	: SoldierDataTable(FSoftObjectPath(
		TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers.DT_GuLiStrikeCommander_Soldiers")))
{
}
