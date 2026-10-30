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

	/** BonusPercent (float, Optional) */
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

	/** RequiredCardIds (str[], Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TArray<FString> RequiredCardIds;

	/** ExcludedCardIds (str[], Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TArray<FString> ExcludedCardIds;

	/** MaxAcquisitions (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	int32 MaxAcquisitions = 0;

	/** BonusCount (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	int32 BonusCount = 0;

	/** EffectIds (str[], Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cards")
	TArray<FString> EffectIds;

};

/** DataTable DT_GuLiStrikeRogueCards_GroundRicochet 的行结构（源: GuLiStrikeRogueCards.xlsx / GroundRicochet）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeRogueCardsGroundRicochetRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundRicochet")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundRicochet")
	FString Note;

	/** MinimumHops (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundRicochet")
	int32 MinimumHops = 0;

	/** MaximumHops (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundRicochet")
	int32 MaximumHops = 0;

	/** HopsPerStack (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundRicochet")
	int32 HopsPerStack = 0;

	/** RangeCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundRicochet")
	float RangeCentimeters = 0.0f;

	/** DamageRetention (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="GroundRicochet")
	float DamageRetention = 0.0f;

};

/** DataTable DT_GuLiStrikeRogueCards_Effects 的行结构（源: GuLiStrikeRogueCards.xlsx / Effects）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeRogueCardsEffectsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	FString Note;

	/** Handler (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	FString Handler;

	/** Version (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	int32 Version = 0;

	/** ConfigId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	int32 ConfigId = 0;

	/** BuffId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effects")
	int32 BuffId = 0;

};
