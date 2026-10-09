// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeModels.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeModelsTableRows.generated.h"

/** DataTable DT_GuLiStrikeModels_Models 的行结构（源: GuLiStrikeModels.xlsx / Models）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeModelsModelsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Models")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Models")
	FString Note;

	/** DisplayName (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Models")
	FString DisplayName;

	/** ResourceType (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Models")
	FString ResourceType;

	/** ResourcePath (softobject, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Models")
	TSoftObjectPtr<UObject> ResourcePath;

	/** VATDefinition (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Models")
	TSoftObjectPtr<UObject> VATDefinition;

	/** bTeamColorEnabled (bool, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Models")
	bool bTeamColorEnabled = false;

	/** BluePrimaryHex (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Models")
	FString BluePrimaryHex;

	/** BlueSecondaryHex (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Models")
	FString BlueSecondaryHex;

	/** EnemyPrimaryHex (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Models")
	FString EnemyPrimaryHex;

	/** EnemySecondaryHex (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Models")
	FString EnemySecondaryHex;

	/** CandidateResourcePath (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Models")
	TSoftObjectPtr<UObject> CandidateResourcePath;

	/** CandidateVATDefinition (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Models")
	TSoftObjectPtr<UObject> CandidateVATDefinition;

	/** Description (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Models", meta=(DisplayName="描述", ToolTip="模型的用途及所属玩法或装配"))
	FString Description;

};

/** DataTable DT_GuLiStrikeModels_Parts 的行结构（源: GuLiStrikeModels.xlsx / Parts）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeModelsPartsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parts")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parts")
	FString Note;

	/** ModelId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parts")
	int32 ModelId = 0;

	/** PartKey (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parts")
	FString PartKey;

	/** ComponentPath (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parts")
	FString ComponentPath;

	/** ChildModelId (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parts")
	int32 ChildModelId = 0;

};

/** DataTable DT_GuLiStrikeModels_MaterialParameters 的行结构（源: GuLiStrikeModels.xlsx / MaterialParameters）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeModelsMaterialParametersRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	FString Note;

	/** ModelId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	int32 ModelId = 0;

	/** PartKey (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	FString PartKey;

	/** MaterialSlotName (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	FString MaterialSlotName;

	/** ParameterKey (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	FString ParameterKey;

	/** ParameterName (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	FString ParameterName;

	/** ParameterType (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	FString ParameterType;

	/** Driver (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	FString Driver;

	/** CustomDataIndex (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	int32 CustomDataIndex = 0;

	/** Scope (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	FString Scope;

	/** bRuntimeWritable (bool, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	bool bRuntimeWritable = false;

	/** bTeamManaged (bool, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	bool bTeamManaged = false;

	/** DefaultScalar (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	float DefaultScalar = 0.0f;

	/** DefaultR (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	float DefaultR = 0.0f;

	/** DefaultG (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	float DefaultG = 0.0f;

	/** DefaultB (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	float DefaultB = 0.0f;

	/** DefaultA (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MaterialParameters")
	float DefaultA = 0.0f;

};

/** DataTable DT_GuLiStrikeModels_ColorRegions 的行结构（源: GuLiStrikeModels.xlsx / ColorRegions）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeModelsColorRegionsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ColorRegions")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ColorRegions")
	FString Note;

	/** ModelId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ColorRegions")
	int32 ModelId = 0;

	/** PartKey (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ColorRegions")
	FString PartKey;

	/** RegionKey (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ColorRegions")
	FString RegionKey;

	/** DisplayName (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ColorRegions")
	FString DisplayName;

	/** PaintRole (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ColorRegions")
	int32 PaintRole = 0;

	/** MaskSource (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ColorRegions")
	FString MaskSource;

	/** MaskId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ColorRegions")
	int32 MaskId = 0;

	/** Scope (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="ColorRegions")
	FString Scope;

};
