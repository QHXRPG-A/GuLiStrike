// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"

#if !UE_BUILD_SHIPPING

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace GuLiResourceDebugCommands
{
	bool TryParseTeam(const FString& Value, EGuLiTeam& OutTeam)
	{
		if (Value.Equals(TEXT("Red"), ESearchCase::IgnoreCase)) OutTeam = EGuLiTeam::Red;
		else if (Value.Equals(TEXT("Blue"), ESearchCase::IgnoreCase)) OutTeam = EGuLiTeam::Blue;
		else if (Value.Equals(TEXT("Neutral"), ESearchCase::IgnoreCase)) OutTeam = EGuLiTeam::Unassigned;
		else return false;
		return true;
	}

	void SetTerritoryOwner(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() != 3)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Usage: gs.Resources.SetTerritoryOwner <Row 1-%d> <Column 1-%d> <Neutral|Red|Blue>"),
				GULI_RESOURCE_BOARD_DIMENSION, GULI_RESOURCE_BOARD_DIMENSION);
			return;
		}
		UGuLiResourceWorldSubsystem* Resources = World->GetSubsystem<UGuLiResourceWorldSubsystem>();
		const int32 Row = FCString::Atoi(*Args[0]);
		const int32 Column = FCString::Atoi(*Args[1]);
		EGuLiTeam Team = EGuLiTeam::Unassigned;
		if (!TryParseTeam(Args[2], Team))
		{
			UE_LOG(LogTemp, Warning, TEXT("Unknown team '%s'; expected Neutral, Red or Blue."), *Args[2]);
			return;
		}
		const bool bSuccess = Resources && Resources->SetTerritoryOwnerByBoardCoordinate(Row, Column, Team);
		UE_LOG(LogTemp, Display, TEXT("gs.Resources.SetTerritoryOwner R%dC%d %s: %s"),
			Row, Column, *Args[2], bSuccess ? TEXT("OK") : TEXT("FAILED"));
	}

	FAutoConsoleCommandWithWorldAndArgs SetTerritoryOwnerCommand(
		TEXT("gs.Resources.SetTerritoryOwner"),
		TEXT("Authority-only debug ownership switch: <Row> <Column> <Neutral|Red|Blue>."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SetTerritoryOwner));
}

#endif
