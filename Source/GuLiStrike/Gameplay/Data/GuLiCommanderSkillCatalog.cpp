#include "Gameplay/Data/GuLiCommanderSkillCatalog.h"
#include "Gameplay/Data/GuLiCommanderDataSettings.h"
#include "Gameplay/Data/Generated/GuLiStrikeCommanderTableRows.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectTypes.h"
#include "Gameplay/Skills/GuLiSkillResolver.h"
#include "Engine/DataTable.h"

bool GuLiCommanderSkillCatalog::Resolve(const UGuLiCommanderDataSettings* Settings,
    TFunctionRef<bool(uint16)> HasUnit,TFunctionRef<const FGuLiSpellFieldConfig*(FName)> FindField,
    TArray<FGuLiSkillDefinition>& SkillDefinitions,TArray<FGuLiUnitSkillConfig>& UnitSkillConfigs,FString& Error)
{
	SkillDefinitions.Reset(); UnitSkillConfigs.Reset(); Error.Reset();
	UDataTable* Skills = Settings ? Settings->SkillDataTable.LoadSynchronous() : nullptr;
	UDataTable* UnitSkills = Settings ? Settings->UnitSkillDataTable.LoadSynchronous() : nullptr;
	if (!Skills || Skills->GetRowStruct() != FGuLiStrikeCommanderSkillsRow::StaticStruct()
		|| !UnitSkills || UnitSkills->GetRowStruct() != FGuLiStrikeCommanderUnitSkillsRow::StaticStruct())
	{
		Error = TEXT("Skills/UnitSkills DataTables missing or using an incompatible row structure.");
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
			Definition.EffectConfigId = FName(*Row->EffectConfigId.TrimStartAndEnd());
			if (!Definition.EffectConfigId.IsNone() && !FindField(Definition.EffectConfigId))
			{
				Error = FString::Printf(TEXT("Skill %s references unknown SpellFields row '%s'."), *Row->SkillId, *Definition.EffectConfigId.ToString());
				break;
			}
			TArray<FString> Tags;
			Row->Tags.ParseIntoArray(Tags, TEXT(";"), true);
			for (const FString& Text : Tags)
			{
				const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Text.TrimStartAndEnd()), false);
				if (!Tag.IsValid()) { Error = FString::Printf(TEXT("Skill %s has unregistered tag '%s'."), *Row->SkillId, *Text); break; }
				Definition.Tags.AddTag(Tag);
			}
		}
		for (const FName Name : UnitSkills->GetRowNames())
		{
			if (!Error.IsEmpty()) break;
			const auto* Row = UnitSkills->FindRow<FGuLiStrikeCommanderUnitSkillsRow>(Name, TEXT("ArmySkillCatalog"), false);
			if (!Row) continue;
			if (Row->UnitTypeId < 1 || Row->UnitTypeId > MAX_uint16 || !HasUnit(static_cast<uint16>(Row->UnitTypeId)))
			{ Error = FString::Printf(TEXT("UnitSkills row %s references unknown unit type %d."), *Name.ToString(), Row->UnitTypeId); break; }
			auto& Config = UnitSkillConfigs.AddDefaulted_GetRef();
			Config.UnitTypeId = static_cast<uint16>(Row->UnitTypeId);
			Config.SlotId = FName(*Row->SlotId.TrimStartAndEnd()); Config.SkillId = FName(*Row->SkillId.TrimStartAndEnd());
			Config.bDefault = Row->bDefault; Config.Damage = Row->Damage;
			const FString TriggerMode = Row->TriggerMode.TrimStartAndEnd();
			if (!TriggerMode.IsEmpty() && TriggerMode != TEXT("Automatic") && TriggerMode != TEXT("Active"))
			{ Error = FString::Printf(TEXT("UnitSkills row %s has invalid TriggerMode '%s'."), *Name.ToString(), *TriggerMode); break; }
			Config.TriggerMode = TriggerMode == TEXT("Active") ? EGuLiWeaponTriggerMode::Active : EGuLiWeaponTriggerMode::Automatic;
			const FGuLiSkillDefinition* Definition = SkillDefinitions.FindByPredicate([&](const auto& D){return D.SkillId==Config.SkillId;});
			if (Definition && !Definition->EffectConfigId.IsNone())
			{
				const FGuLiSpellFieldConfig* Field = FindField(Definition->EffectConfigId);
				if (!Field || !FMath::IsNearlyZero(Row->Damage))
				{
					Error = FString::Printf(TEXT("UnitSkills row %s must leave Damage at 0 because SpellFields row '%s' is authoritative."),
						*Name.ToString(), *Definition->EffectConfigId.ToString());
					break;
				}
				Config.Damage = Field->Damage;
			}
			Config.AttackRatePerSecond = Row->AttackRatePerSecond; Config.RangeCentimeters = Row->RangeCentimeters;
			Config.ProjectileSpeedCentimetersPerSecond = Row->ProjectileSpeedCentimetersPerSecond;
			Config.ProjectileLifetimeSeconds = Row->ProjectileLifetimeSeconds;
			Config.ProjectileSweepRadiusCentimeters = Row->ProjectileSweepRadiusCentimeters;
			Config.ProjectileSpreadAngleDegrees = Row->ProjectileSpreadAngleDegrees;
			Config.ProjectileCount = Row->ProjectileCount > 0 ? Row->ProjectileCount : 1;
		}
		if (Error.IsEmpty() && Settings)
		{
			TMap<uint16, TSet<FName>> SlotsByType;
			for (const auto& Config : UnitSkillConfigs) SlotsByType.FindOrAdd(Config.UnitTypeId).Add(Config.SlotId);
			const int32 MaximumSlots = Settings->MaximumWeaponSlotsPerUnit;
			if (MaximumSlots < 1 || MaximumSlots > 32)
				Error = TEXT("MaximumWeaponSlotsPerUnit must be between 1 and 32.");
			for (const auto& Pair : SlotsByType)
				if (Pair.Value.Num() > MaximumSlots) Error = TEXT("UnitSkills exceeds the configured channel limit.");
			TSet<FString> RuleKeys;
			for (const auto& Rule : Settings->WeaponSlotRules)
			{
				const FString Key = FString::Printf(TEXT("%d/%s"), Rule.UnitTypeId, *Rule.SlotId.ToString());
				if (Rule.UnitTypeId < 1 || Rule.UnitTypeId > MAX_uint16 || Rule.SlotId.IsNone()
					|| RuleKeys.Contains(Key) || !UnitSkillConfigs.ContainsByPredicate([&](const auto& Row)
						{ return Row.UnitTypeId == Rule.UnitTypeId && Row.SlotId == Rule.SlotId; }))
				{ Error = TEXT("WeaponSlotRules contains an unknown or duplicate type/slot."); break; }
				RuleKeys.Add(Key);
				for (auto& Row : UnitSkillConfigs)
					if (Row.UnitTypeId == Rule.UnitTypeId && Row.SlotId == Rule.SlotId)
						Row.bInitiallyUnlocked = Rule.bInitiallyUnlocked;
			}
		}
		if (Error.IsEmpty()) FGuLiSkillResolver::ValidateCatalog(SkillDefinitions, UnitSkillConfigs, Error);
	}
	if (!Error.IsEmpty())
	{
		SkillDefinitions.Reset(); UnitSkillConfigs.Reset();
	}

	return Error.IsEmpty();
}
