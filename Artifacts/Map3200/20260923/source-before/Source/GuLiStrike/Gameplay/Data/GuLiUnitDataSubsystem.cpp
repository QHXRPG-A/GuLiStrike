#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Commander/Behavior/GuLiCommanderBehaviorSchema.h"
#include "Gameplay/Data/GuLiCommanderSoldierResolver.h"
#include "Gameplay/Data/Generated/GuLiStrikeCommanderTableRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"

UGuLiUnitDataSettings::UGuLiUnitDataSettings()
	: SoldierDataTable(FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers.DT_GuLiStrikeCommander_Soldiers")))
{
}

bool UGuLiUnitDataSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

void UGuLiUnitDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const UGuLiUnitDataSettings* Settings = GetDefault<UGuLiUnitDataSettings>();
	UDataTable* Table = Settings->SoldierDataTable.LoadSynchronous();
	const FGuLiSoldierDefinition Fallback = FGuLiCommanderSoldierResolver::MakeFallbackDefinition();
	DefaultDefinition = FGuLiCommanderSoldierResolver::Resolve(Table, Settings->DefaultSoldierRowName, Fallback, bDefaultFromTable);
	if (!Table || Table->GetRowStruct() != FGuLiStrikeCommanderSoldiersRow::StaticStruct())
	{
		CatalogError = TEXT("Soldiers requires a valid unit DataTable.");
		return;
	}
	for (FName RowName : Table->GetRowNames())
	{
		bool bValid = false;
		const FGuLiSoldierDefinition Definition = FGuLiCommanderSoldierResolver::Resolve(Table, RowName, Fallback, bValid);
		if (!bValid || FindDefinition(Definition.UnitTypeId))
		{
			CatalogError = FString::Printf(TEXT("Soldiers row '%s' is invalid or repeats a UnitTypeId."), *RowName.ToString());
			Definitions.Reset(); MassDefinitions.Reset();
			return;
		}
		FString BehaviorError;
		if (!GuLiCommanderBehavior::ValidateDefinition(Definition, BehaviorError))
		{
			CatalogError = FString::Printf(TEXT("Soldiers/%s: %s"), *RowName.ToString(), *BehaviorError);
			Definitions.Reset(); MassDefinitions.Reset();
			UE_LOG(LogTemp, Error, TEXT("%s"), *CatalogError);
			return;
		}
		Definitions.Add(Definition);
		if (Definition.UsesMass()) MassDefinitions.Add(Definition);
	}
	Definitions.Sort([](const auto& A, const auto& B) { return A.UnitTypeId < B.UnitTypeId; });
	MassDefinitions.Sort([](const auto& A, const auto& B) { return A.UnitTypeId < B.UnitTypeId; });
}

const FGuLiSoldierDefinition* UGuLiUnitDataSubsystem::FindDefinition(const uint16 UnitTypeId) const
{
	return Definitions.FindByPredicate([UnitTypeId](const auto& Definition) { return Definition.UnitTypeId == UnitTypeId; });
}

bool UGuLiUnitDataSubsystem::GetUnitDefinition(const int32 UnitTypeId, FGuLiSoldierDefinition& OutDefinition) const
{
	const FGuLiSoldierDefinition* Definition = UnitTypeId > 0 && UnitTypeId <= MAX_uint16 ? FindDefinition(UnitTypeId) : nullptr;
	if (!Definition) return false;
	OutDefinition = *Definition;
	return true;
}
