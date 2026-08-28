// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"

#include "Gameplay/Data/GuLiCommanderDataSettings.h"
#include "Gameplay/Data/GuLiCommanderSoldierResolver.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"

bool UGuLiCommanderDataSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World != nullptr && World->IsGameWorld();
}

void UGuLiCommanderDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UGuLiCommanderDataSettings* Settings = GetDefault<UGuLiCommanderDataSettings>();
	UDataTable* SoldierTable = Settings ? Settings->SoldierDataTable.LoadSynchronous() : nullptr;
	const FName RowName = Settings ? Settings->DefaultSoldierRowName : NAME_None;
	const FGuLiSoldierDefinition Fallback = FGuLiCommanderSoldierResolver::MakeFallbackDefinition();
	DefaultSoldierDefinition = FGuLiCommanderSoldierResolver::Resolve(
		SoldierTable,
		RowName,
		Fallback,
		bDefaultSoldierDefinitionFromDataTable);
}
