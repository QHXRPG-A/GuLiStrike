// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeGameTexts.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeGameTextsTableRows.generated.h"

/** DataTable DT_GuLiStrikeGameTexts_Texts 的行结构（源: GuLiStrikeGameTexts.xlsx / Texts）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeGameTextsTextsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 文本id (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Texts")
	FString TextId;

	/** 介绍 (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Texts")
	FString Introduction;

	/** 内容 (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Texts")
	FString Content;

};
