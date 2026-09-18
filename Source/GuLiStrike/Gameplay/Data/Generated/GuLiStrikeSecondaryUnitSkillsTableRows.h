// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeSecondaryUnitSkills.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeSecondaryUnitSkillsTableRows.generated.h"

/** DataTable DT_GuLiStrikeSecondaryUnitSkills_Skills 的行结构（源: GuLiStrikeSecondaryUnitSkills.xlsx / Skills）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeSecondaryUnitSkillsSkillsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString Note;

	/** TargetMode (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString TargetMode;

	/** CooldownSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float CooldownSeconds = 0.0f;

	/** RangeCentimeters (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float RangeCentimeters = 0.0f;

	/** RangeSourceSlot (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString RangeSourceSlot;

	/** RangeMultiplier (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float RangeMultiplier = 0.0f;

	/** MaximumLevel (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	int32 MaximumLevel = 0;

	/** ExecutorClass (softclass, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	TSoftClassPtr<UObject> ExecutorClass;

	/** ConfigurationClass (softclass, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	TSoftClassPtr<UObject> ConfigurationClass;

	/** Configuration (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	TSoftObjectPtr<UObject> Configuration;

	/** Projectile (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	TSoftObjectPtr<UObject> Projectile;

	/** FieldConfigId (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString FieldConfigId;

	/** SourceWeaponSlot (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	FString SourceWeaponSlot;

	/** UseAuthoredTrajectory (bool, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	bool UseAuthoredTrajectory = false;

	/** GroundWarningStyle (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	TSoftObjectPtr<UObject> GroundWarningStyle;

	/** TargetAreaDiameterCentimeters (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Skills")
	float TargetAreaDiameterCentimeters = 0.0f;

};

/** DataTable DT_GuLiStrikeSecondaryUnitSkills_UnitSkills 的行结构（源: GuLiStrikeSecondaryUnitSkills.xlsx / UnitSkills）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeSecondaryUnitSkillsUnitSkillsRow : public FTableRowBase
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

	/** SkillId (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="UnitSkills")
	FString SkillId;

};
