// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeMech.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeMechTableRows.generated.h"

/** DataTable DT_GuLiStrikeMech_Upgrades 的行结构（源: GuLiStrikeMech.xlsx / 升级表）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeMechUpgradesRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrades")
	FString Id;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrades")
	FString Note;

	/** SkillId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrades")
	int32 SkillId = 0;

	/** Level (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrades")
	int32 Level = 0;

	/** FireRate (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrades")
	float FireRate = 0.0f;

	/** Damage (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Upgrades")
	float Damage = 0.0f;

};

/** DataTable DT_GuLiStrikeMech_Skills 的行结构（源: GuLiStrikeMech.xlsx / 技能表）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeMechSkillsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString Note;

	/** ProjectileSpeed (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float ProjectileSpeed = 0.0f;

	/** ProjectileLifetime (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float ProjectileLifetime = 0.0f;

	/** SweepRadius (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float SweepRadius = 0.0f;

	/** MuzzleSocket (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString MuzzleSocket;

	/** RecoilBone (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString RecoilBone;

	/** RecoilTargetLocalZCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float RecoilTargetLocalZCentimeters = 0.0f;

	/** RecoilDuration (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float RecoilDuration = 0.0f;

	/** RecoilCurve (softobject, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	TSoftObjectPtr<UObject> RecoilCurve;

	/** WeaponAnimation (softclass, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	TSoftClassPtr<UObject> WeaponAnimation;

	/** BulletSystem (softobject, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	TSoftObjectPtr<UObject> BulletSystem;

	/** MuzzleSystem (softobject, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	TSoftObjectPtr<UObject> MuzzleSystem;

};
