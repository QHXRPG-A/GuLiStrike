// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeRogueCardUI.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeRogueCardUITableRows.generated.h"

/** DataTable DT_GuLiStrikeRogueCardUI_TextStyles 的行结构（源: GuLiStrikeRogueCardUI.xlsx / TextStyles）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeRogueCardUITextStylesRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="TextStyles")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="TextStyles")
	FString Note;

	/** FontAsset (softobject, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="TextStyles")
	TSoftObjectPtr<UObject> FontAsset;

	/** FontSize (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="TextStyles")
	int32 FontSize = 0;

	/** Typeface (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="TextStyles")
	FString Typeface;

	/** ColorSRGB (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="TextStyles")
	FString ColorSRGB;

	/** OutlineSize (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="TextStyles")
	int32 OutlineSize = 0;

	/** OutlineColorSRGB (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="TextStyles")
	FString OutlineColorSRGB;

};
