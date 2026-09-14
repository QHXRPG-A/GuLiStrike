#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"
#include "GuLiMapTypes.generated.h"

USTRUCT(BlueprintType)
struct GULIMAPAUTHORINGCORE_API FGuLiMapShape
{
    GENERATED_BODY()
};

USTRUCT(BlueprintType, meta=(DisplayName="圆柱 Cylinder"))
struct GULIMAPAUTHORINGCORE_API FGuLiMapCylinder : public FGuLiMapShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Shape", meta=(Units="cm")) double Radius = 10000.0;
    UPROPERTY(EditAnywhere, Category="Shape", meta=(Units="cm")) double MinZ = -5000.0;
    UPROPERTY(EditAnywhere, Category="Shape", meta=(Units="cm")) double MaxZ = 5000.0;
};

USTRUCT(BlueprintType, meta=(DisplayName="球 Sphere"))
struct GULIMAPAUTHORINGCORE_API FGuLiMapSphere : public FGuLiMapShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Shape", meta=(Units="cm")) double Radius = 10000.0;
};

USTRUCT(BlueprintType, meta=(DisplayName="旋转方框 Box"))
struct GULIMAPAUTHORINGCORE_API FGuLiMapBox : public FGuLiMapShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Shape", meta=(Units="cm", DisplayName="半尺寸（完整尺寸 = 2 × 半尺寸）")) FVector HalfExtents = FVector(10000, 10000, 5000);
};

USTRUCT(BlueprintType, meta=(DisplayName="多边形柱体 Polygon Prism"))
struct GULIMAPAUTHORINGCORE_API FGuLiMapPolygonPrism : public FGuLiMapShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Shape", meta=(DisplayName="Vertices (cm)")) TArray<FVector2D> Vertices = { FVector2D(-10000,-10000), FVector2D(10000,-10000), FVector2D(10000,10000), FVector2D(-10000,10000) };
    UPROPERTY(EditAnywhere, Category="Shape", meta=(Units="cm")) double MinZ = -5000.0;
    UPROPERTY(EditAnywhere, Category="Shape", meta=(Units="cm")) double MaxZ = 5000.0;
};

USTRUCT(BlueprintType)
struct GULIMAPAUTHORINGCORE_API FGuLiMapRegionRecord
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, Category="Identity") FGuid RegionId;
    UPROPERTY(EditAnywhere, Category="Identity") FName RegionKey = "Area";
    UPROPERTY(EditAnywhere, Category="Identity") FString DisplayName;
    UPROPERTY(EditAnywhere, Category="Identity") bool bEnabled = true;
    UPROPERTY(EditAnywhere, Category="Transform", meta=(Units="cm")) FVector Translation = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, Category="Transform", meta=(Units="deg")) FRotator Rotation = FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere, Category="Shape", meta=(BaseStruct="/Script/GuLiMapAuthoringCore.GuLiMapShape", ExcludeBaseStruct)) FInstancedStruct Geometry;

    FTransform GetTransform() const { return FTransform(Rotation, Translation, FVector::OneVector); }
};

USTRUCT(BlueprintType)
struct GULIMAPAUTHORINGCORE_API FGuLiMapFieldRule
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Field") FGuid FieldId;
    UPROPERTY(EditAnywhere, Category="Field") FString DisplayName;
    UPROPERTY(EditAnywhere, Category="Field") bool bRequired = false;
    UPROPERTY(EditAnywhere, Category="Field") bool bUseMinimum = false;
    UPROPERTY(EditAnywhere, Category="Field", meta=(EditCondition="bUseMinimum")) double Minimum = 0;
    UPROPERTY(EditAnywhere, Category="Field") bool bUseMaximum = false;
    UPROPERTY(EditAnywhere, Category="Field", meta=(EditCondition="bUseMaximum")) double Maximum = 0;
};

UCLASS(BlueprintType)
class GULIMAPAUTHORINGCORE_API UGuLiMapTypeDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Type") FName TypeId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Type") FString DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Type") FLinearColor Color = FLinearColor(0.1f,0.7f,1.f);
    UPROPERTY(EditAnywhere, Category="Type") FName EditorIcon = "Icons.Location";
    UPROPERTY(EditAnywhere, Category="Type") TArray<FName> AllowedShapes = { "Cylinder", "Sphere", "Box", "PolygonPrism" };
    UPROPERTY(EditAnywhere, Category="Type") TArray<FGuLiMapRegionRecord> DefaultRegions;
    UPROPERTY(EditAnywhere, Category="Fields", meta=(AllowContainers="false")) FInstancedPropertyBag DefaultParameters;
    UPROPERTY(EditAnywhere, Category="Fields") TArray<FGuLiMapFieldRule> FieldRules;
    virtual bool IsEditorOnly() const override { return true; }
};

USTRUCT(BlueprintType)
struct GULIMAPAUTHORINGCORE_API FGuLiMapMarkerRecord
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, Category="Identity") FGuid MarkerId;
    UPROPERTY(EditAnywhere, Category="Identity") FName MarkerKey;
    UPROPERTY(EditAnywhere, Category="Identity") FString DisplayName;
    UPROPERTY(EditAnywhere, Category="Identity") TSoftObjectPtr<UGuLiMapTypeDefinition> Type;
    UPROPERTY(EditAnywhere, Category="Identity") bool bEnabled = true;
    UPROPERTY(EditAnywhere, Category="Identity") TArray<FName> Tags;
    UPROPERTY(EditAnywhere, Category="Identity", meta=(MultiLine=true)) FString Note;
    UPROPERTY(EditAnywhere, Category="Fields", meta=(FixedLayout, AllowContainers="false")) FInstancedPropertyBag Parameters;
    UPROPERTY(EditAnywhere, Category="Regions", meta=(TitleProperty="RegionKey")) TArray<FGuLiMapRegionRecord> Regions;
};

USTRUCT(BlueprintType)
struct GULIMAPAUTHORINGCORE_API FGuLiMapDensityTile
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, Category="Density") FIntPoint TileCoord = FIntPoint::ZeroValue;
    // Row-major 32 x 32 uint8 values. Empty/zero-only tiles are never persisted.
    UPROPERTY(VisibleAnywhere, Category="Density") TArray<uint8> Values;
};

USTRUCT(BlueprintType)
struct GULIMAPAUTHORINGCORE_API FGuLiMapDensityLayer
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, Category="Identity") FGuid LayerId;
    UPROPERTY(EditAnywhere, Category="Identity") FName LayerKey;
    UPROPERTY(EditAnywhere, Category="Identity") FString DisplayName;
    UPROPERTY(EditAnywhere, Category="Preview") FLinearColor Color = FLinearColor::White;
    UPROPERTY(VisibleAnywhere, Category="Density") TArray<FGuLiMapDensityTile> Tiles;
};

USTRUCT(BlueprintType)
struct GULIMAPAUTHORINGCORE_API FGuLiMapDensityMapRecord
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, Category="Identity") FGuid DensityMapId;
    UPROPERTY(VisibleAnywhere, Category="Identity") int32 DataVersion = 1;
    UPROPERTY(EditAnywhere, Category="Grid", meta=(Units="cm", ClampMin="1")) double CellSizeCm = 2500.0;
    UPROPERTY(EditAnywhere, Category="Territory") FName TerritoryTypeId = TEXT("Outpost");
    UPROPERTY(EditAnywhere, Category="Territory") FName TerritoryRegionKey = TEXT("Territory");
    UPROPERTY(EditAnywhere, Category="Layers", meta=(TitleProperty="LayerKey")) TArray<FGuLiMapDensityLayer> Layers;
};

UENUM(BlueprintType)
enum class EGuLiMapIssueSeverity : uint8
{
    Error,
    Warning
};

USTRUCT(BlueprintType)
struct GULIMAPAUTHORINGCORE_API FGuLiMapIssue
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Issue") EGuLiMapIssueSeverity Severity = EGuLiMapIssueSeverity::Error;
    UPROPERTY(BlueprintReadOnly, Category="Issue") FGuid MarkerId;
    UPROPERTY(BlueprintReadOnly, Category="Issue") FGuid RegionId;
    UPROPERTY(BlueprintReadOnly, Category="Issue") FString Field;
    UPROPERTY(BlueprintReadOnly, Category="Issue") FString Message;
    FGuLiMapIssue() = default;
    FGuLiMapIssue(const FString& InMessage, FGuid InMarker = {}, FGuid InRegion = {}, const FString& InField = {}, EGuLiMapIssueSeverity InSeverity = EGuLiMapIssueSeverity::Error)
        : Severity(InSeverity), MarkerId(InMarker), RegionId(InRegion), Field(InField), Message(InMessage) {}
};

USTRUCT(BlueprintType)
struct GULIMAPAUTHORINGCORE_API FGuLiMapResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Result") bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category="Result") TArray<FGuLiMapIssue> Issues;
    UPROPERTY(BlueprintReadOnly, Category="Result") FString Json;
    UPROPERTY(BlueprintReadOnly, Category="Result") TArray<FString> Files;
};

// Value copies are taken on the game thread. No Actor pointers cross the snapshot boundary.
struct GULIMAPAUTHORINGCORE_API FGuLiMapSnapshotEntry
{
    FGuLiMapMarkerRecord Record;
    FTransform WorldTransform = FTransform::Identity;
};

struct GULIMAPAUTHORINGCORE_API FGuLiMapSnapshot
{
    FString MapPackage;
    TArray<FGuLiMapSnapshotEntry> Markers;
    TOptional<FGuLiMapDensityMapRecord> DensityMap;
};
