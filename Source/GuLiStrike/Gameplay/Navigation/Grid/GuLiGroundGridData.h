#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GuLiGroundGridData.generated.h"

/** Terrain-only connected components inside 32-cell tiles, indexed by body size. */
USTRUCT()
struct GULISTRIKE_API FGuLiGridHierarchyData
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) int32 Footprint=1;
    UPROPERTY(EditAnywhere) TArray<int32> Regions;
    UPROPERTY(EditAnywhere) TArray<int32> EdgeOffsets;
    UPROPERTY(EditAnywhere) TArray<int32> Neighbours;
};

/** Cooked ground data. Runtime queries never resample Landscape or rebuild this asset. */
UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiGroundGridData final : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ground Grid") int32 SchemaVersion = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ground Grid") FIntPoint Cells = FIntPoint(567,567);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ground Grid") FVector2D Origin = FVector2D(-85050,-85050);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ground Grid") double CellSizeCentimeters = 300;
    /** Explicit standalone design rectangle; legacy assets keep their original bounds policy. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ground Grid") bool bHasAuthoredDesignBounds = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ground Grid") FString SourceHash;
    UPROPERTY(EditAnywhere, Category="Ground Grid") TArray<float> VertexHeights;
    UPROPERTY(EditAnywhere, Category="Ground Grid") TArray<uint8> Walkable;
    UPROPERTY(EditAnywhere, Category="Ground Grid") TArray<int32> Clearance;
    UPROPERTY(EditAnywhere, Category="Ground Grid") float MaximumSlopeDegrees = 44;
    UPROPERTY(EditAnywhere, Category="Ground Grid") TArray<FGuLiGridHierarchyData> Hierarchies;

    bool Validate(FString& Error) const;
};
