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
    if (!ResolveAuthoredDefinitions(Definitions,CatalogError))
    {Definitions.Reset();MassDefinitions.Reset();UE_LOG(LogTemp,Error,TEXT("%s"),*CatalogError);return;}
    MassDefinitions=Definitions.FilterByPredicate([](const auto& D){return D.UsesMass();});
}

bool UGuLiUnitDataSubsystem::ResolveAuthoredDefinitions(TArray<FGuLiSoldierDefinition>& Out,FString& Error)
{
    Error.Reset();TArray<FGuLiSoldierDefinition> Resolved;
    const auto* Table=GetDefault<UGuLiUnitDataSettings>()->SoldierDataTable.LoadSynchronous();
    const auto Fallback=FGuLiCommanderSoldierResolver::MakeFallbackDefinition();
	if (!Table || Table->GetRowStruct() != FGuLiStrikeCommanderSoldiersRow::StaticStruct())
	{
		Error = TEXT("Soldiers requires a valid unit DataTable.");
		return false;
	}
	for (FName RowName : Table->GetRowNames())
	{
		bool bValid = false;
		const FGuLiSoldierDefinition Definition = FGuLiCommanderSoldierResolver::Resolve(Table, RowName, Fallback, bValid);
		if (!bValid || Resolved.FindByPredicate([&](const auto& D){return D.UnitTypeId==Definition.UnitTypeId;}))
		{
			Error = FString::Printf(TEXT("Soldiers row '%s' is invalid or repeats a UnitTypeId."), *RowName.ToString());

			return false;
		}
		FString BehaviorError;
		if (!GuLiCommanderBehavior::ValidateDefinition(Definition, BehaviorError))
		{
			Error = FString::Printf(TEXT("Soldiers/%s: %s"), *RowName.ToString(), *BehaviorError);

			return false;
		}
		Resolved.Add(Definition);
	}
	Resolved.Sort([](const auto& A, const auto& B) { return A.UnitTypeId < B.UnitTypeId; });
    Out=MoveTemp(Resolved);return true;
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
