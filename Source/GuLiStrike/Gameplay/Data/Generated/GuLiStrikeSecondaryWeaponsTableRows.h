// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeSecondaryWeapons.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeSecondaryWeaponsTableRows.generated.h"

/** DataTable DT_GuLiStrikeSecondaryWeapons_Projectiles 的行结构（源: GuLiStrikeSecondaryWeapons.xlsx / Projectiles）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeSecondaryWeaponsProjectilesRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectiles")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectiles")
	FString Note;

	/** ProjectileAsset (softobject, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectiles")
	TSoftObjectPtr<UObject> ProjectileAsset;

	/** SpeedCentimetersPerSecond (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectiles")
	float SpeedCentimetersPerSecond = 0.0f;

	/** LiftSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectiles")
	float LiftSeconds = 0.0f;

	/** MinimumLiftHeightCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectiles")
	float MinimumLiftHeightCentimeters = 0.0f;

	/** MaximumLiftHeightCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectiles")
	float MaximumLiftHeightCentimeters = 0.0f;

	/** LateralOffsetCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectiles")
	float LateralOffsetCentimeters = 0.0f;

	/** ConvergenceDistanceCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectiles")
	float ConvergenceDistanceCentimeters = 0.0f;

	/** TurnRateDegreesPerSecond (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectiles")
	float TurnRateDegreesPerSecond = 0.0f;

	/** SweepRadiusCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectiles")
	float SweepRadiusCentimeters = 0.0f;

	/** MaximumLifetimeSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectiles")
	float MaximumLifetimeSeconds = 0.0f;

	/** UnitTypeId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectiles")
	int32 UnitTypeId = 0;

	/** SlotId (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectiles")
	FString SlotId;

	/** SkillId (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Projectiles")
	FString SkillId;

};
