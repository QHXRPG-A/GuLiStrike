// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeGroundHUD.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeGroundHUDTableRows.generated.h"

/** DataTable DT_GuLiStrikeGroundHUD_Elements 的行结构（源: GuLiStrikeGroundHUD.xlsx / Elements）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeGroundHUDElementsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elements")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elements")
	FString Note;

	/** Texture (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elements")
	TSoftObjectPtr<UObject> Texture;

	/** TextId (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elements")
	FString TextId;

	/** Sprite (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elements")
	int32 Sprite = 0;

	/** Value (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elements")
	float Value = 0.0f;

	/** ColorSRGB (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elements")
	FString ColorSRGB;

	/** IconId (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elements")
	int32 IconId = 0;

};
