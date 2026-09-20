// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeSpecialTasks.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeSpecialTasksTableRows.generated.h"

/** DataTable DT_GuLiStrikeSpecialTasks_Tasks 的行结构（源: GuLiStrikeSpecialTasks.xlsx / Tasks）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeSpecialTasksTasksRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks")
	FString Note;

	/** DisplayName (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks")
	FString DisplayName;

	/** TaskTag (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks")
	FString TaskTag;

	/** ApplicableUnitIds (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks")
	FString ApplicableUnitIds;

	/** AutoActivate (bool, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks")
	bool AutoActivate = false;

	/** LifetimePolicy (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks")
	FString LifetimePolicy;

	/** ExecutorClass (softclass, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Tasks")
	TSoftClassPtr<UObject> ExecutorClass;

};
