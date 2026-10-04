// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeLOD.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeLODTableRows.generated.h"

/** DataTable DT_GuLiStrikeLOD_LOD 的行结构（源: GuLiStrikeLOD.xlsx / LOD）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeLODLODRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LOD")
	int32 Id = 0;

	/** Category (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LOD")
	FString Category;

	/** LODLevel (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LOD")
	int32 LODLevel = 0;

	/** DisplayName (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LOD")
	FString DisplayName;

	/** EnterDistanceCm (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LOD")
	float EnterDistanceCm = 0.0f;

	/** HoldDistanceCm (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LOD")
	float HoldDistanceCm = 0.0f;

	/** EnterScreenFraction (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LOD")
	float EnterScreenFraction = 0.0f;

	/** HoldScreenFraction (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LOD")
	float HoldScreenFraction = 0.0f;

	/** MinimumResidenceSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LOD")
	float MinimumResidenceSeconds = 0.0f;

	/** IsConfigured (bool, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LOD")
	bool IsConfigured = false;

	/** IsFallback (bool, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LOD")
	bool IsFallback = false;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LOD")
	FString Note;

};
