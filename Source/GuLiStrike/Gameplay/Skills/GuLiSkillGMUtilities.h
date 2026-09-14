// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"

namespace GuLiSkillGM
{
	/** Stable identity shared by the console adapter and the in-game GM panel. */
	GULISTRIKE_API FGuid MakeSourceId(EGuLiTeam Team, const FString& Label);
}

