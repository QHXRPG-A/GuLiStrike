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
    if (!ResolveAuthoredDefinitions(Definitions,CatalogError,&Avoidance))
    {Definitions.Reset();MassDefinitions.Reset();UE_LOG(LogTemp,Error,TEXT("%s"),*CatalogError);return;}
    const auto* Row=Table->FindRow<FGuLiStrikeCommanderSoldiersRow>(Settings->DefaultSoldierRowName,TEXT("DefaultUnit"));
    const auto* Default=Row?FindDefinition(uint16(Row->Id)):nullptr;
    if (!Default) {CatalogError=TEXT("Default Soldier row is missing from the validated directory");return;}
    DefaultDefinition=*Default;bDefaultFromTable=true;

    MassDefinitions=Definitions.FilterByPredicate([](const auto& D){return D.UsesMass();});
}

bool UGuLiUnitDataSubsystem::ResolveAuthoredDefinitions(TArray<FGuLiSoldierDefinition>& Out,FString& Error,FGuLiAvoidanceCatalog* OutAvoidance)
{
    Error.Reset();TArray<FGuLiSoldierDefinition> Resolved;
    const auto* Table=GetDefault<UGuLiUnitDataSettings>()->SoldierDataTable.LoadSynchronous();
    const auto Fallback=FGuLiCommanderSoldierResolver::MakeFallbackDefinition();
	if (!Table || Table->GetRowStruct() != FGuLiStrikeCommanderSoldiersRow::StaticStruct())
	{
		Error = TEXT("Soldiers requires a valid unit DataTable.");
		return false;
	}
    TArray<uint16> UnitIds;
    for (FName Name:Table->GetRowNames())
    {
        const auto* Row=Table->FindRow<FGuLiStrikeCommanderSoldiersRow>(Name,TEXT("UnitIds"));
        if (!Row || Row->Id<1 || Row->Id>MAX_uint16 || UnitIds.Contains(uint16(Row->Id)))
        {Error=TEXT("Invalid or duplicate Soldier UnitTypeId");return false;}
        UnitIds.Add(uint16(Row->Id));
    }
    FGuLiAvoidanceCatalog Circles;
    if (!FGuLiAvoidanceCatalog::Load(UnitIds,Circles,Error)) return false;
	for (FName RowName : Table->GetRowNames())
	{
		bool bValid = false;
		const auto* Row=Table->FindRow<FGuLiStrikeCommanderSoldiersRow>(RowName,TEXT("UnitDefinition"));
        const FGuLiSoldierDefinition Definition = FGuLiCommanderSoldierResolver::Resolve(Table, RowName, Fallback, bValid,Circles.Units.Find(uint16(Row->Id)));
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
    Out=MoveTemp(Resolved);if (OutAvoidance) *OutAvoidance=MoveTemp(Circles);return true;
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
