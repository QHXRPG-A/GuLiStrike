// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "GameplayTagContainer.h"
#include "GuLiSkillTypes.generated.h"

UENUM()
enum class EGuLiWeaponTriggerMode : uint8 { Automatic, Active };

USTRUCT()
struct GULISTRIKE_API FGuLiSkillDefinition
{
	GENERATED_BODY()
	UPROPERTY() FName SkillId;
	UPROPERTY() FString DisplayName;
	UPROPERTY() FName ExecutorId;
	/** Optional SpellFields row supplying the single authored base damage and field timing/range. */
	UPROPERTY() FName EffectConfigId;
	UPROPERTY() FGameplayTagContainer Tags;
};

USTRUCT()
struct GULISTRIKE_API FGuLiUnitSkillConfig
{
	GENERATED_BODY()
	UPROPERTY() uint16 UnitTypeId = 1;
	UPROPERTY() FName SlotId = TEXT("BasicAttack");
	UPROPERTY() FName SkillId;
	UPROPERTY() bool bDefault = false;
	UPROPERTY() EGuLiWeaponTriggerMode TriggerMode = EGuLiWeaponTriggerMode::Automatic;
	UPROPERTY() float Damage = 0.0f;
	UPROPERTY() float AttackRatePerSecond = 0.0f;
	UPROPERTY() float RangeCentimeters = 0.0f;
	/** Authored availability; rewards may unlock this stable slot during the match. */
	UPROPERTY() bool bInitiallyUnlocked = true;
	UPROPERTY() float ProjectileSpeedCentimetersPerSecond = 0.0f;
	UPROPERTY() float ProjectileLifetimeSeconds = 0.0f;
	UPROPERTY() float ProjectileSweepRadiusCentimeters = 0.0f;
};

/** Final authority configuration; per-soldier target/cooldown/health never live here. */
USTRUCT()
struct GULISTRIKE_API FGuLiResolvedSkillProfile
{
	GENERATED_BODY()
	UPROPERTY() EGuLiTeam Team = EGuLiTeam::Unassigned;
	UPROPERTY() uint16 UnitTypeId = 1;
	UPROPERTY() FName SlotId = TEXT("BasicAttack");
	UPROPERTY() FName SkillId;
	UPROPERTY() FName ExecutorId;
	UPROPERTY() FGameplayTagContainer Tags;
	UPROPERTY() float Damage = 0.0f;
	UPROPERTY() float AttackRatePerSecond = 0.0f;
	UPROPERTY() float RangeCentimeters = 0.0f;
	UPROPERTY() uint32 Revision = 0;
	UPROPERTY() bool bUnlocked = true;
	UPROPERTY() bool bEquipped = true;
	UPROPERTY() EGuLiWeaponTriggerMode TriggerMode = EGuLiWeaponTriggerMode::Automatic;
	UPROPERTY() float ProjectileSpeedCentimetersPerSecond = 0.0f;
	UPROPERTY() float ProjectileLifetimeSeconds = 0.0f;
	UPROPERTY() float ProjectileSweepRadiusCentimeters = 0.0f;
	bool HasSameConfiguration(const FGuLiResolvedSkillProfile& Other) const;
};

UENUM()
enum class EGuLiSkillAttribute : uint8 { Damage, AttackRate, Range };
UENUM()
enum class EGuLiSkillModifierOperation : uint8 { AddFlat, AddPercent };

USTRUCT()
struct GULISTRIKE_API FGuLiSkillTargetSelector
{
	GENERATED_BODY()
	/** Empty explicitly means every authored unit type in this team. */
	UPROPERTY() TArray<uint16> UnitTypeIds;
	UPROPERTY() FName SlotId = TEXT("BasicAttack");
	/** Final-skill filters apply to modifiers only, never chained replacements. */
	UPROPERTY() FName RequiredSkillId;
	UPROPERTY() FGameplayTagContainer RequiredTags;
	bool MatchesUnitSlot(uint16 UnitTypeId, FName InSlotId) const;
	bool MatchesFinalSkill(const FGuLiSkillDefinition& Definition) const;
};

USTRUCT()
struct GULISTRIKE_API FGuLiSkillModifier
{
	GENERATED_BODY()
	UPROPERTY() FGuLiSkillTargetSelector Target;
	UPROPERTY() EGuLiSkillAttribute Attribute = EGuLiSkillAttribute::Damage;
	UPROPERTY() EGuLiSkillModifierOperation Operation = EGuLiSkillModifierOperation::AddFlat;
	/** AddPercent uses fraction units: 0.2 means +20 percent. */
	UPROPERTY() float Magnitude = 0.0f;
};

USTRUCT()
struct GULISTRIKE_API FGuLiSkillSlotReplacement
{
	GENERATED_BODY()
	UPROPERTY() FGuLiSkillTargetSelector Target;
	UPROPERTY() FName SkillId;
	UPROPERTY() int32 Priority = 0;
};

USTRUCT()
struct GULISTRIKE_API FGuLiSkillSlotUnlock
{
	GENERATED_BODY()
	UPROPERTY() FGuLiSkillTargetSelector Target;
};

/** Explicit equipment choice. Empty SkillId means an intentionally unequipped slot. */
USTRUCT()
struct GULISTRIKE_API FGuLiSkillLoadoutSelection
{
	GENERATED_BODY()
	UPROPERTY() uint16 UnitTypeId = 1;
	UPROPERTY() FName SlotId = TEXT("BasicAttack");
	UPROPERTY() FName SkillId;
};

/** Opaque neutral source; no technology/card product concepts in the skill bridge. */
USTRUCT()
struct GULISTRIKE_API FGuLiSkillSource
{
	GENERATED_BODY()
	UPROPERTY() FGuid SourceInstanceId;
	UPROPERTY() FString DebugLabel;
	UPROPERTY() TArray<FGuLiSkillModifier> Modifiers;
	UPROPERTY() TArray<FGuLiSkillSlotReplacement> Replacements;
	UPROPERTY() TArray<FGuLiSkillSlotUnlock> Unlocks;
};

USTRUCT()
struct GULISTRIKE_API FGuLiSkillNumericOverride
{
	GENERATED_BODY()
	UPROPERTY() uint16 UnitTypeId = 1;
	UPROPERTY() FName SlotId = TEXT("BasicAttack");
	UPROPERTY() bool bOverrideDamage = false;
	UPROPERTY() float Damage = 0.0f;
	UPROPERTY() bool bOverrideAttackRate = false;
	UPROPERTY() float AttackRatePerSecond = 0.0f;
	UPROPERTY() bool bOverrideRange = false;
	UPROPERTY() float RangeCentimeters = 0.0f;
};

UENUM()
enum class EGuLiArmySkillCommand : uint8 { UpsertSource, RemoveSource, SetNumericOverride, ClearNumericOverride, ClearAll };

USTRUCT()
struct GULISTRIKE_API FGuLiArmySkillCommand
{
	GENERATED_BODY()
	UPROPERTY() EGuLiArmySkillCommand Command = EGuLiArmySkillCommand::UpsertSource;
	UPROPERTY() FGuLiSkillSource Source;
	UPROPERTY() FGuid SourceInstanceId;
	UPROPERTY() FGuLiSkillNumericOverride NumericOverride;
};

/** Extension contract only. New effect implementations register explicitly, not per combination. */
struct GULISTRIKE_API FGuLiSkillEffectContext
{
	const FGuLiResolvedSkillProfile* Profile = nullptr;
	uint32 AttackerId = 0;
	uint32 TargetId = 0;
	float AppliedDamage = 0.0f;
};
using FGuLiSkillEffectHook = TFunction<void(const FGuLiSkillEffectContext&)>;

/** Team is supplied by the authority call; this key identifies one cached slot within it. */
struct GULISTRIKE_API FGuLiSkillSlotKey
{
	uint16 UnitTypeId = 1;
	FName SlotId = TEXT("BasicAttack");
	bool operator==(const FGuLiSkillSlotKey& Other) const
	{
		return UnitTypeId == Other.UnitTypeId && SlotId == Other.SlotId;
	}
};
