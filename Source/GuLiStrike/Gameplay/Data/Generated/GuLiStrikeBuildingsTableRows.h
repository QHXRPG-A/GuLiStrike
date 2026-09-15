// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeBuildings.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeBuildingsTableRows.generated.h"

/** DataTable DT_GuLiStrikeBuildings_Buildings 的行结构（源: GuLiStrikeBuildings.xlsx / Buildings）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeBuildingsBuildingsRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	FString Note;

	/** Category (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	int32 Category = 0;

	/** PlacementType (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	int32 PlacementType = 0;

	/** DisplayName (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	FString DisplayName;

	/** Description (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	FString Description;

	/** Mesh (softobject, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	TSoftObjectPtr<UObject> Mesh;

	/** CollisionExtentX/CollisionExtentY/CollisionExtentZ (float) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	FVector CollisionExtent = FVector::ZeroVector;

	/** VisualOffsetX/VisualOffsetY/VisualOffsetZ (float) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	FVector VisualOffset = FVector::ZeroVector;

	/** MeshScaleX/MeshScaleY/MeshScaleZ (float) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	FVector MeshScale = FVector::ZeroVector;

	/** MaxHealth (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	float MaxHealth = 0.0f;

	/** MaxShield (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	float MaxShield = 0.0f;

	/** BuildLevel (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	int32 BuildLevel = 0;

	/** BlueCost (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	int32 BlueCost = 0;

	/** RedCost (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	int32 RedCost = 0;

	/** ConstructionWork (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	float ConstructionWork = 0.0f;

	/** ProductionUnitId (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	int32 ProductionUnitId = 0;

	/** ProductionSeconds (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	float ProductionSeconds = 0.0f;

	/** ProductionCount (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	int32 ProductionCount = 0;

	/** FirstCaptureGiftIds (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	FString FirstCaptureGiftIds;

	/** TransitFieldId (int, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	int32 TransitFieldId = 0;

	/** ShieldRadius (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	float ShieldRadius = 0.0f;

	/** ShieldRechargePerSecond (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Buildings")
	float ShieldRechargePerSecond = 0.0f;

};
