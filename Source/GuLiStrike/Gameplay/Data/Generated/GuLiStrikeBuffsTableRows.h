// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeBuffs.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeBuffsTableRows.generated.h"

/** DataTable DT_GuLiStrikeBuffs_Buffs 的行结构（源: GuLiStrikeBuffs.xlsx / Buffs）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeBuffsBuffsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buffs")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buffs")
	FString Note;

	/** DescriptionTextId (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buffs")
	FString DescriptionTextId;

	/** ImplementationClass (softclass, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buffs")
	TSoftClassPtr<UObject> ImplementationClass;

	/** Version (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buffs")
	int32 Version = 0;

	/** ConfigId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buffs")
	int32 ConfigId = 0;

};

/** DataTable DT_GuLiStrikeBuffs_Burning 的行结构（源: GuLiStrikeBuffs.xlsx / Burning）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeBuffsBurningRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Burning")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Burning")
	FString Note;

	/** DurationSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Burning")
	float DurationSeconds = 0.0f;

	/** TickIntervalSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Burning")
	float TickIntervalSeconds = 0.0f;

	/** MaxHealthDamageRatio (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Burning")
	float MaxHealthDamageRatio = 0.0f;

	/** StatusVfxId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Burning")
	int32 StatusVfxId = 0;

	/** BulletVfxId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Burning")
	int32 BulletVfxId = 0;

};

/** DataTable DT_GuLiStrikeBuffs_BurningBurst 的行结构（源: GuLiStrikeBuffs.xlsx / BurningBurst）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeBuffsBurningBurstRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BurningBurst")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BurningBurst")
	FString Note;

	/** ParentBuffId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BurningBurst")
	int32 ParentBuffId = 0;

	/** IntervalSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BurningBurst")
	float IntervalSeconds = 0.0f;

	/** LostHealthDamageRatio (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BurningBurst")
	float LostHealthDamageRatio = 0.0f;

	/** RadiusMultiplier (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BurningBurst")
	float RadiusMultiplier = 0.0f;

	/** BurstVfxId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BurningBurst")
	int32 BurstVfxId = 0;

};
