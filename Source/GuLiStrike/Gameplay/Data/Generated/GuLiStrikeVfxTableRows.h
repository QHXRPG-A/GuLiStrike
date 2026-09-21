// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeVfx.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeVfxTableRows.generated.h"

/** DataTable DT_GuLiStrikeVfx_Effects 的行结构（源: GuLiStrikeVfx.xlsx / Effects）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeVfxEffectsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	FString Note;

	/** ResourcePath (softobject, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	TSoftObjectPtr<UObject> ResourcePath;

	/** ScaleX/ScaleY/ScaleZ (float) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	FVector Scale = FVector::ZeroVector;

};
