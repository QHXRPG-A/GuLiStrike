#include "GuLiMapDensityMap.h"

#include "GuLiMapAuthoring.h"
#include "GuLiMapAuthoringSettings.h"
#include "DynamicMeshBuilder.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "EngineDefines.h"
#include "EngineUtils.h"
#include "LandscapeProxy.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "MeshElementCollector.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "SceneInterface.h"
#include "SceneView.h"

namespace
{
struct FHeatRenderData
{
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    FLinearColor Color = FLinearColor::White;
};

class FGuLiMapDensitySceneProxy final : public FPrimitiveSceneProxy
{
public:
    explicit FGuLiMapDensitySceneProxy(const UGuLiMapDensityVisualizationComponent* Component)
        : FPrimitiveSceneProxy(Component)
    {
        bWillEverBeLit = false;
        FillMaterial = GEngine && GEngine->DebugMeshMaterial ? GEngine->DebugMeshMaterial->GetRenderProxy() : nullptr;
        if (FillMaterial) FillRelevance = GEngine->DebugMeshMaterial->GetRelevance_Concurrent(GetScene().GetShaderPlatform());
        const AGuLiMapDensityMap* Actor = Cast<AGuLiMapDensityMap>(Component->GetOwner());
        if (!Actor || !FMath::IsFinite(Actor->Record.CellSizeCm) || Actor->Record.CellSizeCm <= 0.0) return;
        const double CellSize = Actor->Record.CellSizeCm;
        int32 VisibleLayerIndex = 0;
        for (const FGuLiMapDensityLayer& Layer : Actor->Record.Layers)
        {
            if (!Actor->IsLayerVisible(Layer.LayerKey)) continue;
            TArray<FHeatRenderData> Buckets;
            Buckets.SetNum(16);
            for (int32 Bucket = 0; Bucket < Buckets.Num(); ++Bucket)
            {
                const float Alpha = FMath::Lerp(0.05f, FMath::Clamp(Layer.Color.A, 0.08f, 0.8f), static_cast<float>(Bucket + 1) / Buckets.Num());
                Buckets[Bucket].Color = FLinearColor(Layer.Color.R, Layer.Color.G, Layer.Color.B, Alpha);
            }
            for (const FGuLiMapDensityTile& Tile : Layer.Tiles)
            {
                if (Tile.Values.Num() != GuLiMap::DensityTileSize * GuLiMap::DensityTileSize) continue;
                for (int32 ValueIndex = 0; ValueIndex < Tile.Values.Num(); ++ValueIndex)
                {
                    const uint8 Density = Tile.Values[ValueIndex];
                    if (Density == 0) continue;
                    const int64 CellX64 = static_cast<int64>(Tile.TileCoord.X) * GuLiMap::DensityTileSize + ValueIndex % GuLiMap::DensityTileSize;
                    const int64 CellY64 = static_cast<int64>(Tile.TileCoord.Y) * GuLiMap::DensityTileSize + ValueIndex / GuLiMap::DensityTileSize;
                    if (CellX64 < MIN_int32 || CellX64 > MAX_int32 || CellY64 < MIN_int32 || CellY64 > MAX_int32) continue;
                    const FIntPoint Cell(static_cast<int32>(CellX64), static_cast<int32>(CellY64));
                    if (Cell.X==MAX_int32||Cell.Y==MAX_int32) continue;
                    const int32 BucketIndex = FMath::Min(15, static_cast<int32>(Density) / 16);
                    FHeatRenderData& Mesh = Buckets[BucketIndex];
                    const int32 Base = Mesh.Vertices.Num();
                    const double X0 = static_cast<double>(Cell.X) * CellSize;
                    const double Y0 = static_cast<double>(Cell.Y) * CellSize;
                    const double X1 = X0 + CellSize;
                    const double Y1 = Y0 + CellSize;
                    const double Offset = 12.0 + VisibleLayerIndex * 5.0;
                    Mesh.Vertices.Append({
                        FVector(X0, Y0, Component->SampleSurfaceHeight(FIntPoint(Cell.X, Cell.Y), CellSize) + Offset),
                        FVector(X1, Y0, Component->SampleSurfaceHeight(FIntPoint(Cell.X + 1, Cell.Y), CellSize) + Offset),
                        FVector(X1, Y1, Component->SampleSurfaceHeight(FIntPoint(Cell.X + 1, Cell.Y + 1), CellSize) + Offset),
                        FVector(X0, Y1, Component->SampleSurfaceHeight(FIntPoint(Cell.X, Cell.Y + 1), CellSize) + Offset)});
                    Mesh.Triangles.Append({Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
                }
            }
            for (FHeatRenderData& Mesh : Buckets) if (!Mesh.Triangles.IsEmpty()) Meshes.Add(MoveTemp(Mesh));
            ++VisibleLayerIndex;
        }
    }

    SIZE_T GetTypeHash() const override { static size_t Unique; return reinterpret_cast<size_t>(&Unique); }
    uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }
    FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
    {
        FPrimitiveViewRelevance Relevance;
        Relevance.bDrawRelevance = IsShown(View) && !View->Family->EngineShowFlags.Game;
        Relevance.bDynamicRelevance = true;
        Relevance.bEditorPrimitiveRelevance = false;
        FillRelevance.SetPrimitiveViewRelevance(Relevance);
        Relevance.bOpaque = true;
        return Relevance;
    }
    void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily&, const uint32 VisibilityMap, FMeshElementCollector& Collector) const override
    {
        if (!FillMaterial) return;
        for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
        {
            if (!(VisibilityMap & (1 << ViewIndex))) continue;
            for (const FHeatRenderData& Data : Meshes)
            {
                FColoredMaterialRenderProxy* Material = new FColoredMaterialRenderProxy(FillMaterial, Data.Color);
                Collector.RegisterOneFrameMaterialProxy(Material);
                FDynamicMeshBuilder Mesh(Views[ViewIndex]->GetFeatureLevel());
                for (const FVector Vertex : Data.Vertices)
                {
                    Mesh.AddVertex(FVector3f(Vertex), FVector2f::ZeroVector, FVector3f(1, 0, 0), FVector3f(0, 1, 0), FVector3f(0, 0, 1), FColor::White);
                }
                for (int32 Index = 0; Index + 2 < Data.Triangles.Num(); Index += 3) Mesh.AddTriangle(Data.Triangles[Index], Data.Triangles[Index + 1], Data.Triangles[Index + 2]);
                Mesh.GetMesh(GetLocalToWorld(), Material, SDPG_World, true, false, ViewIndex, Collector);
            }
        }
    }
private:
    TArray<FHeatRenderData> Meshes;
    const FMaterialRenderProxy* FillMaterial = nullptr;
    FMaterialRelevance FillRelevance;
};
}

UGuLiMapDensityVisualizationComponent::UGuLiMapDensityVisualizationComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetGenerateOverlapEvents(false);
    SetCanEverAffectNavigation(false);
    CastShadow = false;
    bUseEditorCompositing = false;
    bHiddenInGame = true;
    bIsEditorOnly = true;
}

FPrimitiveSceneProxy* UGuLiMapDensityVisualizationComponent::CreateSceneProxy()
{
    return new FGuLiMapDensitySceneProxy(this);
}

FBoxSphereBounds UGuLiMapDensityVisualizationComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    FBox Box(FVector(-100.0), FVector(100.0));
    if (const AGuLiMapDensityMap* Actor = Cast<AGuLiMapDensityMap>(GetOwner()))
    {
        const double CellSize = FMath::Max(1.0, Actor->Record.CellSizeCm);
        for (const FGuLiMapDensityLayer& Layer : Actor->Record.Layers)
        {
            for (const FGuLiMapDensityTile& Tile : Layer.Tiles)
            {
                if (Tile.Values.Num() != GuLiMap::DensityTileSize * GuLiMap::DensityTileSize) continue;
                const int64 CornerX0=static_cast<int64>(Tile.TileCoord.X)*GuLiMap::DensityTileSize;
                const int64 CornerY0=static_cast<int64>(Tile.TileCoord.Y)*GuLiMap::DensityTileSize;
                const int64 CornerX1=CornerX0+GuLiMap::DensityTileSize;
                const int64 CornerY1=CornerY0+GuLiMap::DensityTileSize;
                if (CornerX0<MIN_int32||CornerX1>MAX_int32||CornerY0<MIN_int32||CornerY1>MAX_int32) continue;
                for (const FIntPoint Corner:{FIntPoint(CornerX0,CornerY0),FIntPoint(CornerX1,CornerY0),FIntPoint(CornerX1,CornerY1),FIntPoint(CornerX0,CornerY1)})
                {
                    const double Z=SampleSurfaceHeight(Corner,CellSize); Box+=FVector(static_cast<double>(Corner.X)*CellSize,static_cast<double>(Corner.Y)*CellSize,Z-100.0); Box+=FVector(static_cast<double>(Corner.X)*CellSize,static_cast<double>(Corner.Y)*CellSize,Z+100.0);
                }
            }
        }
    }
    return FBoxSphereBounds(Box).TransformBy(LocalToWorld);
}

double UGuLiMapDensityVisualizationComponent::SampleSurfaceHeight(const FIntPoint Corner, const double CellSizeCm) const
{
    if (const double* Cached = SurfaceHeightCache.Find(Corner)) return *Cached;
    const FVector XY(static_cast<double>(Corner.X) * CellSizeCm, static_cast<double>(Corner.Y) * CellSizeCm, 0.0);
    double Height = GetDefault<UGuLiMapAuthoringSettings>()->WorkPlaneZ;
    UWorld* World = GetWorld();
    bool bFound = false;
    if (World)
    {
        for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
        {
            if (const TOptional<float> LandscapeHeight = It->GetHeightAtLocation(XY))
            {
                Height = LandscapeHeight.GetValue();
                bFound = true;
                break;
            }
        }
        if (!bFound)
        {
            FHitResult Hit;
            const FVector Start(XY.X, XY.Y, HALF_WORLD_MAX * 0.5);
            const FVector End(XY.X, XY.Y, -HALF_WORLD_MAX * 0.5);
            if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility)) Height = Hit.ImpactPoint.Z;
        }
    }
    SurfaceHeightCache.Add(Corner, Height);
    return Height;
}

void UGuLiMapDensityVisualizationComponent::InvalidateSurfaceCache()
{
    SurfaceHeightCache.Reset();
}

AGuLiMapDensityMap::AGuLiMapDensityMap()
{
    PrimaryActorTick.bCanEverTick = false;
    bIsEditorOnlyActor = true;
    bIsSpatiallyLoaded = false;
    SetLockLocation(true);
    SetActorEnableCollision(false);
    Visualization = CreateDefaultSubobject<UGuLiMapDensityVisualizationComponent>(TEXT("DensityHeatmap"));
    SetRootComponent(Visualization);
}

void AGuLiMapDensityMap::EnforceEditorInvariants()
{
    SetActorTransform(FTransform::Identity);
    SetActorEnableCollision(false);
    bIsSpatiallyLoaded = false;
    SetFolderPath(TEXT("GuLi/MapAuthoring"));
}

void AGuLiMapDensityMap::RegenerateIdentity()
{
    Record.DensityMapId = FGuid::NewGuid();
    for (FGuLiMapDensityLayer& Layer : Record.Layers) Layer.LayerId = FGuid::NewGuid();
}

void AGuLiMapDensityMap::PostActorCreated()
{
    Super::PostActorCreated();
    if (!IsTemplate())
    {
        GuLiMap::EnsureDensityPresets(Record);
        EnforceEditorInvariants();
        RefreshVisuals(true);
    }
}

void AGuLiMapDensityMap::PostDuplicate(const EDuplicateMode::Type DuplicateMode)
{
    Super::PostDuplicate(DuplicateMode);
    if (DuplicateMode == EDuplicateMode::Normal && !IsTemplate()) RegenerateIdentity();
    EnforceEditorInvariants();
    RefreshVisuals(true);
}

void AGuLiMapDensityMap::PostEditImport()
{
    Super::PostEditImport();
    RegenerateIdentity();
    EnforceEditorInvariants();
    RefreshVisuals(true);
}

void AGuLiMapDensityMap::PreEditChange(FProperty* PropertyAboutToChange)
{
    CellSizeBeforeEdit=Record.CellSizeCm; bDensityWasEmptyBeforeEdit=GuLiMap::IsDensityMapEmpty(Record); Super::PreEditChange(PropertyAboutToChange);
}

void AGuLiMapDensityMap::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    if (!FMath::IsNearlyEqual(CellSizeBeforeEdit,Record.CellSizeCm)&&!bDensityWasEmptyBeforeEdit)
    {
        Record.CellSizeCm=CellSizeBeforeEdit;
        UE_LOG(LogTemp,Warning,TEXT("GuLiMap: clear all density layers explicitly before changing CellSizeCm; direct edit was rolled back."));
    }
    GuLiMap::NormalizeDensityMap(Record);
    EnforceEditorInvariants();
    Super::PostEditChangeProperty(Event);
    RefreshVisuals(Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(FGuLiMapDensityMapRecord, CellSizeCm));
}

void AGuLiMapDensityMap::PostEditMove(const bool bFinished)
{
    EnforceEditorInvariants();
    Super::PostEditMove(bFinished);
    RefreshVisuals(false);
}

void AGuLiMapDensityMap::PostEditUndo()
{
    Super::PostEditUndo();
    GuLiMap::NormalizeDensityMap(Record);
    EnforceEditorInvariants();
    RefreshVisuals(true);
}

void AGuLiMapDensityMap::PostLoad()
{
    Super::PostLoad();
    GuLiMap::NormalizeDensityMap(Record);
    EnforceEditorInvariants();
}

void AGuLiMapDensityMap::RefreshVisuals(const bool bInvalidateSurface)
{
    if (Visualization)
    {
        if (bInvalidateSurface) Visualization->InvalidateSurfaceCache();
        Visualization->UpdateBounds();
        Visualization->MarkRenderStateDirty();
    }
    if (GEditor) GEditor->RedrawLevelEditingViewports();
}

bool AGuLiMapDensityMap::IsLayerVisible(const FName LayerKey) const
{
    return !HiddenLayers.Contains(LayerKey);
}

void AGuLiMapDensityMap::SetLayerVisible(const FName LayerKey, const bool bVisible)
{
    if (bVisible) HiddenLayers.Remove(LayerKey); else HiddenLayers.Add(LayerKey);
    RefreshVisuals(false);
}

void AGuLiMapDensityMap::ShowAllLayers()
{
    HiddenLayers.Reset();
    RefreshVisuals(false);
}
