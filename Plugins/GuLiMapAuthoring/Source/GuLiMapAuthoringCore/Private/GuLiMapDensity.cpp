#include "GuLiMapAuthoring.h"

#include "Dom/JsonObject.h"

namespace
{
constexpr int32 TileValueCount = GuLiMap::DensityTileSize * GuLiMap::DensityTileSize;

int32 FloorDiv(const int32 Value, const int32 Divisor)
{
    const int64 Wide = Value;
    return static_cast<int32>(Wide >= 0 ? Wide / Divisor : -((-Wide + Divisor - 1) / Divisor));
}

FIntPoint TileForCell(const FIntPoint Cell)
{
    return FIntPoint(FloorDiv(Cell.X, GuLiMap::DensityTileSize), FloorDiv(Cell.Y, GuLiMap::DensityTileSize));
}

int32 IndexInTile(const FIntPoint Cell, const FIntPoint Tile)
{
    const int32 X = Cell.X - Tile.X * GuLiMap::DensityTileSize;
    const int32 Y = Cell.Y - Tile.Y * GuLiMap::DensityTileSize;
    return Y * GuLiMap::DensityTileSize + X;
}

bool HasNonZero(const FGuLiMapDensityTile& Tile)
{
    return Tile.Values.ContainsByPredicate([](const uint8 Value) { return Value != 0; });
}

void CsvRow(FString& Out, const TArray<FString>& Cells)
{
    for (int32 Index = 0; Index < Cells.Num(); ++Index)
    {
        if (Index != 0) Out += TEXT(",");
        FString Cell = Cells[Index];
        Cell.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
        Cell.ReplaceInline(TEXT("\r"), TEXT("\n"));
        if (Cell.Contains(TEXT(",")) || Cell.Contains(TEXT("\"")) || Cell.Contains(TEXT("\n")))
        {
            Cell.ReplaceInline(TEXT("\""), TEXT("\"\""));
            Cell = TEXT("\"") + Cell + TEXT("\"");
        }
        Out += Cell;
    }
    Out += TEXT("\n");
}

TSharedRef<FJsonValue> JsonObjectValue(const TSharedRef<FJsonObject>& Object)
{
    return MakeShared<FJsonValueObject>(Object);
}

TSharedRef<FJsonValue> JsonStringValue(const FString& String)
{
    return MakeShared<FJsonValueString>(String);
}

struct FDensityCell
{
    FIntPoint Coord = FIntPoint::ZeroValue;
    uint8 Density = 0;
};

bool EnumerateLayerCells(const FGuLiMapDensityLayer& Layer, TArray<FDensityCell>& Out, FString& Error)
{
    Out.Reset();
    for (const FGuLiMapDensityTile& Tile : Layer.Tiles)
    {
        if (Tile.Values.Num() != TileValueCount)
        {
            Error = FString::Printf(TEXT("Density tile %d,%d in layer %s must contain exactly %d values."), Tile.TileCoord.X, Tile.TileCoord.Y, *Layer.LayerKey.ToString(), TileValueCount);
            return false;
        }
        for (int32 Index = 0; Index < TileValueCount; ++Index)
        {
            const uint8 Density = Tile.Values[Index];
            if (Density == 0) continue;
            const int64 CellX = static_cast<int64>(Tile.TileCoord.X) * GuLiMap::DensityTileSize + Index % GuLiMap::DensityTileSize;
            const int64 CellY = static_cast<int64>(Tile.TileCoord.Y) * GuLiMap::DensityTileSize + Index / GuLiMap::DensityTileSize;
            if (CellX < MIN_int32 || CellX > MAX_int32 || CellY < MIN_int32 || CellY > MAX_int32)
            {
                Error = TEXT("Density tile expands outside the supported int32 cell-coordinate range.");
                return false;
            }
            Out.Add({FIntPoint(static_cast<int32>(CellX), static_cast<int32>(CellY)), Density});
        }
    }
    Out.Sort([](const FDensityCell& A, const FDensityCell& B)
    {
        return A.Coord.Y == B.Coord.Y ? A.Coord.X < B.Coord.X : A.Coord.Y < B.Coord.Y;
    });
    return true;
}

enum class EPointPolygon : uint8 { Outside, Boundary, Inside };

double Cross2D(const FVector2D A, const FVector2D B, const FVector2D P)
{
    return (B.X - A.X) * (P.Y - A.Y) - (B.Y - A.Y) * (P.X - A.X);
}

bool PointOnSegment(const FVector2D A, const FVector2D B, const FVector2D P)
{
    const FVector2D Edge = B - A;
    const double Tolerance = 1.e-7 * FMath::Max(1.0, Edge.Size());
    if (FMath::Abs(Cross2D(A, B, P)) > Tolerance) return false;
    return FVector2D::DotProduct(P - A, P - B) <= Tolerance;
}

EPointPolygon ClassifyPoint(const TArray<FVector2D>& Polygon, const FVector2D Point)
{
    bool bInside = false;
    for (int32 Index = 0, Previous = Polygon.Num() - 1; Index < Polygon.Num(); Previous = Index++)
    {
        const FVector2D A = Polygon[Previous];
        const FVector2D B = Polygon[Index];
        if (PointOnSegment(A, B, Point)) return EPointPolygon::Boundary;
        const bool bCrosses = (A.Y > Point.Y) != (B.Y > Point.Y);
        if (bCrosses && Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X) bInside = !bInside;
    }
    return bInside ? EPointPolygon::Inside : EPointPolygon::Outside;
}

struct FTerritory
{
    FGuid MarkerId;
    FString MarkerKey;
    FGuid RegionId;
    TArray<FVector2D> Polygon;
};

enum class EAssignment : uint8 { Territory, Unassigned, Overlap };

struct FAssignmentResult
{
    EAssignment Kind = EAssignment::Unassigned;
    int32 TerritoryIndex = INDEX_NONE;
    TArray<int32> Candidates;
};

struct FTerritoryStat
{
    EAssignment Kind = EAssignment::Territory;
    int32 TerritoryIndex = INDEX_NONE;
    int32 CellCount = 0;
    double WeightSum = 0.0;
};

struct FAnomaly
{
    EAssignment Kind = EAssignment::Unassigned;
    FString LayerKey;
    FIntPoint Cell = FIntPoint::ZeroValue;
    uint8 Density = 0;
    TArray<int32> Candidates;
};

struct FLayerAnalysis
{
    const FGuLiMapDensityLayer* Layer = nullptr;
    TArray<FDensityCell> Cells;
    TArray<FTerritoryStat> Stats;
};

struct FDensityAnalysis
{
    TArray<FTerritory> Territories;
    TArray<FLayerAnalysis> Layers;
    TArray<FAnomaly> Anomalies;
};

void AddWarning(TArray<FGuLiMapIssue>& Issues, const FString& Message, const FGuid MarkerId = FGuid(), const FGuid RegionId = FGuid())
{
    Issues.Emplace(Message, MarkerId, RegionId, TEXT("density"), EGuLiMapIssueSeverity::Warning);
}

bool BuildTerritories(const FGuLiMapSnapshot& Snapshot, FDensityAnalysis& Analysis, TArray<FGuLiMapIssue>& Issues, const bool bEmitWarnings)
{
    const FGuLiMapDensityMapRecord& DensityMap = Snapshot.DensityMap.GetValue();
    for (const FGuLiMapSnapshotEntry& Entry : Snapshot.Markers)
    {
        const UGuLiMapTypeDefinition* Type = Entry.Record.Type.LoadSynchronous();
        if (!Entry.Record.bEnabled || !Type || Type->TypeId != DensityMap.TerritoryTypeId) continue;
        const FGuLiMapRegionRecord* Region = Entry.Record.Regions.FindByPredicate([&](const FGuLiMapRegionRecord& Candidate)
        {
            return Candidate.bEnabled && Candidate.RegionKey == DensityMap.TerritoryRegionKey;
        });
        if (!Region)
        {
            if (bEmitWarnings) AddWarning(Issues, TEXT("Enabled Outpost has no enabled Territory polygon; density outside other territories remains unassigned."), Entry.Record.MarkerId);
            continue;
        }
        const FGuLiMapPolygonPrism* Polygon = Region->Geometry.GetPtr<FGuLiMapPolygonPrism>();
        if (!Polygon)
        {
            Issues.Emplace(TEXT("Territory must use PolygonPrism geometry."), Entry.Record.MarkerId, Region->RegionId, TEXT("geometry"));
            continue;
        }
        FString GeometryError;
        const TSharedPtr<IGuLiMapGeometryHandler> Handler = GuLiMap::FindGeometry(Region->Geometry);
        if (!Handler || !Handler->Validate(Region->Geometry, GeometryError))
        {
            Issues.Emplace(GeometryError.IsEmpty() ? TEXT("Territory polygon is invalid.") : GeometryError, Entry.Record.MarkerId, Region->RegionId, TEXT("geometry"));
            continue;
        }
        const FTransform World = Region->GetTransform() * Entry.WorldTransform;
        const FVector Up = World.GetUnitAxis(EAxis::Z);
        if (!World.IsValid() || FVector::DotProduct(Up, FVector::UpVector) < 0.999999)
        {
            Issues.Emplace(TEXT("Territory must be horizontal; pitch/roll or inverted transforms are unsupported."), Entry.Record.MarkerId, Region->RegionId, TEXT("transform"));
            continue;
        }
        FTerritory Territory;
        Territory.MarkerId = Entry.Record.MarkerId;
        Territory.MarkerKey = Entry.Record.MarkerKey.ToString();
        Territory.RegionId = Region->RegionId;
        for (const FVector2D Vertex : Polygon->Vertices)
        {
            const FVector Position = World.TransformPosition(FVector(Vertex.X, Vertex.Y, 0.0));
            Territory.Polygon.Add(FVector2D(Position.X, Position.Y));
        }
        Analysis.Territories.Add(MoveTemp(Territory));
    }
    Analysis.Territories.Sort([](const FTerritory& A, const FTerritory& B)
    {
        if (A.MarkerKey != B.MarkerKey) return A.MarkerKey < B.MarkerKey;
        return GuLiMap::Guid(A.RegionId) < GuLiMap::Guid(B.RegionId);
    });
    return !GuLiMap::HasErrors(Issues);
}

FAssignmentResult AssignPoint(const TArray<FTerritory>& Territories, const FVector2D Point)
{
    FAssignmentResult Result;
    TArray<int32> Inside;
    TArray<int32> Boundary;
    for (int32 Index = 0; Index < Territories.Num(); ++Index)
    {
        switch (ClassifyPoint(Territories[Index].Polygon, Point))
        {
        case EPointPolygon::Inside: Inside.Add(Index); break;
        case EPointPolygon::Boundary: Boundary.Add(Index); break;
        default: break;
        }
    }
    if (Inside.Num() > 1)
    {
        Result.Kind = EAssignment::Overlap;
        Result.Candidates = MoveTemp(Inside);
    }
    else if (Inside.Num() == 1)
    {
        Result.Kind = EAssignment::Territory;
        Result.TerritoryIndex = Inside[0];
    }
    else if (!Boundary.IsEmpty())
    {
        // Territories are already sorted by stable MarkerKey/RegionId. Exact seam hits
        // therefore have one deterministic owner and are not reported as overlap.
        Result.Kind = EAssignment::Territory;
        Result.TerritoryIndex = Boundary[0];
    }
    return Result;
}

bool AnalyzeDensity(const FGuLiMapSnapshot& Snapshot, FDensityAnalysis& Analysis, TArray<FGuLiMapIssue>& Issues, const bool bEmitWarnings)
{
    if (!Snapshot.DensityMap.IsSet()) return true;
    const FGuLiMapDensityMapRecord& DensityMap = Snapshot.DensityMap.GetValue();
    if (!BuildTerritories(Snapshot, Analysis, Issues, bEmitWarnings)) return false;

    TArray<const FGuLiMapDensityLayer*> SortedLayers;
    for (const FGuLiMapDensityLayer& Layer : DensityMap.Layers) SortedLayers.Add(&Layer);
    SortedLayers.Sort([](const FGuLiMapDensityLayer& A, const FGuLiMapDensityLayer& B)
    {
        if (A.LayerKey != B.LayerKey) return A.LayerKey.LexicalLess(B.LayerKey);
        return GuLiMap::Guid(A.LayerId) < GuLiMap::Guid(B.LayerId);
    });
    int64 UnassignedCount = 0;
    int64 OverlapCount = 0;
    for (const FGuLiMapDensityLayer* Layer : SortedLayers)
    {
        FLayerAnalysis LayerAnalysis;
        LayerAnalysis.Layer = Layer;
        FString Error;
        if (!EnumerateLayerCells(*Layer, LayerAnalysis.Cells, Error))
        {
            Issues.Emplace(Error, FGuid(), FGuid(), TEXT("density"));
            return false;
        }
        for (int32 TerritoryIndex = 0; TerritoryIndex < Analysis.Territories.Num(); ++TerritoryIndex)
        {
            LayerAnalysis.Stats.Add({EAssignment::Territory, TerritoryIndex});
        }
        const int32 UnassignedStat = LayerAnalysis.Stats.Add({EAssignment::Unassigned, INDEX_NONE});
        const int32 OverlapStat = LayerAnalysis.Stats.Add({EAssignment::Overlap, INDEX_NONE});
        for (const FDensityCell& Cell : LayerAnalysis.Cells)
        {
            const FVector2D Center((static_cast<double>(Cell.Coord.X) + 0.5) * DensityMap.CellSizeCm, (static_cast<double>(Cell.Coord.Y) + 0.5) * DensityMap.CellSizeCm);
            FAssignmentResult Assignment = AssignPoint(Analysis.Territories, Center);
            int32 StatIndex = Assignment.Kind == EAssignment::Territory ? Assignment.TerritoryIndex : Assignment.Kind == EAssignment::Unassigned ? UnassignedStat : OverlapStat;
            FTerritoryStat& Stat = LayerAnalysis.Stats[StatIndex];
            ++Stat.CellCount;
            Stat.WeightSum += static_cast<double>(Cell.Density) / 255.0;
            if (Assignment.Kind != EAssignment::Territory)
            {
                Analysis.Anomalies.Add({Assignment.Kind, Layer->LayerKey.ToString(), Cell.Coord, Cell.Density, MoveTemp(Assignment.Candidates)});
                if (Assignment.Kind == EAssignment::Unassigned) ++UnassignedCount; else ++OverlapCount;
            }
        }
        Analysis.Layers.Add(MoveTemp(LayerAnalysis));
    }
    if (bEmitWarnings && UnassignedCount > 0)
    {
        AddWarning(Issues, FString::Printf(TEXT("%lld non-zero density cells are not assigned to a valid Territory."), UnassignedCount));
    }
    if (bEmitWarnings && OverlapCount > 0)
    {
        AddWarning(Issues, FString::Printf(TEXT("%lld non-zero density cells are strictly inside multiple Territories."), OverlapCount));
    }
    return !GuLiMap::HasErrors(Issues);
}

FString AssignmentName(const EAssignment Kind)
{
    switch (Kind)
    {
    case EAssignment::Territory: return TEXT("territory");
    case EAssignment::Overlap: return TEXT("overlap");
    default: return TEXT("unassigned");
    }
}
}

namespace GuLiMap
{
bool HasErrors(const TArray<FGuLiMapIssue>& Issues)
{
    return Issues.ContainsByPredicate([](const FGuLiMapIssue& Issue) { return Issue.Severity == EGuLiMapIssueSeverity::Error; });
}

int32 WorldToDensityCell(const double WorldCoordinate, const double CellSizeCm)
{
    if (!FMath::IsFinite(WorldCoordinate) || !FMath::IsFinite(CellSizeCm) || CellSizeCm <= 0.0) return 0;
    const double Cell = FMath::FloorToDouble(WorldCoordinate / CellSizeCm);
    return static_cast<int32>(FMath::Clamp(Cell, static_cast<double>(MIN_int32), static_cast<double>(MAX_int32)));
}

uint8 GetDensityCell(const FGuLiMapDensityLayer& Layer, const FIntPoint Cell)
{
    const FIntPoint TileCoord = TileForCell(Cell);
    const FGuLiMapDensityTile* Tile = Layer.Tiles.FindByPredicate([&](const FGuLiMapDensityTile& Candidate) { return Candidate.TileCoord == TileCoord; });
    if (!Tile || Tile->Values.Num() != TileValueCount) return 0;
    return Tile->Values[IndexInTile(Cell, TileCoord)];
}

bool SetDensityCell(FGuLiMapDensityLayer& Layer, const FIntPoint Cell, const uint8 Value)
{
    const FIntPoint TileCoord = TileForCell(Cell);
    const int32 ExistingIndex = Layer.Tiles.IndexOfByPredicate([&](const FGuLiMapDensityTile& Candidate) { return Candidate.TileCoord == TileCoord; });
    if (ExistingIndex == INDEX_NONE && Value == 0) return false;
    int32 TileIndex = ExistingIndex;
    if (TileIndex == INDEX_NONE)
    {
        FGuLiMapDensityTile Tile;
        Tile.TileCoord = TileCoord;
        Tile.Values.Init(0, TileValueCount);
        TileIndex = Layer.Tiles.Add(MoveTemp(Tile));
    }
    FGuLiMapDensityTile& Tile = Layer.Tiles[TileIndex];
    if (Tile.Values.Num() != TileValueCount) Tile.Values.SetNumZeroed(TileValueCount);
    uint8& Existing = Tile.Values[IndexInTile(Cell, TileCoord)];
    if (Existing == Value) return false;
    Existing = Value;
    if (Value == 0 && !HasNonZero(Tile)) Layer.Tiles.RemoveAt(TileIndex);
    return true;
}

bool IsDensityMapEmpty(const FGuLiMapDensityMapRecord& DensityMap)
{
    for (const FGuLiMapDensityLayer& Layer : DensityMap.Layers)
    {
        for (const FGuLiMapDensityTile& Tile : Layer.Tiles) if (HasNonZero(Tile)) return false;
    }
    return true;
}

void NormalizeDensityMap(FGuLiMapDensityMapRecord& DensityMap)
{
    for (FGuLiMapDensityLayer& Layer : DensityMap.Layers)
    {
        TMap<FIntPoint, TArray<uint8>> Merged;
        for (const FGuLiMapDensityTile& Tile : Layer.Tiles)
        {
            TArray<uint8>& Values = Merged.FindOrAdd(Tile.TileCoord);
            if (Values.IsEmpty()) Values.Init(0, TileValueCount);
            const int32 Count = FMath::Min(Tile.Values.Num(), TileValueCount);
            for (int32 Index = 0; Index < Count; ++Index) Values[Index] = FMath::Max(Values[Index], Tile.Values[Index]);
        }
        Layer.Tiles.Reset();
        for (TPair<FIntPoint, TArray<uint8>>& Pair : Merged)
        {
            if (!Pair.Value.ContainsByPredicate([](const uint8 Value) { return Value != 0; })) continue;
            FGuLiMapDensityTile Tile;
            Tile.TileCoord = Pair.Key;
            Tile.Values = MoveTemp(Pair.Value);
            Layer.Tiles.Add(MoveTemp(Tile));
        }
        Layer.Tiles.Sort([](const FGuLiMapDensityTile& A, const FGuLiMapDensityTile& B)
        {
            return A.TileCoord.Y == B.TileCoord.Y ? A.TileCoord.X < B.TileCoord.X : A.TileCoord.Y < B.TileCoord.Y;
        });
    }
    DensityMap.Layers.Sort([](const FGuLiMapDensityLayer& A, const FGuLiMapDensityLayer& B)
    {
        if (A.LayerKey != B.LayerKey) return A.LayerKey.LexicalLess(B.LayerKey);
        return Guid(A.LayerId) < Guid(B.LayerId);
    });
}

bool EnsureDensityPresets(FGuLiMapDensityMapRecord& DensityMap)
{
    bool bChanged = false;
    if (!DensityMap.DensityMapId.IsValid()) { DensityMap.DensityMapId = FGuid::NewGuid(); bChanged = true; }
    if (DensityMap.DataVersion != 1) return bChanged;
    struct FPreset { const TCHAR* Key; const TCHAR* DisplayName; FLinearColor Color; };
    const FPreset Presets[] =
    {
        {TEXT("BlueOre"), TEXT("蓝矿"), FLinearColor(0.05f, 0.35f, 1.0f, 0.55f)},
        {TEXT("RedOre"), TEXT("红矿"), FLinearColor(1.0f, 0.08f, 0.04f, 0.55f)}
    };
    for (const FPreset& Preset : Presets)
    {
        FGuLiMapDensityLayer* Layer = DensityMap.Layers.FindByPredicate([&](const FGuLiMapDensityLayer& Candidate) { return Candidate.LayerKey == FName(Preset.Key); });
        if (!Layer)
        {
            FGuLiMapDensityLayer NewLayer;
            NewLayer.LayerId = FGuid::NewGuid();
            NewLayer.LayerKey = FName(Preset.Key);
            NewLayer.DisplayName = Preset.DisplayName;
            NewLayer.Color = Preset.Color;
            DensityMap.Layers.Add(MoveTemp(NewLayer));
            bChanged = true;
        }
        else if (!Layer->LayerId.IsValid())
        {
            Layer->LayerId = FGuid::NewGuid();
            bChanged = true;
        }
    }
    NormalizeDensityMap(DensityMap);
    return bChanged;
}

bool ApplyDensityBrush(FGuLiMapDensityMapRecord& DensityMap, const FName LayerKey, const TArray<FVector2D>& WorldSamples, const double RadiusCm, const double Strength01, const double Falloff01, const bool bErase, FString& Error)
{
    if (WorldSamples.IsEmpty()) { Error = TEXT("A density stroke requires at least one world sample."); return false; }
    if (!FMath::IsFinite(DensityMap.CellSizeCm) || DensityMap.CellSizeCm <= 0.0 || !FMath::IsFinite(RadiusCm) || RadiusCm <= 0.0 ||
        !FMath::IsFinite(Strength01) || Strength01 < 0.0 || Strength01 > 1.0 || !FMath::IsFinite(Falloff01) || Falloff01 < 0.0 || Falloff01 > 1.0)
    {
        Error = TEXT("Invalid cell size, brush radius, strength, or falloff.");
        return false;
    }
    for (const FVector2D Sample : WorldSamples) if (!FMath::IsFinite(Sample.X) || !FMath::IsFinite(Sample.Y))
    {
        Error = TEXT("Density stroke contains a non-finite sample.");
        return false;
    }
    FGuLiMapDensityLayer* Layer = DensityMap.Layers.FindByPredicate([&](const FGuLiMapDensityLayer& Candidate) { return Candidate.LayerKey == LayerKey; });
    if (!Layer) { Error = TEXT("Unknown density layer: ") + LayerKey.ToString(); return false; }

    TArray<FVector2D> Stamps;
    const double Spacing = FMath::Max(1.0, FMath::Min(DensityMap.CellSizeCm * 0.5, RadiusCm * 0.25));
    if (WorldSamples.Num() == 1) Stamps.Add(WorldSamples[0]);
    else
    {
        for (int32 Segment = 0; Segment + 1 < WorldSamples.Num(); ++Segment)
        {
            const FVector2D A = WorldSamples[Segment];
            const FVector2D B = WorldSamples[Segment + 1];
            const int32 Steps = FMath::Max(1, FMath::CeilToInt((B - A).Size() / Spacing));
            const int32 Start = Segment == 0 ? 0 : 1;
            for (int32 Step = Start; Step <= Steps; ++Step) Stamps.Add(FMath::Lerp(A, B, static_cast<double>(Step) / Steps));
        }
    }
    TMap<FIntPoint, double> Coverage;
    const double InnerRadius = RadiusCm * (1.0 - Falloff01);
    for (const FVector2D Stamp : Stamps)
    {
        const int32 MinX = WorldToDensityCell(Stamp.X - RadiusCm, DensityMap.CellSizeCm);
        const int32 MaxX = WorldToDensityCell(Stamp.X + RadiusCm, DensityMap.CellSizeCm);
        const int32 MinY = WorldToDensityCell(Stamp.Y - RadiusCm, DensityMap.CellSizeCm);
        const int32 MaxY = WorldToDensityCell(Stamp.Y + RadiusCm, DensityMap.CellSizeCm);
        for (int64 Y = MinY; Y <= static_cast<int64>(MaxY); ++Y)
        {
            for (int64 X = MinX; X <= static_cast<int64>(MaxX); ++X)
            {
                const FVector2D Center((static_cast<double>(X) + 0.5) * DensityMap.CellSizeCm, (static_cast<double>(Y) + 0.5) * DensityMap.CellSizeCm);
                const double Distance = (Center - Stamp).Size();
                if (Distance > RadiusCm) continue;
                double Weight = 1.0;
                if (Falloff01 > 0.0 && Distance > InnerRadius) Weight = (RadiusCm - Distance) / (RadiusCm - InnerRadius);
                const FIntPoint Cell(static_cast<int32>(X), static_cast<int32>(Y));
                double& Existing = Coverage.FindOrAdd(Cell);
                Existing = FMath::Max(Existing, FMath::Clamp(Weight, 0.0, 1.0));
            }
        }
    }
    for (const TPair<FIntPoint, double>& Pair : Coverage)
    {
        const int32 Delta = FMath::RoundToInt(255.0 * Strength01 * Pair.Value);
        const int32 Existing = GetDensityCell(*Layer, Pair.Key);
        SetDensityCell(*Layer, Pair.Key, static_cast<uint8>(FMath::Clamp(bErase ? Existing - Delta : Existing + Delta, 0, 255)));
    }
    NormalizeDensityMap(DensityMap);
    return true;
}

void ValidateDensity(const FGuLiMapSnapshot& Snapshot, TArray<FGuLiMapIssue>& Issues)
{
    if (!Snapshot.DensityMap.IsSet()) return;
    const FGuLiMapDensityMapRecord& DensityMap = Snapshot.DensityMap.GetValue();
    if (!DensityMap.DensityMapId.IsValid()) Issues.Emplace(TEXT("DensityMapId is invalid."), FGuid(), FGuid(), TEXT("density_map_id"));
    if (DensityMap.DataVersion != 1) Issues.Emplace(TEXT("Unsupported density data version; expected 1."), FGuid(), FGuid(), TEXT("data_version"));
    if (!FMath::IsFinite(DensityMap.CellSizeCm) || DensityMap.CellSizeCm <= 0.0) Issues.Emplace(TEXT("Density cell size must be finite and positive."), FGuid(), FGuid(), TEXT("cell_size_cm"));
    if (!IsKey(DensityMap.TerritoryTypeId.ToString()) || !IsKey(DensityMap.TerritoryRegionKey.ToString())) Issues.Emplace(TEXT("Territory type and region keys must be ASCII identifiers."), FGuid(), FGuid(), TEXT("territory"));
    TSet<FGuid> LayerIds;
    TSet<FName> LayerKeys;
    for (const FGuLiMapDensityLayer& Layer : DensityMap.Layers)
    {
        if (!Layer.LayerId.IsValid() || LayerIds.Contains(Layer.LayerId)) Issues.Emplace(TEXT("Invalid/duplicate density LayerId."), FGuid(), FGuid(), TEXT("layer_id"));
        if (Layer.LayerKey.IsNone() || !IsKey(Layer.LayerKey.ToString()) || LayerKeys.Contains(Layer.LayerKey)) Issues.Emplace(TEXT("Invalid/duplicate density LayerKey."), FGuid(), FGuid(), TEXT("layer_key"));
        if (!FMath::IsFinite(Layer.Color.R) || !FMath::IsFinite(Layer.Color.G) || !FMath::IsFinite(Layer.Color.B) || !FMath::IsFinite(Layer.Color.A)) Issues.Emplace(TEXT("Density layer color must be finite."), FGuid(), FGuid(), TEXT("color"));
        LayerIds.Add(Layer.LayerId);
        LayerKeys.Add(Layer.LayerKey);
        TSet<FIntPoint> TileCoords;
        for (const FGuLiMapDensityTile& Tile : Layer.Tiles)
        {
            if (TileCoords.Contains(Tile.TileCoord)) Issues.Emplace(TEXT("Duplicate density tile coordinate."), FGuid(), FGuid(), TEXT("tiles"));
            TileCoords.Add(Tile.TileCoord);
            if (Tile.Values.Num() != TileValueCount) Issues.Emplace(TEXT("Density tiles must contain exactly 1024 values."), FGuid(), FGuid(), TEXT("tiles"));
            else if (!HasNonZero(Tile)) Issues.Emplace(TEXT("Zero-only density tiles must be removed."), FGuid(), FGuid(), TEXT("tiles"));
        }
    }
    if (HasErrors(Issues)) return;
    FDensityAnalysis Analysis;
    AnalyzeDensity(Snapshot, Analysis, Issues, true);
}

bool BuildDensityFiles(const FGuLiMapSnapshot& Snapshot, TMap<FString, FString>& Files, TArray<FGuLiMapIssue>& Issues)
{
    if (!Snapshot.DensityMap.IsSet()) return true;
    FDensityAnalysis Analysis;
    TArray<FGuLiMapIssue> LocalIssues;
    if (!AnalyzeDensity(Snapshot, Analysis, LocalIssues, false))
    {
        Issues.Append(LocalIssues);
        return false;
    }
    const FGuLiMapDensityMapRecord& DensityMap = Snapshot.DensityMap.GetValue();
    auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schema_version"), 1);
    Root->SetStringField(TEXT("map_package"), Snapshot.MapPackage);
    Root->SetStringField(TEXT("length_unit"), TEXT("cm"));
    Root->SetStringField(TEXT("coordinate_system"), TEXT("world-aligned XY grid; cell index=floor(world_xy/cell_size_cm); Z is preview-only"));
    Root->SetStringField(TEXT("density_map_id"), Guid(DensityMap.DensityMapId));
    Root->SetNumberField(TEXT("cell_size_cm"), DensityMap.CellSizeCm);
    Root->SetStringField(TEXT("territory_type_id"), DensityMap.TerritoryTypeId.ToString());
    Root->SetStringField(TEXT("territory_region_key"), DensityMap.TerritoryRegionKey.ToString());

    FString CellCsv;
    CsvRow(CellCsv, {TEXT("layer_id"), TEXT("layer_key"), TEXT("cell_x"), TEXT("cell_y"), TEXT("world_center_x_cm"), TEXT("world_center_y_cm"), TEXT("density_u8"), TEXT("density_01")});
    TArray<TSharedPtr<FJsonValue>> JsonLayers;
    for (const FLayerAnalysis& LayerAnalysis : Analysis.Layers)
    {
        const FGuLiMapDensityLayer& Layer = *LayerAnalysis.Layer;
        auto LayerJson = MakeShared<FJsonObject>();
        LayerJson->SetStringField(TEXT("layer_id"), Guid(Layer.LayerId));
        LayerJson->SetStringField(TEXT("layer_key"), Layer.LayerKey.ToString());
        LayerJson->SetStringField(TEXT("display_name"), Layer.DisplayName);
        LayerJson->SetField(TEXT("color_rgb"), VectorJson(FVector(Layer.Color.R, Layer.Color.G, Layer.Color.B)));
        LayerJson->SetNumberField(TEXT("color_alpha"), Layer.Color.A);
        TArray<TSharedPtr<FJsonValue>> JsonCells;
        for (const FDensityCell& Cell : LayerAnalysis.Cells)
        {
            auto CellJson = MakeShared<FJsonObject>();
            CellJson->SetNumberField(TEXT("cell_x"), Cell.Coord.X);
            CellJson->SetNumberField(TEXT("cell_y"), Cell.Coord.Y);
            CellJson->SetNumberField(TEXT("density_u8"), Cell.Density);
            JsonCells.Add(JsonObjectValue(CellJson));
            const double WorldX = (static_cast<double>(Cell.Coord.X) + 0.5) * DensityMap.CellSizeCm;
            const double WorldY = (static_cast<double>(Cell.Coord.Y) + 0.5) * DensityMap.CellSizeCm;
            CsvRow(CellCsv, {Guid(Layer.LayerId), Layer.LayerKey.ToString(), FString::FromInt(Cell.Coord.X), FString::FromInt(Cell.Coord.Y), Number(WorldX), Number(WorldY), FString::FromInt(Cell.Density), Number(static_cast<double>(Cell.Density) / 255.0)});
        }
        LayerJson->SetArrayField(TEXT("cells"), JsonCells);
        JsonLayers.Add(JsonObjectValue(LayerJson));
    }
    Root->SetArrayField(TEXT("layers"), JsonLayers);

    FString TerritoryCsv;
    CsvRow(TerritoryCsv, {TEXT("assignment_kind"), TEXT("marker_id"), TEXT("marker_key"), TEXT("region_id"), TEXT("layer_id"), TEXT("layer_key"), TEXT("cell_count"), TEXT("density_weight_sum"), TEXT("weighted_area_m2")});
    TArray<TSharedPtr<FJsonValue>> JsonSummary;
    const double CellAreaM2 = DensityMap.CellSizeCm * DensityMap.CellSizeCm / 10000.0;
    for (const FLayerAnalysis& LayerAnalysis : Analysis.Layers)
    {
        for (const FTerritoryStat& Stat : LayerAnalysis.Stats)
        {
            FString MarkerId;
            FString MarkerKey;
            FString RegionId;
            if (Stat.Kind == EAssignment::Territory)
            {
                const FTerritory& Territory = Analysis.Territories[Stat.TerritoryIndex];
                MarkerId = Guid(Territory.MarkerId);
                MarkerKey = Territory.MarkerKey;
                RegionId = Guid(Territory.RegionId);
            }
            const FString Kind = AssignmentName(Stat.Kind);
            const double WeightedArea = Stat.WeightSum * CellAreaM2;
            CsvRow(TerritoryCsv, {Kind, MarkerId, MarkerKey, RegionId, Guid(LayerAnalysis.Layer->LayerId), LayerAnalysis.Layer->LayerKey.ToString(), FString::FromInt(Stat.CellCount), Number(Stat.WeightSum), Number(WeightedArea)});
            auto Summary = MakeShared<FJsonObject>();
            Summary->SetStringField(TEXT("assignment_kind"), Kind);
            Summary->SetStringField(TEXT("marker_id"), MarkerId);
            Summary->SetStringField(TEXT("marker_key"), MarkerKey);
            Summary->SetStringField(TEXT("region_id"), RegionId);
            Summary->SetStringField(TEXT("layer_id"), Guid(LayerAnalysis.Layer->LayerId));
            Summary->SetStringField(TEXT("layer_key"), LayerAnalysis.Layer->LayerKey.ToString());
            Summary->SetNumberField(TEXT("cell_count"), Stat.CellCount);
            Summary->SetNumberField(TEXT("density_weight_sum"), Stat.WeightSum);
            Summary->SetNumberField(TEXT("weighted_area_m2"), WeightedArea);
            JsonSummary.Add(JsonObjectValue(Summary));
        }
    }
    Root->SetArrayField(TEXT("territory_summary"), JsonSummary);

    TArray<TSharedPtr<FJsonValue>> JsonAnomalies;
    for (const FAnomaly& Anomaly : Analysis.Anomalies)
    {
        auto Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("assignment_kind"), AssignmentName(Anomaly.Kind));
        Json->SetStringField(TEXT("layer_key"), Anomaly.LayerKey);
        Json->SetNumberField(TEXT("cell_x"), Anomaly.Cell.X);
        Json->SetNumberField(TEXT("cell_y"), Anomaly.Cell.Y);
        Json->SetNumberField(TEXT("density_u8"), Anomaly.Density);
        TArray<TSharedPtr<FJsonValue>> Candidates;
        for (const int32 Candidate : Anomaly.Candidates)
        {
            const FTerritory& Territory = Analysis.Territories[Candidate];
            Candidates.Add(JsonStringValue(Territory.MarkerKey + TEXT(":") + Guid(Territory.RegionId)));
        }
        Json->SetArrayField(TEXT("territory_candidates"), Candidates);
        JsonAnomalies.Add(JsonObjectValue(Json));
    }
    Root->SetArrayField(TEXT("anomalies"), JsonAnomalies);

    Files.Add(TEXT("density_layers.json"), CanonicalJson(JsonObjectValue(Root)) + TEXT("\n"));
    Files.Add(TEXT("density_cells.csv"), MoveTemp(CellCsv));
    Files.Add(TEXT("density_territories.csv"), MoveTemp(TerritoryCsv));
    return true;
}
}
