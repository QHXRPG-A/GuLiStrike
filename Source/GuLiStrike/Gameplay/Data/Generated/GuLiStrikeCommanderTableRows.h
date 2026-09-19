// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeCommander.xlsx, GuLiStrikeSecondaryWeapons.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeCommanderTableRows.generated.h"

/** DataTable DT_GuLiStrikeCommander_Soldiers 的行结构（源: GuLiStrikeCommander.xlsx / Soldiers）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeCommanderSoldiersRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	FString Note;

	/** MovementSpeedCmPerSecond (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	float MovementSpeedCmPerSecond = 0.0f;

	/** MaxHealth (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	float MaxHealth = 0.0f;

	/** ModelAsset (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	TSoftObjectPtr<UObject> ModelAsset;

	/** Defense (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	float Defense = 0.0f;

	/** ActorClass (softclass, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	TSoftClassPtr<UObject> ActorClass;

	/** PresentationClass (softclass, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	TSoftClassPtr<UObject> PresentationClass;

	/** PresentationScale (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	float PresentationScale = 0.0f;

	/** DisplayName (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	FString DisplayName;

	/** ModelWidthMeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	float ModelWidthMeters = 0.0f;

	/** MinAvoidanceDistanceMeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	float MinAvoidanceDistanceMeters = 0.0f;

};

/** DataTable DT_GuLiStrikeCommander_Skills 的行结构（源: GuLiStrikeSecondaryWeapons.xlsx / Skills）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeCommanderSkillsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString Note;

	/** SkillId (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString SkillId;

	/** DisplayName (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString DisplayName;

	/** ExecutorId (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString ExecutorId;

	/** Tags (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString Tags;

	/** 产生的法术场 (Fields.id) -> EffectConfigId (Fields.name；导出时解析) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString EffectConfigId;

};

/** DataTable DT_GuLiStrikeCommander_UnitSkills 的行结构（源: GuLiStrikeSecondaryWeapons.xlsx / UnitSkills）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeCommanderUnitSkillsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UnitSkills")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UnitSkills")
	FString Note;

	/** UnitTypeId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UnitSkills")
	int32 UnitTypeId = 0;

	/** SlotId (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UnitSkills")
	FString SlotId;

	/** SkillId (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UnitSkills")
	FString SkillId;

	/** bDefault (bool, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UnitSkills")
	bool bDefault = false;

	/** Damage (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UnitSkills")
	float Damage = 0.0f;

	/** AttackRatePerSecond (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UnitSkills")
	float AttackRatePerSecond = 0.0f;

	/** RangeCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UnitSkills")
	float RangeCentimeters = 0.0f;

	/** TriggerMode (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UnitSkills")
	FString TriggerMode;

	/** ProjectileSpeedCentimetersPerSecond (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UnitSkills")
	float ProjectileSpeedCentimetersPerSecond = 0.0f;

	/** ProjectileLifetimeSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UnitSkills")
	float ProjectileLifetimeSeconds = 0.0f;

	/** ProjectileSweepRadiusCentimeters (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UnitSkills")
	float ProjectileSweepRadiusCentimeters = 0.0f;

};

/** DataTable DT_GuLiStrikeCommander_WeaponMounts 的行结构（源: GuLiStrikeSecondaryWeapons.xlsx / WeaponMounts）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeCommanderWeaponMountsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WeaponMounts")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WeaponMounts")
	FString Note;

	/** UnitTypeId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WeaponMounts")
	int32 UnitTypeId = 0;

	/** SlotId (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WeaponMounts")
	FString SlotId;

	/** PointRole (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WeaponMounts")
	FString PointRole;

	/** PointIndex (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WeaponMounts")
	int32 PointIndex = 0;

	/** SocketName (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WeaponMounts")
	FString SocketName;

	/** OffsetX/OffsetY/OffsetZ (float) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WeaponMounts")
	FVector Offset = FVector::ZeroVector;

	/** bCalibrated (bool, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WeaponMounts")
	bool bCalibrated = false;

};
