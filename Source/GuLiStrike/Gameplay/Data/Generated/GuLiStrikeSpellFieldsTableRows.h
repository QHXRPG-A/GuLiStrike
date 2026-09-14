// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeSpellFields.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeSpellFieldsTableRows.generated.h"

/** DataTable DT_GuLiStrikeSpellFields_Fields 的行结构（源: GuLiStrikeSpellFields.xlsx / Fields）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeSpellFieldsFieldsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	FString Note;

	/** FieldType (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	FString FieldType;

	/** Damage (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	float Damage = 0.0f;

	/** RadiusCentimeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	float RadiusCentimeters = 0.0f;

	/** Timing (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	FString Timing;

	/** DelaySeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	float DelaySeconds = 0.0f;

	/** DurationSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	float DurationSeconds = 0.0f;

	/** PulseIntervalSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	float PulseIntervalSeconds = 0.0f;

	/** DissipationSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	float DissipationSeconds = 0.0f;

	/** Level (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	int32 Level = 0;

	/** bAllowPlayerVehicles (bool, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	bool bAllowPlayerVehicles = false;

	/** WindupSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	float WindupSeconds = 0.0f;

	/** RecoverySeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	float RecoverySeconds = 0.0f;

	/** BeamHeightCentimeters (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	float BeamHeightCentimeters = 0.0f;

	/** MaxTargetWaitSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	float MaxTargetWaitSeconds = 0.0f;

	/** MaxShipHeightCentimeters (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fields")
	float MaxShipHeightCentimeters = 0.0f;

};
