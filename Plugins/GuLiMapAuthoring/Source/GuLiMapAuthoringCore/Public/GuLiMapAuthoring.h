#pragma once

#include "GuLiMapTypes.h"

class FJsonObject;
class FJsonValue;

// Mesh in region-local coordinates. Triplets index Triangles; Lines contains endpoint pairs.
struct GULIMAPAUTHORINGCORE_API FGuLiMapGeometryMesh
{
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Lines;
};

class GULIMAPAUTHORINGCORE_API IGuLiMapGeometryHandler
{
public:
    virtual ~IGuLiMapGeometryHandler() = default;
    virtual FName GetShapeType() const = 0;
    virtual UScriptStruct* GetStruct() const = 0;
    virtual bool Validate(const FInstancedStruct& Shape, FString& Error) const = 0;
    virtual TSharedRef<FJsonObject> ToJson(const FInstancedStruct& Shape) const = 0;
    virtual bool FromJson(const FJsonObject& Json, FInstancedStruct& Shape, FString& Error) const = 0;
    virtual FGuLiMapGeometryMesh BuildMesh(const FInstancedStruct& Shape) const = 0;
};

class GULIMAPAUTHORINGCORE_API IGuLiMapExportProvider
{
public:
    virtual ~IGuLiMapExportProvider() = default;
    virtual bool Generate(const FGuLiMapSnapshot& Snapshot, TMap<FString,FString>& AdditionalFiles, FString& Error) const = 0;
};

namespace GuLiMap
{
    inline constexpr int32 DensityTileSize = 32;
    GULIMAPAUTHORINGCORE_API bool RegisterGeometry(TSharedRef<IGuLiMapGeometryHandler> Handler);
    GULIMAPAUTHORINGCORE_API void UnregisterGeometry(FName ShapeType);
    GULIMAPAUTHORINGCORE_API TSharedPtr<IGuLiMapGeometryHandler> FindGeometry(FName ShapeType);
    GULIMAPAUTHORINGCORE_API TSharedPtr<IGuLiMapGeometryHandler> FindGeometry(const FInstancedStruct& Shape);
    GULIMAPAUTHORINGCORE_API TArray<FName> ListShapes();
    using FTypeValidator = TFunction<void(const FGuLiMapSnapshotEntry&, TArray<FGuLiMapIssue>&)>;
    GULIMAPAUTHORINGCORE_API bool RegisterValidator(FName Name, FTypeValidator Validator);
    GULIMAPAUTHORINGCORE_API void UnregisterValidator(FName Name);
    GULIMAPAUTHORINGCORE_API bool RegisterExporter(FName Name, TSharedRef<IGuLiMapExportProvider> Provider);
    GULIMAPAUTHORINGCORE_API void UnregisterExporter(FName Name);
    GULIMAPAUTHORINGCORE_API bool IsKey(const FString& Value);
    GULIMAPAUTHORINGCORE_API FString Guid(const FGuid& Value);
    GULIMAPAUTHORINGCORE_API FString Number(double Value);
    GULIMAPAUTHORINGCORE_API FString CanonicalJson(const TSharedPtr<FJsonValue>& Value);
    GULIMAPAUTHORINGCORE_API TSharedRef<FJsonValue> VectorJson(const FVector& Value);
    GULIMAPAUTHORINGCORE_API bool ReadVector(const TSharedPtr<FJsonValue>& Value, FVector& Out);
    GULIMAPAUTHORINGCORE_API bool CheckKeys(const FJsonObject& Object, const TArray<FString>& Allowed, FString& Error);
    GULIMAPAUTHORINGCORE_API FString FieldType(const FPropertyBagPropertyDesc& Desc);
    GULIMAPAUTHORINGCORE_API TSharedPtr<FJsonValue> FieldValue(const FInstancedPropertyBag& Bag, const FPropertyBagPropertyDesc& Desc);
    GULIMAPAUTHORINGCORE_API bool SetField(FInstancedPropertyBag& Bag, const FPropertyBagPropertyDesc& Desc, const TSharedPtr<FJsonValue>& Value, FString& Error);
    // False leaves Bag completely untouched. ExplicitReset never converts incompatible values.
    GULIMAPAUTHORINGCORE_API bool MigrateFields(FInstancedPropertyBag& Bag, const FInstancedPropertyBag& Defaults, bool ExplicitReset, FString& Error);
    GULIMAPAUTHORINGCORE_API void ValidateType(const UGuLiMapTypeDefinition& Type, TArray<FGuLiMapIssue>& Issues);
    GULIMAPAUTHORINGCORE_API bool HasErrors(const TArray<FGuLiMapIssue>& Issues);
    GULIMAPAUTHORINGCORE_API int32 WorldToDensityCell(double WorldCoordinate, double CellSizeCm);
    GULIMAPAUTHORINGCORE_API uint8 GetDensityCell(const FGuLiMapDensityLayer& Layer, FIntPoint Cell);
    GULIMAPAUTHORINGCORE_API bool SetDensityCell(FGuLiMapDensityLayer& Layer, FIntPoint Cell, uint8 Value);
    GULIMAPAUTHORINGCORE_API bool IsDensityMapEmpty(const FGuLiMapDensityMapRecord& DensityMap);
    GULIMAPAUTHORINGCORE_API void NormalizeDensityMap(FGuLiMapDensityMapRecord& DensityMap);
    GULIMAPAUTHORINGCORE_API bool EnsureDensityPresets(FGuLiMapDensityMapRecord& DensityMap);
    // Applies one deterministic stroke. Samples are resampled by fixed world distance and
    // every cell receives the maximum coverage of this stroke exactly once.
    GULIMAPAUTHORINGCORE_API bool ApplyDensityBrush(FGuLiMapDensityMapRecord& DensityMap, FName LayerKey, const TArray<FVector2D>& WorldSamples, double RadiusCm, double Strength01, double Falloff01, bool bErase, FString& Error);
    GULIMAPAUTHORINGCORE_API void ValidateDensity(const FGuLiMapSnapshot& Snapshot, TArray<FGuLiMapIssue>& Issues);
    GULIMAPAUTHORINGCORE_API bool BuildDensityFiles(const FGuLiMapSnapshot& Snapshot, TMap<FString,FString>& Files, TArray<FGuLiMapIssue>& Issues);
    GULIMAPAUTHORINGCORE_API void Validate(const FGuLiMapSnapshot& Snapshot, TArray<FGuLiMapIssue>& Issues);
    GULIMAPAUTHORINGCORE_API bool BuildFiles(const FGuLiMapSnapshot& Snapshot, TMap<FString,FString>& Files, TArray<FGuLiMapIssue>& Issues);
    GULIMAPAUTHORINGCORE_API bool Publish(const FString& Directory, const TMap<FString,FString>& Files, FString& Error);
    GULIMAPAUTHORINGCORE_API void RegisterBuiltInGeometry();
    GULIMAPAUTHORINGCORE_API void ShutdownRegistries();
}
