// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"

#include "Gameplay/Data/GuLiCommanderDataSettings.h"
#include "Gameplay/Data/GuLiCommanderSoldierResolver.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Gameplay/Data/Generated/GuLiStrikeCommanderTableRows.h"
#include "Gameplay/Skills/GuLiSkillResolver.h"
#include "GuLiStrike.h"

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
	FString SoldierCatalogError;
	if (SoldierTable && SoldierTable->GetRowStruct() == FGuLiStrikeCommanderSoldiersRow::StaticStruct())
	{
		for (const FName Name : SoldierTable->GetRowNames())
		{
			bool bValid = false;
			FGuLiSoldierDefinition Definition = FGuLiCommanderSoldierResolver::Resolve(SoldierTable, Name, Fallback, bValid);
			if (bValid && !FindSoldierDefinition(Definition.UnitTypeId)) SoldierDefinitions.Add(Definition);
			else
			{
				SoldierCatalogError = FString::Printf(TEXT("Soldiers row '%s' is invalid or duplicates a UnitTypeId."), *Name.ToString());
				UE_LOG(LogGuLiStrike, Warning, TEXT("%s"), *SoldierCatalogError);
			}
		}
	}
	if (SoldierDefinitions.IsEmpty()) SoldierDefinitions.Add(DefaultSoldierDefinition);
	SoldierDefinitions.Sort([](const auto& A, const auto& B) { return A.UnitTypeId < B.UnitTypeId; });
	LoadSkillCatalog(Settings);
	if (!SoldierCatalogError.IsEmpty())
	{
		SkillCatalogError = SoldierCatalogError;
		SkillDefinitions.Reset(); UnitSkillConfigs.Reset();
		UE_LOG(LogGuLiStrike, Error, TEXT("Army skills disabled rather than selecting an ambiguous soldier row: %s"), *SkillCatalogError);
	}
}

const FGuLiSoldierDefinition* UGuLiCommanderDataSubsystem::FindSoldierDefinition(uint16 UnitTypeId) const
{
	return SoldierDefinitions.FindByPredicate([UnitTypeId](const auto& Definition) { return Definition.UnitTypeId == UnitTypeId; });
}

void UGuLiCommanderDataSubsystem::LoadSkillCatalog(const UGuLiCommanderDataSettings* Settings)
{
	UDataTable* Skills = Settings ? Settings->SkillDataTable.LoadSynchronous() : nullptr;
	UDataTable* UnitSkills = Settings ? Settings->UnitSkillDataTable.LoadSynchronous() : nullptr;
	if (!Skills || Skills->GetRowStruct() != FGuLiStrikeCommanderSkillsRow::StaticStruct()
		|| !UnitSkills || UnitSkills->GetRowStruct() != FGuLiStrikeCommanderUnitSkillsRow::StaticStruct())
	{
		SkillCatalogError = TEXT("Skills/UnitSkills DataTables missing or using an incompatible row structure.");
	}
	else
	{
		for (const FName Name : Skills->GetRowNames())
		{
			const auto* Row = Skills->FindRow<FGuLiStrikeCommanderSkillsRow>(Name, TEXT("ArmySkillCatalog"), false);
			if (!Row) continue;
			auto& Definition = SkillDefinitions.AddDefaulted_GetRef();
			Definition.SkillId = FName(*Row->SkillId.TrimStartAndEnd());
			Definition.DisplayName = Row->DisplayName;
			Definition.ExecutorId = FName(*Row->ExecutorId.TrimStartAndEnd());
			TArray<FString> Tags;
			Row->Tags.ParseIntoArray(Tags, TEXT(";"), true);
			for (const FString& Text : Tags)
			{
				const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Text.TrimStartAndEnd()), false);
				if (!Tag.IsValid()) { SkillCatalogError = FString::Printf(TEXT("Skill %s has unregistered tag '%s'."), *Row->SkillId, *Text); break; }
				Definition.Tags.AddTag(Tag);
			}
		}
		for (const FName Name : UnitSkills->GetRowNames())
		{
			const auto* Row = UnitSkills->FindRow<FGuLiStrikeCommanderUnitSkillsRow>(Name, TEXT("ArmySkillCatalog"), false);
			if (!Row) continue;
			if (Row->UnitTypeId < 1 || Row->UnitTypeId > MAX_uint16 || !FindSoldierDefinition(static_cast<uint16>(Row->UnitTypeId)))
			{ SkillCatalogError = FString::Printf(TEXT("UnitSkills row %s references unknown unit type %d."), *Name.ToString(), Row->UnitTypeId); break; }
			auto& Config = UnitSkillConfigs.AddDefaulted_GetRef();
			Config.UnitTypeId = static_cast<uint16>(Row->UnitTypeId);
			Config.SlotId = FName(*Row->SlotId.TrimStartAndEnd()); Config.SkillId = FName(*Row->SkillId.TrimStartAndEnd());
			Config.bDefault = Row->bDefault; Config.Damage = Row->Damage;
			Config.AttackRatePerSecond = Row->AttackRatePerSecond; Config.RangeCentimeters = Row->RangeCentimeters;
		}
		if (SkillCatalogError.IsEmpty()) FGuLiSkillResolver::ValidateCatalog(SkillDefinitions, UnitSkillConfigs, SkillCatalogError);
	}
	if (!SkillCatalogError.IsEmpty())
	{
		SkillDefinitions.Reset(); UnitSkillConfigs.Reset();
		UE_LOG(LogGuLiStrike, Error, TEXT("Army skills disabled: %s"), *SkillCatalogError);
	}
}
