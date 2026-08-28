// ====================================================================
// 自动生成自 Data/Excel/GuLiStrikeCommander.xlsx —— 禁止手改。
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

/** DataTable DT_GuLiStrikeCommander_Soldiers 的行结构（源: GuLiStrikeCommander.xlsx 的 Soldiers sheet）。 */
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

	/** MaxHealth (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	int32 MaxHealth = 0;

	/** ModelAsset (softobject, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	TSoftObjectPtr<UObject> ModelAsset;

	/** AttackPower (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	float AttackPower = 0.0f;

	/** Defense (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	float Defense = 0.0f;

	/** AttackRangeCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Soldiers")
	float AttackRangeCentimeters = 0.0f;

};
