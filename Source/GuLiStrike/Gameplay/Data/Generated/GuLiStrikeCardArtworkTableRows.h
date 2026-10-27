// ====================================================================
// 自动生成自 Data/Excel/: GuLiStrikeCardArtwork.xlsx —— 禁止手改。
// 由 Tools/DataPipeline/export_data_from_excel.py 生成。
// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。
// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；
//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；
//       softobject -> TSoftObjectPtr<UObject>。
// ====================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GuLiStrikeCardArtworkTableRows.generated.h"

/** DataTable DT_GuLiStrikeCardArtwork_Templates 的行结构（源: GuLiStrikeCardArtwork.xlsx / Templates）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeCardArtworkTemplatesRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Templates")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Templates")
	FString Note;

	/** ParentMaterial (softobject, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Templates")
	TSoftObjectPtr<UObject> ParentMaterial;

	/** Layout (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Templates")
	FString Layout;

	/** CompositePreset (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Templates")
	FString CompositePreset;

};

/** DataTable DT_GuLiStrikeCardArtwork_Artworks 的行结构（源: GuLiStrikeCardArtwork.xlsx / Artworks）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeCardArtworkArtworksRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Artworks")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Artworks")
	FString Note;

	/** Title (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Artworks")
	FString Title;

	/** TemplateId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Artworks")
	int32 TemplateId = 0;

	/** Material (softobject, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Artworks")
	TSoftObjectPtr<UObject> Material;

	/** AssetRoot (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Artworks")
	FString AssetRoot;

	/** SourceRoot (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Artworks")
	FString SourceRoot;

	/** ReferenceFile (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Artworks")
	FString ReferenceFile;

	/** PreviewUrl (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Artworks")
	FString PreviewUrl;

};

/** DataTable DT_GuLiStrikeCardArtwork_Layers 的行结构（源: GuLiStrikeCardArtwork.xlsx / Layers）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeCardArtworkLayersRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
	FString Note;

	/** ArtworkId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
	int32 ArtworkId = 0;

	/** LayerIndex (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
	int32 LayerIndex = 0;

	/** Label (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
	FString Label;

	/** TextureParameter (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
	FString TextureParameter;

	/** Depth (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
	float Depth = 0.0f;

	/** Opacity (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
	float Opacity = 0.0f;

	/** ScaleU (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
	float ScaleU = 0.0f;

	/** ScaleV (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
	float ScaleV = 0.0f;

	/** ScaleB (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
	float ScaleB = 0.0f;

	/** ScaleA (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
	float ScaleA = 0.0f;

	/** OffsetU (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
	float OffsetU = 0.0f;

	/** OffsetV (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
	float OffsetV = 0.0f;

	/** OffsetB (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
	float OffsetB = 0.0f;

	/** OffsetA (float, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Layers")
	float OffsetA = 0.0f;

};

/** DataTable DT_GuLiStrikeCardArtwork_Parameters 的行结构（源: GuLiStrikeCardArtwork.xlsx / Parameters）。 */
USTRUCT(BlueprintType)
struct FGuLiStrikeCardArtworkParametersRow : public FTableRowBase
{
	GENERATED_BODY()

	/** id (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parameters")
	int32 Id = 0;

	/** Note (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parameters")
	FString Note;

	/** ArtworkId (int, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parameters")
	int32 ArtworkId = 0;

	/** Key (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parameters")
	FString Key;

	/** Kind (str, Necessary) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parameters")
	FString Kind;

	/** ScalarValue (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parameters")
	float ScalarValue = 0.0f;

	/** VectorR (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parameters")
	float VectorR = 0.0f;

	/** VectorG (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parameters")
	float VectorG = 0.0f;

	/** VectorB (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parameters")
	float VectorB = 0.0f;

	/** VectorA (float, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parameters")
	float VectorA = 0.0f;

	/** Texture (softobject, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parameters")
	TSoftObjectPtr<UObject> Texture;

	/** SourceFile (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parameters")
	FString SourceFile;

	/** TextureRole (str, Optional) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parameters")
	FString TextureRole;

};
