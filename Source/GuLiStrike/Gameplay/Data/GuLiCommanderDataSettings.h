// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
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

	/** Static archetype row used by all Soldiers in this implementation. */
	UPROPERTY(Config, EditAnywhere, Category = "Commander|Data")
	FName DefaultSoldierRowName = TEXT("DefaultSoldier");
};
