// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeRogueCards.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeRogueCardsTableRows.generated.h"

/** DataTable DT_GuLiStrikeRogueCards_Cards 的行结构（源: GuLiStrikeRogueCards.xlsx / Cards）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeRogueCardsCardsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	FString Id;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	FString Note;

	/** Type (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	int32 Type = 0;

	/** TextIds (str[], Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TArray<FString> TextIds;

	/** ImplementationClass (softclass, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TSoftClassPtr<UObject> ImplementationClass;

	/** UnitTypeId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	int32 UnitTypeId = 0;

	/** BonusPercent (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float BonusPercent = 0.0f;

	/** FrontMaterial (softobject, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TSoftObjectPtr<UObject> FrontMaterial;

	/** UpgradeVfx (softobject, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TSoftObjectPtr<UObject> UpgradeVfx;

	/** UpgradeVfxScale (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	float UpgradeVfxScale = 0.0f;

	/** UpgradeVfxColor (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	FString UpgradeVfxColor;

};
