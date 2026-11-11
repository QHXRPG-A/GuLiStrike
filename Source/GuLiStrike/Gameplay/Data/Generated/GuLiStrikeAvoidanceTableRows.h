// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeAvoidance.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeAvoidanceTableRows.generated.h"

/** DataTable DT_GuLiStrikeAvoidance_Units 的行结构（源: GuLiStrikeAvoidance.xlsx / Units）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeAvoidanceUnitsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	FString Note;

	/** UnitTypeId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	int32 UnitTypeId = 0;

	/** RadiusMeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	float RadiusMeters = 0.0f;

	/** ClearanceMeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	float ClearanceMeters = 0.0f;

	/** ContactMarginCm (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	float ContactMarginCm = 0.0f;

	/** LookAheadSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	float LookAheadSeconds = 0.0f;

	/** TurnAngle1Degrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	float TurnAngle1Degrees = 0.0f;

	/** TurnAngle2Degrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	float TurnAngle2Degrees = 0.0f;

	/** TurnAngle3Degrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	float TurnAngle3Degrees = 0.0f;

	/** TurnAngle4Degrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	float TurnAngle4Degrees = 0.0f;

	/** TurnAngle5Degrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	float TurnAngle5Degrees = 0.0f;

	/** TurnAngle6Degrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	float TurnAngle6Degrees = 0.0f;

	/** TurnAngle7Degrees (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	float TurnAngle7Degrees = 0.0f;

	/** ClearancePreference (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	float ClearancePreference = 0.0f;

	/** SidePreferenceRadians (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	float SidePreferenceRadians = 0.0f;

	/** GridReferenceDiameterMeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	float GridReferenceDiameterMeters = 0.0f;

	/** YieldSeconds (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Units")
	float YieldSeconds = 0.0f;

};

/** DataTable DT_GuLiStrikeAvoidance_BuildingCircles 的行结构（源: GuLiStrikeAvoidance.xlsx / BuildingCircles）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeAvoidanceBuildingCirclesRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BuildingCircles")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BuildingCircles")
	FString Note;

	/** BuildingDefinitionId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BuildingCircles")
	int32 BuildingDefinitionId = 0;

	/** CircleIndex (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BuildingCircles")
	int32 CircleIndex = 0;

	/** LocalCenterMetersX/LocalCenterMetersY/LocalCenterMetersZ (float) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BuildingCircles")
	FVector LocalCenterMeters = FVector::ZeroVector;

	/** RadiusMeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BuildingCircles")
	float RadiusMeters = 0.0f;

	/** HeightMinimumMeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BuildingCircles")
	float HeightMinimumMeters = 0.0f;

	/** HeightMaximumMeters (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BuildingCircles")
	float HeightMaximumMeters = 0.0f;

	/** PhaseMask (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BuildingCircles")
	int32 PhaseMask = 0;

};

/** DataTable DT_GuLiStrikeAvoidance_OreClustering 的行结构（源: GuLiStrikeAvoidance.xlsx / OreClustering）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeAvoidanceOreClusteringRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OreClustering")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OreClustering")
	FString Note;

	/** MaximumRadiusRatio (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OreClustering")
	float MaximumRadiusRatio = 0.0f;

	/** MaximumAreaRatio (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OreClustering")
	float MaximumAreaRatio = 0.0f;

	/** PreserveWorkEntrances (bool, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="OreClustering")
	bool PreserveWorkEntrances = false;

};
