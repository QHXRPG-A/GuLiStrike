// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"

#include "Gameplay/Data/GuLiCommanderDataSettings.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Gameplay/Data/GuLiSpellFieldDataSubsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Gameplay/Data/Generated/GuLiStrikeCommanderTableRows.h"
#include "Gameplay/Skills/GuLiSkillResolver.h"
#include "GuLiStrike.h"

namespace
{
	struct FWeaponMountKey
	{
		uint16 UnitTypeId = 0;
		FName SlotId;
		bool operator==(const FWeaponMountKey& Other) const
		{
			return UnitTypeId == Other.UnitTypeId && SlotId == Other.SlotId;
		}
		friend uint32 GetTypeHash(const FWeaponMountKey& Key)
		{
			return HashCombine(GetTypeHash(Key.UnitTypeId), GetTypeHash(Key.SlotId));
		}
	};

	struct FWeaponMountBuilder
	{
		TMap<int32, FVector> Muzzles;
	};


}

bool UGuLiCommanderDataSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World != nullptr && World->IsGameWorld();
}

void UGuLiCommanderDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGuLiUnitDataSubsystem>();
	Collection.InitializeDependency<UGuLiSpellFieldDataSubsystem>();
	Units = GetWorld()->GetSubsystem<UGuLiUnitDataSubsystem>();
	Fields = GetWorld()->GetSubsystem<UGuLiSpellFieldDataSubsystem>();
	const auto* Settings = GetDefault<UGuLiCommanderDataSettings>();
	LoadSkillCatalog(Settings);
	if (!Units->IsCatalogValid())
	{
		SkillCatalogError = Units->GetCatalogError();
		SkillDefinitions.Reset(); UnitSkillConfigs.Reset();
	}
	LoadWeaponMountCatalog(Settings);
}

const FGuLiTeleportFieldConfig* UGuLiCommanderDataSubsystem::FindTeleportFieldConfig(const int32 Level) const
{
	return Fields->FindTeleportField(Level);
}



const FGuLiSoldierDefinition* UGuLiCommanderDataSubsystem::FindSoldierDefinition(const uint16 UnitTypeId) const
{
	return Units->FindDefinition(UnitTypeId);
}

const FGuLiSkillDefinition* UGuLiCommanderDataSubsystem::FindSkillDefinition(const FName SkillId) const
{
	return SkillDefinitions.FindByPredicate([SkillId](const auto& Definition) { return Definition.SkillId == SkillId; });
}

const FGuLiSpellFieldConfig* UGuLiCommanderDataSubsystem::FindSpellFieldConfig(const FName ConfigId) const
{
	return Fields->FindCombatField(ConfigId);
}

const FGuLiWeaponMountConfig* UGuLiCommanderDataSubsystem::FindWeaponMountConfig(
	const uint16 UnitTypeId, const FName SlotId) const
{
	return WeaponMountConfigs.FindByPredicate([UnitTypeId, SlotId](const FGuLiWeaponMountConfig& Config)
	{
		return Config.UnitTypeId == UnitTypeId && Config.SlotId == SlotId;
	});
}

const FVector* UGuLiCommanderDataSubsystem::FindAimOffset(const uint16 UnitTypeId) const
{
	if (const FGuLiWeaponMountConfig* Config = WeaponMountConfigs.FindByPredicate(
		[UnitTypeId](const FGuLiWeaponMountConfig& Candidate) { return Candidate.UnitTypeId == UnitTypeId; }))
	{
		return &Config->AimOffset;
	}
	return nullptr;
}



void UGuLiCommanderDataSubsystem::LoadSkillCatalog(const UGuLiCommanderDataSettings* Settings)
{
	SkillDefinitions.Reset(); UnitSkillConfigs.Reset(); SkillCatalogError.Reset();
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
			Definition.EffectConfigId = FName(*Row->EffectConfigId.TrimStartAndEnd());
			if (!Definition.EffectConfigId.IsNone() && !FindSpellFieldConfig(Definition.EffectConfigId))
			{
				SkillCatalogError = FString::Printf(TEXT("Skill %s references unknown SpellFields row '%s'."), *Row->SkillId, *Definition.EffectConfigId.ToString());
				break;
			}
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
			if (!SkillCatalogError.IsEmpty()) break;
			const auto* Row = UnitSkills->FindRow<FGuLiStrikeCommanderUnitSkillsRow>(Name, TEXT("ArmySkillCatalog"), false);
			if (!Row) continue;
			if (Row->UnitTypeId < 1 || Row->UnitTypeId > MAX_uint16 || !FindSoldierDefinition(static_cast<uint16>(Row->UnitTypeId)))
			{ SkillCatalogError = FString::Printf(TEXT("UnitSkills row %s references unknown unit type %d."), *Name.ToString(), Row->UnitTypeId); break; }
			auto& Config = UnitSkillConfigs.AddDefaulted_GetRef();
			Config.UnitTypeId = static_cast<uint16>(Row->UnitTypeId);
			Config.SlotId = FName(*Row->SlotId.TrimStartAndEnd()); Config.SkillId = FName(*Row->SkillId.TrimStartAndEnd());
			Config.bDefault = Row->bDefault; Config.Damage = Row->Damage;
			const FString TriggerMode = Row->TriggerMode.TrimStartAndEnd();
			if (!TriggerMode.IsEmpty() && TriggerMode != TEXT("Automatic") && TriggerMode != TEXT("Active"))
			{ SkillCatalogError = FString::Printf(TEXT("UnitSkills row %s has invalid TriggerMode '%s'."), *Name.ToString(), *TriggerMode); break; }
			Config.TriggerMode = TriggerMode == TEXT("Active") ? EGuLiWeaponTriggerMode::Active : EGuLiWeaponTriggerMode::Automatic;
			const FGuLiSkillDefinition* Definition = FindSkillDefinition(Config.SkillId);
			if (Definition && !Definition->EffectConfigId.IsNone())
			{
				const FGuLiSpellFieldConfig* Field = FindSpellFieldConfig(Definition->EffectConfigId);
				if (!Field || !FMath::IsNearlyZero(Row->Damage))
				{
					SkillCatalogError = FString::Printf(TEXT("UnitSkills row %s must leave Damage at 0 because SpellFields row '%s' is authoritative."),
						*Name.ToString(), *Definition->EffectConfigId.ToString());
					break;
				}
				Config.Damage = Field->Damage;
			}
			Config.AttackRatePerSecond = Row->AttackRatePerSecond; Config.RangeCentimeters = Row->RangeCentimeters;
			Config.ProjectileSpeedCentimetersPerSecond = Row->ProjectileSpeedCentimetersPerSecond;
			Config.ProjectileLifetimeSeconds = Row->ProjectileLifetimeSeconds;
			Config.ProjectileSweepRadiusCentimeters = Row->ProjectileSweepRadiusCentimeters;
		}
		if (SkillCatalogError.IsEmpty() && Settings)
		{
			TMap<uint16, TSet<FName>> SlotsByType;
			for (const auto& Config : UnitSkillConfigs) SlotsByType.FindOrAdd(Config.UnitTypeId).Add(Config.SlotId);
			const int32 MaximumSlots = Settings->MaximumWeaponSlotsPerUnit;
			if (MaximumSlots < 1 || MaximumSlots > 32)
				SkillCatalogError = TEXT("MaximumWeaponSlotsPerUnit must be between 1 and 32.");
			for (const auto& Pair : SlotsByType)
				if (Pair.Value.Num() > MaximumSlots) SkillCatalogError = TEXT("UnitSkills exceeds the configured channel limit.");
			TSet<FString> RuleKeys;
			for (const auto& Rule : Settings->WeaponSlotRules)
			{
				const FString Key = FString::Printf(TEXT("%d/%s"), Rule.UnitTypeId, *Rule.SlotId.ToString());
				if (Rule.UnitTypeId < 1 || Rule.UnitTypeId > MAX_uint16 || Rule.SlotId.IsNone()
					|| RuleKeys.Contains(Key) || !UnitSkillConfigs.ContainsByPredicate([&](const auto& Row)
						{ return Row.UnitTypeId == Rule.UnitTypeId && Row.SlotId == Rule.SlotId; }))
				{ SkillCatalogError = TEXT("WeaponSlotRules contains an unknown or duplicate type/slot."); break; }
				RuleKeys.Add(Key);
				for (auto& Row : UnitSkillConfigs)
					if (Row.UnitTypeId == Rule.UnitTypeId && Row.SlotId == Rule.SlotId)
						Row.bInitiallyUnlocked = Rule.bInitiallyUnlocked;
			}
		}
		if (SkillCatalogError.IsEmpty()) FGuLiSkillResolver::ValidateCatalog(SkillDefinitions, UnitSkillConfigs, SkillCatalogError);
	}
	if (!SkillCatalogError.IsEmpty())
	{
		SkillDefinitions.Reset(); UnitSkillConfigs.Reset();
		UE_LOG(LogGuLiStrike, Error, TEXT("Army skills disabled: %s"), *SkillCatalogError);
	}
}

void UGuLiCommanderDataSubsystem::LoadWeaponMountCatalog(const UGuLiCommanderDataSettings* Settings)
{
	WeaponMountConfigs.Reset();
	WeaponMountCatalogError.Reset();
	if (!SkillCatalogError.IsEmpty())
	{
		WeaponMountCatalogError = TEXT("WeaponMounts requires a valid Soldiers/Skills/UnitSkills catalog.");
	}
	UDataTable* Table = Settings ? Settings->WeaponMountDataTable.LoadSynchronous() : nullptr;
	if (WeaponMountCatalogError.IsEmpty()
		&& (!Table || Table->GetRowStruct() != FGuLiStrikeCommanderWeaponMountsRow::StaticStruct()))
	{
		WeaponMountCatalogError = TEXT("WeaponMounts DataTable missing or using an incompatible row structure.");
	}

	TMap<uint16, FVector> AimOffsets;
	TMap<FWeaponMountKey, FWeaponMountBuilder> Builders;
	if (WeaponMountCatalogError.IsEmpty())
	{
		for (const FName Name : Table->GetRowNames())
		{
			const auto* Row = Table->FindRow<FGuLiStrikeCommanderWeaponMountsRow>(Name, TEXT("WeaponMountCatalog"), false);
			if (!Row) continue;
			const FString Role = Row->PointRole.TrimStartAndEnd();
			const FString SlotText = Row->SlotId.TrimStartAndEnd();
			const FString SocketName = Row->SocketName.TrimStartAndEnd();
			if (Row->UnitTypeId < 1 || Row->UnitTypeId > MAX_uint16
				|| !FindSoldierDefinition(static_cast<uint16>(Row->UnitTypeId)))
			{
				WeaponMountCatalogError = FString::Printf(TEXT("WeaponMounts row '%s' references unknown unit type %d."),
					*Name.ToString(), Row->UnitTypeId);
				break;
			}
			if (!Row->bCalibrated || SocketName.IsEmpty() || !SocketName.StartsWith(TEXT("FX_"))
				|| Row->Offset.ContainsNaN() || Row->Offset.GetAbsMax() > 1000000.0)
			{
				WeaponMountCatalogError = FString::Printf(TEXT("WeaponMounts row '%s' has an invalid calibration, SocketName, or offset."),
					*Name.ToString());
				break;
			}

			const uint16 UnitTypeId = static_cast<uint16>(Row->UnitTypeId);
			// Tables retain source-mesh local socket coordinates. The runtime catalog
			// freezes gameplay-centimeter offsets; emitters use unit-scale logical poses.
			const FVector ResolvedOffset = FindSoldierDefinition(UnitTypeId)->ResolveModelOffsetCentimeters(Row->Offset);
			if (Role.Equals(TEXT("AimTarget"), ESearchCase::IgnoreCase))
			{
				if (!SlotText.IsEmpty() || Row->PointIndex != 0 || AimOffsets.Contains(UnitTypeId))
				{
					WeaponMountCatalogError = FString::Printf(TEXT("WeaponMounts row '%s' duplicates or misidentifies a unit AimTarget."),
						*Name.ToString());
					break;
				}
				AimOffsets.Add(UnitTypeId, ResolvedOffset);
			}
			else if (Role.Equals(TEXT("Muzzle"), ESearchCase::IgnoreCase))
			{
				if (SlotText.IsEmpty() || Row->PointIndex < 0 || Row->PointIndex > MAX_uint8)
				{
					WeaponMountCatalogError = FString::Printf(TEXT("WeaponMounts row '%s' has an invalid muzzle slot or index."),
						*Name.ToString());
					break;
				}
				const FWeaponMountKey Key{UnitTypeId, FName(*SlotText)};
				FWeaponMountBuilder& Builder = Builders.FindOrAdd(Key);
				if (Builder.Muzzles.Contains(Row->PointIndex))
				{
					WeaponMountCatalogError = FString::Printf(TEXT("WeaponMounts row '%s' duplicates muzzle index %d for %d/%s."),
						*Name.ToString(), Row->PointIndex, UnitTypeId, *SlotText);
					break;
				}
				Builder.Muzzles.Add(Row->PointIndex, ResolvedOffset);
			}
			else
			{
				WeaponMountCatalogError = FString::Printf(TEXT("WeaponMounts row '%s' has unknown PointRole '%s'."),
					*Name.ToString(), *Role);
				break;
			}
		}
	}

	if (WeaponMountCatalogError.IsEmpty() && (AimOffsets.IsEmpty() || Builders.IsEmpty()))
	{
		WeaponMountCatalogError = TEXT("WeaponMounts must contain at least one AimTarget and one Muzzle.");
	}
	if (WeaponMountCatalogError.IsEmpty())
	{
		for (const TPair<FWeaponMountKey, FWeaponMountBuilder>& Pair : Builders)
		{
			const FVector* AimOffset = AimOffsets.Find(Pair.Key.UnitTypeId);
			const bool bKnownSlot = UnitSkillConfigs.ContainsByPredicate([&Pair](const FGuLiUnitSkillConfig& Config)
			{
				return Config.UnitTypeId == Pair.Key.UnitTypeId && Config.SlotId == Pair.Key.SlotId;
			});
			if (!AimOffset || !bKnownSlot || Pair.Value.Muzzles.IsEmpty())
			{
				WeaponMountCatalogError = FString::Printf(TEXT("WeaponMounts %d/%s lacks an AimTarget, muzzle, or matching UnitSkills slot."),
					Pair.Key.UnitTypeId, *Pair.Key.SlotId.ToString());
				break;
			}
			FGuLiWeaponMountConfig& Config = WeaponMountConfigs.AddDefaulted_GetRef();
			Config.UnitTypeId = Pair.Key.UnitTypeId;
			Config.SlotId = Pair.Key.SlotId;
			Config.AimOffset = *AimOffset;
			Config.bCalibrated = true;
			Config.Muzzles.SetNum(Pair.Value.Muzzles.Num());
			for (int32 Index = 0; Index < Config.Muzzles.Num(); ++Index)
			{
				const FVector* Point = Pair.Value.Muzzles.Find(Index);
				if (!Point)
				{
					WeaponMountCatalogError = FString::Printf(TEXT("WeaponMounts %d/%s muzzle indices must be contiguous from zero."),
						Pair.Key.UnitTypeId, *Pair.Key.SlotId.ToString());
					break;
				}
				Config.Muzzles[Index] = *Point;
			}
			if (!WeaponMountCatalogError.IsEmpty() || !Config.IsValid())
			{
				if (WeaponMountCatalogError.IsEmpty()) WeaponMountCatalogError = TEXT("WeaponMounts produced an invalid runtime mount.");
				break;
			}
		}
	}
	if (WeaponMountCatalogError.IsEmpty())
	{
		for (const FGuLiUnitSkillConfig& Skill : UnitSkillConfigs)
		{
			if (!FindWeaponMountConfig(Skill.UnitTypeId, Skill.SlotId))
			{
				WeaponMountCatalogError = FString::Printf(TEXT("WeaponMounts is missing the UnitSkills slot %d/%s."),
					Skill.UnitTypeId, *Skill.SlotId.ToString());
				break;
			}
		}
	}
	if (WeaponMountCatalogError.IsEmpty())
	{
		WeaponMountConfigs.Sort([](const FGuLiWeaponMountConfig& A, const FGuLiWeaponMountConfig& B)
		{
			return A.UnitTypeId != B.UnitTypeId ? A.UnitTypeId < B.UnitTypeId : A.SlotId.ToString() < B.SlotId.ToString();
		});
	}
	else
	{
		WeaponMountConfigs.Reset();
		UE_LOG(LogGuLiStrike, Error, TEXT("Commander weapon effects disabled: %s"), *WeaponMountCatalogError);
	}
}

const TArray<FGuLiSoldierDefinition>& UGuLiCommanderDataSubsystem::GetSoldierDefinitions() const { return Units->GetMassDefinitions(); }
const FGuLiSoldierDefinition& UGuLiCommanderDataSubsystem::GetDefaultSoldierDefinition() const { return Units->GetDefaultDefinition(); }
bool UGuLiCommanderDataSubsystem::IsDefaultSoldierDefinitionFromDataTable() const { return Units->IsDefaultFromTable(); }
const TArray<FGuLiSpellFieldConfig>& UGuLiCommanderDataSubsystem::GetSpellFieldConfigs() const { return Fields->GetCombatFields(); }
bool UGuLiCommanderDataSubsystem::IsSpellFieldCatalogValid() const { return Fields->IsCatalogValid(); }
const FString& UGuLiCommanderDataSubsystem::GetSpellFieldCatalogError() const { return Fields->GetCatalogError(); }
bool UGuLiCommanderDataSubsystem::IsTeleportCatalogValid() const { return Fields->IsTeleportCatalogValid(); }
