#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Gameplay/Data/Generated/GuLiStrikeSpellFieldsTableRows.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"

namespace
{
	bool ResolveCombatRow(FName Name, const FGuLiStrikeSpellFieldsFieldsRow& Row, FGuLiSpellFieldConfig& Out)
	{
		if (!Row.FieldType.Equals(TEXT("Combat"), ESearchCase::IgnoreCase)) return false;
		Out.ConfigId = Name; Out.Damage = Row.Damage; Out.Radius = Row.RadiusCentimeters;
		Out.Delay = Row.DelaySeconds; Out.Duration = Row.DurationSeconds;
		Out.PulseInterval = Row.PulseIntervalSeconds; Out.DissipationSeconds = Row.DissipationSeconds;
		if (Row.Timing.Equals(TEXT("Instant"), ESearchCase::IgnoreCase)) Out.Timing = EGuLiSpellFieldTiming::Instant;
		else if (Row.Timing.Equals(TEXT("Delayed"), ESearchCase::IgnoreCase)) Out.Timing = EGuLiSpellFieldTiming::Delayed;
		else if (Row.Timing.Equals(TEXT("Periodic"), ESearchCase::IgnoreCase)) Out.Timing = EGuLiSpellFieldTiming::Periodic;
		else return false;
		return Out.IsValid();
	}
}

UGuLiSpellFieldDataSettings::UGuLiSpellFieldDataSettings()
	: DataTable(FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeSpellFields_Fields.DT_GuLiStrikeSpellFields_Fields")))
{
}

bool UGuLiSpellFieldDataSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

void UGuLiSpellFieldDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UDataTable* Table = GetDefault<UGuLiSpellFieldDataSettings>()->DataTable.LoadSynchronous();
	if (!Table || Table->GetRowStruct() != FGuLiStrikeSpellFieldsFieldsRow::StaticStruct())
	{
		CatalogError = TEXT("The global SpellFields DataTable is missing or has an invalid row type.");
		return;
	}
	for (FName Name : Table->GetRowNames())
	{
		const auto& Row = *Table->FindRow<FGuLiStrikeSpellFieldsFieldsRow>(Name, TEXT("Global fields"));
		if (Row.FieldType.Equals(TEXT("Teleport"), ESearchCase::IgnoreCase))
		{
			FGuLiTeleportFieldConfig Field;
			Field.Level = Row.Level; Field.RadiusCentimeters = Row.RadiusCentimeters;
			Field.bAllowPlayerVehicles = Row.bAllowPlayerVehicles;
			Field.WindupSeconds = Row.WindupSeconds; Field.RecoverySeconds = Row.RecoverySeconds;
			Field.BeamHeightCentimeters = Row.BeamHeightCentimeters;
			Field.MaxTargetWaitSeconds = Row.MaxTargetWaitSeconds; Field.MaxShipHeightCentimeters = Row.MaxShipHeightCentimeters;
			if (Field.IsValid() && !FindTeleportField(Field.Level)) { TeleportFields.Add(Field); continue; }
		}
		else if (Row.FieldType == TEXT("StrongholdTransit"))
		{
			if (Row.LaneHeightCentimeters > 0 && Row.AscentSeconds > 0 && Row.AccelerationSeconds > 0
				&& Row.DecelerationSeconds > 0 && Row.ExitFlashSeconds > 0 && Row.SpeedMultiplier > 0 && Row.ExitRadiusCentimeters > 0
				&& !FindStrongholdTransit(Row.Id))
			{
				auto& Transit = StrongholdTransits.AddDefaulted_GetRef();
				Transit.Id = Row.Id; Transit.LaneHeight = Row.LaneHeightCentimeters;
				Transit.AscentSeconds = Row.AscentSeconds; Transit.AccelerationSeconds = Row.AccelerationSeconds;
				Transit.DecelerationSeconds = Row.DecelerationSeconds; Transit.ExitFlashSeconds = Row.ExitFlashSeconds;
				Transit.SpeedMultiplier = Row.SpeedMultiplier; Transit.ExitRadius = Row.ExitRadiusCentimeters;
				Transit.EnergyMaterial = TSoftObjectPtr<UMaterialInterface>(Row.EnergyMaterial.ToSoftObjectPath());
				Transit.TrailSystem = TSoftObjectPtr<UNiagaraSystem>(Row.TrailSystem.ToSoftObjectPath());
				Transit.FlashSystem = TSoftObjectPtr<UNiagaraSystem>(Row.FlashSystem.ToSoftObjectPath());
				continue;
			}
		}
		else
		{
			FGuLiSpellFieldConfig Field;
			if (ResolveCombatRow(Name, Row, Field)) { CombatFields.Add(Field); continue; }
		}
		CatalogError = FString::Printf(TEXT("Global field '%s' has invalid type, timing, values or duplicate teleport level."), *Name.ToString());
		CombatFields.Reset(); TeleportFields.Reset(); StrongholdTransits.Reset();
		return;
	}
}

const FGuLiStrongholdTransitConfig* UGuLiSpellFieldDataSubsystem::FindStrongholdTransit(int32 Id) const
{
	return StrongholdTransits.FindByPredicate([Id](const auto& Transit) { return Transit.Id == Id; });
}

const FGuLiSpellFieldConfig* UGuLiSpellFieldDataSubsystem::FindCombatField(const FName ConfigId) const
{
	return CombatFields.FindByPredicate([ConfigId](const auto& Field) { return Field.ConfigId == ConfigId; });
}

const FGuLiTeleportFieldConfig* UGuLiSpellFieldDataSubsystem::FindTeleportField(const int32 Level) const
{
	return TeleportFields.FindByPredicate([Level](const auto& Field) { return Field.Level == Level; });
}

bool UGuLiSpellFieldDataSubsystem::ResolveAuthoredConfig(const FName ConfigId, FGuLiSpellFieldConfig& OutConfig)
{
	UDataTable* Table = GetDefault<UGuLiSpellFieldDataSettings>()->DataTable.LoadSynchronous();
	if (!Table || Table->GetRowStruct() != FGuLiStrikeSpellFieldsFieldsRow::StaticStruct()) return false;
	const auto* Row = Table->FindRow<FGuLiStrikeSpellFieldsFieldsRow>(ConfigId, TEXT("Authored field"), false);
	return Row && ResolveCombatRow(ConfigId, *Row, OutConfig);
}

bool UGuLiSpellFieldDataSubsystem::GetCombatField(const FName ConfigId, FGuLiSpellFieldConfig& OutConfig) const
{
	const auto* Field = FindCombatField(ConfigId);
	if (!Field) return false;
	OutConfig = *Field;
	return true;
}
