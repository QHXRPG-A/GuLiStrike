#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/Interface.h"
#include "GuLiConstructionShape.generated.h"

class UMeshComponent;
class UStaticMesh;

/** Ground-relative, unrotated building coordinates, in displayed centimeters. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiConstructionContour
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVector2D> Points;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bHole = false;
};

/** A stable interval of one closed contour. Distances are world centimeters. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiConstructionSpan
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Contour = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) float Start = 0;
	UPROPERTY(BlueprintReadOnly) float Length = 0;
	UPROPERTY() uint32 Revision = 0;
};

UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiConstructionShape : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FGuLiConstructionContour> Contours;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FBox Bounds = FBox(ForceInit);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString SourceVersion;
	/** Walls have ground-relative XY and unit height (100 cm); the floor retains holes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMesh> WallMesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMesh> BaseMesh;
	UPROPERTY(VisibleAnywhere) TArray<FSoftObjectPath> SourceAssets;
	bool IsUsable() const;
};

UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiConstructionShapeCatalog : public UDataAsset
{
	GENERATED_BODY()
public:
	/** Generated from the building catalog; this is not an opt-in list. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TMap<int32, TObjectPtr<UGuLiConstructionShape>> Shapes;
};

/** Optional customization. Ordinary catalog buildings need no implementation. */
UINTERFACE(BlueprintType, Blueprintable)
class GULISTRIKE_API UGuLiConstructionShapeProvider : public UInterface { GENERATED_BODY() };

class GULISTRIKE_API IGuLiConstructionShapeProvider
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Building|Construction")
	UGuLiConstructionShape* GetConstructionShapeOverride() const;
	/** Used by editor baking; empty uses the normal visible model source discovery. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Building|Construction")
	TArray<UMeshComponent*> GetConstructionFootprintSources() const;
};
