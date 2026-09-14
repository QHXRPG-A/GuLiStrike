// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Skills/GuLiSkillGMUtilities.h"

#include "Misc/SecureHash.h"

FGuid GuLiSkillGM::MakeSourceId(const EGuLiTeam Team, const FString& Label)
{
	FGuid Result;
	FGuid::Parse(
		FMD5::HashAnsiString(
			*FString::Printf(TEXT("GuLiSkillGM/%d/%s"), static_cast<int32>(Team), *Label)),
		Result);
	return Result;
}

