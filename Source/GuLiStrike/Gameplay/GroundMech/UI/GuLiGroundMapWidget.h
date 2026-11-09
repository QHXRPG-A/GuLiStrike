#pragma once
#include "Components/Widget.h"
#include "Rendering/DrawElements.h"
#include "Gameplay/GroundMech/UI/GuLiGroundHUDData.h"
#include "GuLiGroundMapWidget.generated.h"
struct FGuLiGridSnapshot;
class SGuLiGroundMap;
class UTexture2D;
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiGroundMapSelection,const FGuLiGroundHUDMarker*);

UCLASS()
class GULISTRIKE_API UGuLiGroundMapWidget final:public UWidget
{
    GENERATED_BODY()
public:
    void Configure(bool Full,double InRange,double InContourStep,double InHeightScale);
    void SetSnapshot(const FGuLiGroundHUDSnapshot* InData);
    void Present(float Delta,float Yaw);
    void CenterPlayer();
    void SetPreviewLocation(FVector Location);
    bool HasTerrain() const;
    void SetColors(FLinearColor Panel,FLinearColor Raised,FLinearColor Line,FLinearColor Secondary,FLinearColor Accent);
    FGuLiGroundMapSelection OnSelected;
    virtual void ReleaseSlateResources(bool ReleaseChildren) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    friend class SGuLiGroundMap;
    void CacheTerrain();
    void CachePlanarTerrain();
    void ReleasePlanarTerrain();
    void CacheMarkers();
    FVector2D Project(const FVector& P,FVector2D Size) const;
    int32 Paint(const FGeometry&,FSlateWindowElementList&,int32) const;
    void Select(FVector2D Point,FVector2D Size);
    void Pan(FVector2D Delta,FVector2D Size);
    void Zoom(float Delta);
    TSharedPtr<SGuLiGroundMap> Surface;
    TSharedPtr<const FGuLiGridSnapshot,ESPMode::ThreadSafe> Terrain;
    const FGuLiGroundHUDSnapshot* Data=nullptr;
    TArray<FVector> Vertices;TArray<SlateIndex> Indices;
	// Slate copies submitted data; retain CPU scratch capacity for the next paint.
	mutable TArray<FSlateVertex> PaintVertices;
	mutable TArray<SlateIndex> PaintIndices;
	struct FTerrainPaintBatch
	{
		TArray<FSlateVertex> Vertices;
		TArray<FVector2f> Positions;
		TArray<SlateIndex> Indices;
		int32 FixedVertices=0;
	};
	mutable TArray<FTerrainPaintBatch> TerrainPaintBatches;
	mutable FVector TerrainPaintCenter=FVector::ZeroVector;
	mutable FVector2D TerrainPaintSize=FVector2D::ZeroVector;
	mutable FSlateRenderTransform TerrainPaintTransform;
	mutable float TerrainPaintHeading=0;
    TArray<FColor> TerrainColors;
    TArray<FVector> TerrainPerimeter;
    TArray<TArray<SlateIndex>> HeadingIndices;
    TArray<TPair<FVector,FVector>> Contours;
    // Full-map shading is generated from every height sample once, then drawn as
    // one textured quad. UMG retains the transient texture across GC.
    UPROPERTY(Transient) TObjectPtr<UTexture2D> PlanarTerrainTexture;
    FSlateBrush PlanarTerrainBrush;
    struct FMarkerGroup { FGuLiGroundHUDMarker Marker;int32 Count=1; };
    TArray<FMarkerGroup> MarkerGroups;
    FVector Center=FVector::ZeroVector,CachedCenter=FVector(UE_BIG_NUMBER);
    FVector2D MapPan=FVector2D::ZeroVector;
    FGuLiHybridObjectKey Selected;
    FLinearColor PanelColor,RaisedColor,LineColor,SecondaryColor,AccentColor;
    double Range=45000,ContourStep=1000,HeightScale=2,ZoomFactor=1;
    double MinimumHeight=0,MaximumHeight=1;
    FVector2D PatchOrigin=FVector2D::ZeroVector;
    double PatchSpan=0;
    float Heading=0;bool bFull=false,bCentered=false;
};
