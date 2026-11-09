#include "Gameplay/GroundMech/UI/GuLiGroundMapWidget.h"
#include "Gameplay/Navigation/Grid/GuLiGroundGridSubsystem.h"
#include "Gameplay/Navigation/Grid/GuLiGridNavigation.h"
#include "Gameplay/Navigation/Grid/GuLiGridTypes.h"
#include "Gameplay/Presentation/GuLiLocalTeamColors.h"
#include "Gameplay/GroundMech/UI/GuLiGroundHUDVisuals.h"
#include "Engine/World.h"
#include "Widgets/SLeafWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "InputCoreTypes.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

class SGuLiGroundMap final:public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SGuLiGroundMap){}SLATE_END_ARGS()
    void Construct(const FArguments&,UGuLiGroundMapWidget* In){Owner=In;}
    virtual FVector2D ComputeDesiredSize(float) const override{return FVector2D(320,280);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& E,int32 L,const FWidgetStyle&,bool) const override
    {return Owner.IsValid()?Owner->Paint(G,E,L):L;}
    virtual FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E) override
    {
        if(!Owner.IsValid()||!Owner->bFull||E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
        Start=Last=G.AbsoluteToLocal(E.GetScreenSpacePosition());Dragged=false;return FReply::Handled().CaptureMouse(SharedThis(this));
    }
    virtual FReply OnMouseMove(const FGeometry& G,const FPointerEvent& E) override
    {
        if(!HasMouseCapture()||!Owner.IsValid())return FReply::Unhandled();
        const auto P=G.AbsoluteToLocal(E.GetScreenSpacePosition());Dragged|=FVector2D::Distance(P,Start)>4;
        if(Dragged)Owner->Pan(P-Last,G.GetLocalSize());Last=P;return FReply::Handled();
    }
    virtual FReply OnMouseButtonUp(const FGeometry& G,const FPointerEvent& E) override
    {
        if(!HasMouseCapture()||E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
        if(!Dragged&&Owner.IsValid())Owner->Select(G.AbsoluteToLocal(E.GetScreenSpacePosition()),G.GetLocalSize());
        return FReply::Handled().ReleaseMouseCapture();
    }
    virtual FReply OnMouseWheel(const FGeometry&,const FPointerEvent& E) override
    {if(!Owner.IsValid()||!Owner->bFull)return FReply::Unhandled();Owner->Zoom(E.GetWheelDelta());return FReply::Handled();}
private:TWeakObjectPtr<UGuLiGroundMapWidget> Owner;FVector2D Start,Last;bool Dragged=false;
};
TSharedRef<SWidget> UGuLiGroundMapWidget::RebuildWidget(){Surface=SNew(SGuLiGroundMap,this);return Surface.ToSharedRef();}
void UGuLiGroundMapWidget::ReleaseSlateResources(bool Release){Super::ReleaseSlateResources(Release);Surface.Reset();ReleasePlanarTerrain();Terrain.Reset();Data=nullptr;Vertices.Reset();Contours.Reset();Indices.Reset();TerrainColors.Reset();TerrainPerimeter.Reset();HeadingIndices.Reset();MarkerGroups.Reset();TerrainPaintBatches.Reset();}
bool UGuLiGroundMapWidget::HasTerrain() const{return Terrain && !Terrain->Heights.IsEmpty() && (bFull?PlanarTerrainTexture!=nullptr:!Vertices.IsEmpty());}
void UGuLiGroundMapWidget::SetColors(FLinearColor Panel,FLinearColor Raised,FLinearColor Line,FLinearColor Secondary,FLinearColor Accent)
{PanelColor=Panel;RaisedColor=Raised;LineColor=Line;SecondaryColor=Secondary;AccentColor=Accent;CachedCenter=FVector(UE_BIG_NUMBER);ReleasePlanarTerrain();TerrainPaintBatches.Reset();}
void UGuLiGroundMapWidget::Configure(bool Full,double InRange,double Step,double Scale)
{
    bFull=Full;Range=InRange;ContourStep=FMath::Max(100.,Step);HeightScale=Scale;
    TerrainPaintBatches.Reset();
    // UWidget::SynchronizeProperties copies this value onto the Slate surface.
    // Setting only the Slate constructor was overwritten by UMG's Inherit default.
    SetClipping(EWidgetClipping::ClipToBoundsAlways);
}
void UGuLiGroundMapWidget::SetSnapshot(const FGuLiGroundHUDSnapshot* In)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMap_Snapshot);
    Data=In;if(!bCentered&&Data){Center=Data->Position;bCentered=true;}
    const auto* Grid=GetWorld()?GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>():nullptr;
    const auto Next=Grid?Grid->Snapshot():nullptr;
    if((!Next)!=(!Terrain)||(Next&&Terrain&&(Next->Terrain.Get()!=Terrain->Terrain.Get()||Next->SourceHash!=Terrain->SourceHash||Next->TerrainRevision!=Terrain->TerrainRevision||Next->Cells!=Terrain->Cells||Next->Origin!=Terrain->Origin)))
    {Terrain=Next;CachedCenter=FVector(UE_BIG_NUMBER);ReleasePlanarTerrain();Vertices.Reset();Contours.Reset();Indices.Reset();}
    CacheTerrain();CacheMarkers();
    if(bFull&&Selected.IsValid()&&Data){const auto* Marker=Data->Markers.Find(Selected);OnSelected.Broadcast(Marker);if(!Marker)Selected={};}
    if(Surface)Surface->Invalidate(EInvalidateWidgetReason::Paint);
}
void UGuLiGroundMapWidget::CenterPlayer(){if(Data)MapPan=FVector2D(Data->Position);if(Surface)Surface->Invalidate(EInvalidateWidgetReason::Paint);}
void UGuLiGroundMapWidget::SetPreviewLocation(FVector Location)
{if(!Center.Equals(Location,1.)){Center=Location;bCentered=true;CacheTerrain();if(Surface)Surface->Invalidate(EInvalidateWidgetReason::Paint);}}
void UGuLiGroundMapWidget::Present(float Delta,float Yaw)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMap_Present);
    if(bFull||!Data)return;
    const FVector Next=FMath::VInterpTo(Center,Data->Position,Delta,16.f);
    if(!Next.Equals(Center,.1)||!FMath::IsNearlyEqual(Yaw,Heading,.02f))
    {Center=Next;Heading=Yaw;CacheTerrain();if(Surface)Surface->Invalidate(EInvalidateWidgetReason::Paint);}
}
void UGuLiGroundMapWidget::CacheTerrain()
{
    if(!Terrain||Terrain->Heights.IsEmpty())return;
    if(bFull){if(!PlanarTerrainTexture)CachePlanarTerrain();return;}
    const FVector Anchor(FMath::GridSnap(Center.X,3000.),FMath::GridSnap(Center.Y,3000.),0);
    if(!Vertices.IsEmpty()&&Anchor==CachedCenter)return;CachedCenter=Anchor;
    TerrainPaintBatches.Reset();
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMap_RebuildTerrain);
    const int32 N=64;const double Span=Range*.88;
    const FVector2D Origin=FVector2D(Anchor)-FVector2D(Span*.5);
    Vertices.SetNum((N+1)*(N+1));Indices.Reset(N*N*6);Contours.Reset();TerrainColors.SetNum(Vertices.Num());TerrainPerimeter.Reset();HeadingIndices.Reset();
    PatchOrigin=Origin;PatchSpan=Span;MinimumHeight=DBL_MAX;MaximumHeight=-DBL_MAX;
    const FVector2D Maximum=Terrain->Origin+FVector2D(Terrain->Cells)*GuLiGrid::CellSize;
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMap_Heights);
        for(int32 Y=0;Y<=N;++Y)for(int32 X=0;X<=N;++X)
        {
            const auto XY=Origin+FVector2D(X,Y)*(Span/N);
            Vertices[Y*(N+1)+X]=FVector(XY.X,XY.Y,Terrain->HeightAt(XY.X,XY.Y));
            MinimumHeight=FMath::Min(MinimumHeight,Vertices[Y*(N+1)+X].Z);MaximumHeight=FMath::Max(MaximumHeight,Vertices[Y*(N+1)+X].Z);
        }
    }
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMap_Shading);
        const FVector Light=FVector(-.55,-.65,.7).GetSafeNormal();
        for(int32 I=0;I<Vertices.Num();++I)
        {
            const auto& P=Vertices[I];const double Step=Span/N;
            const double DX=(Terrain->HeightAt(P.X+Step,P.Y)-Terrain->HeightAt(P.X-Step,P.Y))/(2*Step);
            const double DY=(Terrain->HeightAt(P.X,P.Y+Step)-Terrain->HeightAt(P.X,P.Y-Step))/(2*Step);
            const double Relief=FMath::Max(2.,HeightScale);
            const double Lit=FMath::Clamp(FVector::DotProduct(FVector(-DX*Relief,-DY*Relief,1).GetSafeNormal(),Light),0.,1.);
            const double Elevation=(P.Z-MinimumHeight)/FMath::Max(1.,MaximumHeight-MinimumHeight);
            const double Neighbours=(Terrain->HeightAt(P.X+Step*3,P.Y)+Terrain->HeightAt(P.X-Step*3,P.Y)+Terrain->HeightAt(P.X,P.Y+Step*3)+Terrain->HeightAt(P.X,P.Y-Step*3))*.25;
            const double Valley=FMath::Clamp((Neighbours-P.Z)/FMath::Max(100.,Step),0.,.45);
            const double Band=FMath::FloorToDouble((P.Z-MinimumHeight)/ContourStep)*ContourStep/FMath::Max(ContourStep,MaximumHeight-MinimumHeight);
            // Directional hill shading, elevation bands and local valley shading are
            // derived once from the real height field when this terrain patch changes.
            const double Facet=Lit<.28?.06:Lit<.65?.24:.48;
            auto Tint=FMath::Lerp(PanelColor*.45f,SecondaryColor,float(FMath::Clamp(Facet+Elevation*.07+Band*.04-Valley*.35,.025,.62)));Tint.A=1;
            TerrainColors[I]=Tint.ToFColor(true);
        }
    }
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMap_Contours);
        for(int32 Y=0;Y<N;++Y)for(int32 X=0;X<N;++X)
        {
            const int32 A=Y*(N+1)+X,B=A+1,C=A+N+2,D=A+N+1;
            if(Vertices[A].X<Terrain->Origin.X||Vertices[A].Y<Terrain->Origin.Y||Vertices[C].X>Maximum.X||Vertices[C].Y>Maximum.Y)continue;
            for(int32 I:{A,B,C,A,C,D})Indices.Add(SlateIndex(I));
            const FVector P[4]={Vertices[A],Vertices[B],Vertices[C],Vertices[D]};
            const double Low=FMath::Min(FMath::Min(P[0].Z,P[1].Z),FMath::Min(P[2].Z,P[3].Z));
            const double High=FMath::Max(FMath::Max(P[0].Z,P[1].Z),FMath::Max(P[2].Z,P[3].Z));
            for(double H=FMath::CeilToDouble(Low/ContourStep)*ContourStep;H<High;H+=ContourStep)
            {
                TArray<FVector,TInlineAllocator<4>> Crossings;
                for(int32 E=0;E<4;++E){const auto& P0=P[E];const auto& P1=P[(E+1)%4];if((P0.Z<=H&&P1.Z>H)||(P1.Z<=H&&P0.Z>H))Crossings.Add(FMath::Lerp(P0,P1,(H-P0.Z)/(P1.Z-P0.Z)));}
                for(int32 I=1;I<Crossings.Num();I+=2)Contours.Emplace(Crossings[I-1],Crossings[I]);
            }
        }
    }
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMap_PainterOrders);
        // Cache the real border profile once. Its vertical cutaway supplies depth
        // without adding any scene capture or per-frame height queries.
        for(int32 X=0;X<N;++X)TerrainPerimeter.Add(Vertices[X]);
        for(int32 Y=0;Y<N;++Y)TerrainPerimeter.Add(Vertices[Y*(N+1)+N]);
        for(int32 X=N;X>0;--X)TerrainPerimeter.Add(Vertices[N*(N+1)+X]);
        for(int32 Y=N;Y>0;--Y)TerrainPerimeter.Add(Vertices[Y*(N+1)]);
        // Eight cached painter orders avoid sorting terrain triangles during rotating-map paints.
        HeadingIndices.SetNum(8);TArray<int32> Triangles;
        TArray<FVector> TriangleCenters;TArray<double> Depths;
        Triangles.Reserve(Indices.Num()/3);TriangleCenters.Reserve(Indices.Num()/3);Depths.SetNumUninitialized(Indices.Num()/3);
        for(int32 I=0;I<Indices.Num();I+=3)
        {
            Triangles.Add(I);
            // Retain the exact sum and comparison used by the painter sort.
            // Neither normalize nor subtract the anchor: rounding changes ties.
            TriangleCenters.Add(Vertices[Indices[I]]+Vertices[Indices[I+1]]+Vertices[Indices[I+2]]);
        }
        for(int32 Direction=0;Direction<8;++Direction)
        {
            const double Angle=Direction*UE_PI/4;const FVector View(FMath::Cos(Angle),FMath::Sin(Angle),0);
            double* const DepthData=Depths.GetData();
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMap_DepthKeys);
                const FVector* const Centers=TriangleCenters.GetData();
                for(int32 I=0;I<TriangleCenters.Num();++I)DepthData[I]=FVector::DotProduct(Centers[I],View);
            }
            // Preserve the prior direction's input permutation, the comparator,
            // and the same sort, including all equal-depth triangle ordering.
            // These offsets were generated above and the arrays cannot resize
            // during sorting; avoid two TArray range checks per comparison.
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMap_SortOrder);
                Triangles.Sort([DepthData](int32 A,int32 B){return DepthData[A/3]>DepthData[B/3];});
            }
            {
                TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMap_WriteOrder);
                auto& Order=HeadingIndices[Direction];Order.SetNumUninitialized(Indices.Num());
                SlateIndex* Target=Order.GetData();const SlateIndex* const Source=Indices.GetData();
                for(int32 I:Triangles){*Target++=Source[I];*Target++=Source[I+1];*Target++=Source[I+2];}
            }
        }
    }
}
void UGuLiGroundMapWidget::CacheMarkers()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMap_Markers);
    MarkerGroups.Reset();if(!Data)return;
    // Spatial aggregation is performed on snapshot submission (5 Hz), never on
    // each rotating-map paint. It operates only on already-visible cached facts.
    const double Span=bFull&&Terrain?FMath::Max(Terrain->Cells.X,Terrain->Cells.Y)*GuLiGrid::CellSize/ZoomFactor:Range;
    const double Cell=Span/(bFull?20.:7.);TMap<FIntVector,int32> Groups;
    for(const auto& Pair:Data->Markers)
    {
        const auto& M=Pair.Value;
        if(M.Key.Kind==EGuLiHybridObjectKind::GroundPlayer&&M.Location.Equals(Data->Position,1.))continue;
        const bool Individual=M.Key==Selected||M.Key.Kind==EGuLiHybridObjectKind::GroundPlayer||M.Key.Kind==EGuLiHybridObjectKind::Territory||M.Key.Kind==EGuLiHybridObjectKind::Building;
        // Units share one faction symbol; combining their kinds prevents three
        // identical overlapping discs for a mixed squad in the same cell.
        const FIntVector Key(FMath::FloorToInt(M.Location.X/Cell),FMath::FloorToInt(M.Location.Y/Cell),int32(M.Team));
        if(!Individual)if(const auto* Found=Groups.Find(Key))
        {auto& Group=MarkerGroups[*Found];Group.Marker.Location=(Group.Marker.Location*Group.Count+M.Location)/(Group.Count+1);++Group.Count;continue;}
        const int32 Index=MarkerGroups.Add({M,1});if(!Individual)Groups.Add(Key,Index);
    }
}
FVector2D UGuLiGroundMapWidget::Project(const FVector& P,FVector2D Size) const
{
    const double Span=bFull&&Terrain?FMath::Max(Terrain->Cells.X,Terrain->Cells.Y)*GuLiGrid::CellSize/ZoomFactor:Range;
    const double Scale=FMath::Min(Size.X,Size.Y)/FMath::Max(1.,Span);
    const FVector D=P-(bFull?FVector(MapPan,0):Center);
    const double R=bFull?0:FMath::DegreesToRadians(double(Heading)),C=FMath::Cos(R),S=FMath::Sin(R);
    return Size*FVector2D(.5,bFull?.5:.61)+FVector2D(-S*D.X+C*D.Y,(-C*D.X-S*D.Y)*(bFull?1.:.62)-(bFull?0:D.Z*HeightScale))*Scale;
}
int32 UGuLiGroundMapWidget::Paint(const FGeometry& G,FSlateWindowElementList& Out,int32 L) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMap_Paint);
    const FVector2D Size=G.GetLocalSize();const auto Transform=G.GetAccumulatedRenderTransform();
    const auto* White=FCoreStyle::Get().GetBrush("WhiteBrush");
    FSlateDrawElement::MakeBox(Out,L,G.ToPaintGeometry(),White,ESlateDrawEffect::None,PanelColor);
    if(!HasTerrain()||!Data)return L;
    // Every point in this paint shares the same view. Keep the original
    // operation order while computing scale and trigonometry only once.
    const double ProjectionSpan=bFull?FMath::Max(Terrain->Cells.X,Terrain->Cells.Y)*GuLiGrid::CellSize/ZoomFactor:Range;
    const double ProjectionScale=FMath::Min(Size.X,Size.Y)/FMath::Max(1.,ProjectionSpan);
    const FVector ProjectionCenter=bFull?FVector(MapPan,0):Center;
    const double ProjectionAngle=bFull?0:FMath::DegreesToRadians(double(Heading));
    const double ProjectionCos=FMath::Cos(ProjectionAngle),ProjectionSin=FMath::Sin(ProjectionAngle);
    auto Project=[&](const FVector& P,FVector2D)
    {
        const FVector D=P-ProjectionCenter;
        return Size*FVector2D(.5,bFull?.5:.61)+FVector2D(-ProjectionSin*D.X+ProjectionCos*D.Y,
            (-ProjectionCos*D.X-ProjectionSin*D.Y)*(bFull?1.:.62)-(bFull?0:D.Z*HeightScale))*ProjectionScale;
    };
    const auto Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
    auto& V=PaintVertices;auto& I=PaintIndices;V.Reset();I.Reset();
    V.Reserve(FMath::Max(Vertices.Num(),4096));
    bool CacheTerrainPaint=false;int32 FixedTerrainVertices=0;
    auto Flush=[&]()
    {
        if(!I.IsEmpty())
        {
            if(CacheTerrainPaint)
            {auto& Batch=TerrainPaintBatches.AddDefaulted_GetRef();Batch.Vertices=V;Batch.Indices=I;Batch.FixedVertices=FixedTerrainVertices;FixedTerrainVertices=0;}
            FSlateDrawElement::MakeCustomVerts(Out,++L,Resource,V,I,nullptr,0,0);
        }
        V.Reset();I.Reset();
    };
    auto Stroke=[&](FVector2D A,FVector2D B,FVector2D UnitNormal,FColor Solid,FColor Transparent,float Width)
    {
        const FVector2D N=UnitNormal*(Width*.5),Outer=UnitNormal*(Width*.5+.7);
        if(V.Num()+8>MAX_uint16)Flush();
        const SlateIndex Base=SlateIndex(V.Num());
        int32 K=0;for(const auto P:{A+Outer,B+Outer,B+N,A+N,A-N,B-N,B-Outer,A-Outer})
        {const auto C=K==0||K==1||K==6||K==7?Transparent:Solid;++K;V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,FVector2f(P),FVector2f::ZeroVector,C));}
        for(int32 J:{0,1,2,0,2,3,3,2,5,3,5,4,4,5,6,4,6,7})I.Add(Base+J);
    };
    auto Line=[&](FVector2D A,FVector2D B,FLinearColor Color,float Width=1.f)
    {
        const FVector2D D=(B-A).GetSafeNormal();
        Stroke(A,B,FVector2D(-D.Y,D.X),Color.ToFColor(true),Color.CopyWithNewOpacity(0).ToFColor(true),Width);
    };
    auto Polygon=[&](TConstArrayView<FVector2D> Points,FLinearColor Tint)
    {
        if(Points.Num()<3)return;if(V.Num()+Points.Num()>MAX_uint16)Flush();const int32 Base=V.Num();
        const FColor Color=Tint.ToFColor(true);
        for(const auto& P:Points)V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,FVector2f(P),FVector2f::ZeroVector,Color));
        for(int32 J=1;J+1<Points.Num();++J){I.Add(SlateIndex(Base));I.Add(SlateIndex(Base+J));I.Add(SlateIndex(Base+J+1));}
    };
    static const TArray<FVector2D> CirclePoints=[]
    {TArray<FVector2D> Points;for(int32 J=0;J<=20;++J){const double A=J*UE_PI/10;Points.Add({FMath::Cos(A),FMath::Sin(A)});}return Points;}();
    static const TArray<FVector2D> CircleNormals=[]
    {TArray<FVector2D> Normals;for(int32 J=0;J<20;++J){const auto D=(CirclePoints[J+1]-CirclePoints[J]).GetSafeNormal();Normals.Add({-D.Y,D.X});}return Normals;}();
    auto Circle=[&](FVector2D P,float Radius,FLinearColor Tint,float Width)
    {
        const FColor Solid=Tint.ToFColor(true),Transparent=Tint.CopyWithNewOpacity(0).ToFColor(true);
        for(int32 J=0;J<20;++J)Stroke(P+CirclePoints[J]*Radius,P+CirclePoints[J+1]*Radius,CircleNormals[J],Solid,Transparent,Width);
    };
    const auto Edge=GuLiGroundUI::Light(LineColor);
    const bool ReuseTerrain=!bFull && !TerrainPaintBatches.IsEmpty() && TerrainPaintHeading==Heading
        && TerrainPaintSize==Size && TerrainPaintTransform==Transform;
    if(ReuseTerrain)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiGroundMap_TranslateTerrain);
        // With the same patch, heading, scale and widget transform, movement
        // changes every terrain point by one translation. Do not retessellate
        // contours, antialias fringes or the cutaway on a position-only update.
        const FVector2f Shift=TransformVector(Transform,FVector2f(Project(TerrainPaintCenter,Size)-Project(ProjectionCenter,Size)));
        for(auto& Batch:TerrainPaintBatches)
        {
            // Keep immutable base positions: no cumulative float drift and no
            // full vertex/color/UV copy before Slate takes its own frame copy.
            FSlateVertex* Target=Batch.Vertices.GetData();
            const FVector2f* Base=Batch.Positions.GetData();
            for(int32 Index=Batch.FixedVertices;Index<Batch.Vertices.Num();++Index)
            {Target[Index].Position.X=Base[Index].X+Shift.X;Target[Index].Position.Y=Base[Index].Y+Shift.Y;}
            FSlateDrawElement::MakeCustomVerts(Out,++L,Resource,Batch.Vertices,Batch.Indices,nullptr,0,0);
        }
        V.Reset();
    }
    else
    {
    CacheTerrainPaint=!bFull;TerrainPaintBatches.Reset();
    TerrainPaintCenter=ProjectionCenter;TerrainPaintHeading=Heading;TerrainPaintSize=Size;TerrainPaintTransform=Transform;
    const float GridSize=bFull?64.f:28.f;
    for(float X=0;X<Size.X;X+=GridSize)Line({X,0},{X,Size.Y},LineColor.CopyWithNewOpacity(.13f));
    for(float Y=0;Y<Size.Y;Y+=GridSize)Line({0,Y},{Size.X,Y},LineColor.CopyWithNewOpacity(.13f));
    FixedTerrainVertices=V.Num();
    if(!bFull)
    {
        // Actual terrain perimeter connects to a flat cutaway base, rather than
        // floating above an unrelated four-corner slab.
        const double Base=MinimumHeight-FMath::Max(300.,(MaximumHeight-MinimumHeight)*.08);
        TArray<FVector2D> Bottom;
        for(const FVector2D UV:{FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)})
        {const auto XY=PatchOrigin+UV*PatchSpan;Bottom.Add(Project(FVector(XY,Base),Size)+FVector2D(0,7));}
        Polygon(Bottom,RaisedColor.CopyWithNewOpacity(.95f));
        for(int32 J=0;J<TerrainPerimeter.Num();++J)
        {
            const FVector A=TerrainPerimeter[J],B=TerrainPerimeter[(J+1)%TerrainPerimeter.Num()];
            const auto A0=Project(FVector(A.X,A.Y,Base),Size)+FVector2D(0,7),B0=Project(FVector(B.X,B.Y,Base),Size)+FVector2D(0,7);
            const auto A1=Project(A,Size),B1=Project(B,Size);
            Polygon({A1,B1,B0,A0},FMath::Lerp(PanelColor,RaisedColor,(J<TerrainPerimeter.Num()/2)?.7f:.35f));
            Line(A1,B1,LineColor.CopyWithNewOpacity(.7f),.8f);
            Line(A0,B0,AccentColor.CopyWithNewOpacity(.65f),1.2f);
            if(J%8==0)Line(A1,A0,SecondaryColor.CopyWithNewOpacity(.3f),.7f);
        }
    }
    Flush();
    if(bFull)
    {
        const FVector2D Extent=FVector2D(Terrain->Cells)*GuLiGrid::CellSize;
        for(const FVector2D UV:{FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)})
            V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,FVector2f(Project(FVector(Terrain->Origin+UV*Extent,0),Size)),FVector2f(UV),FColor::White));
        const TArray<SlateIndex> Quad{0,1,2,0,2,3};
        FSlateDrawElement::MakeCustomVerts(Out,++L,FSlateApplication::Get().GetRenderer()->GetResourceHandle(PlanarTerrainBrush),V,Quad,nullptr,0,0);
    }
    else
    {
        for(int32 Index=0;Index<Vertices.Num();++Index)
            V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,FVector2f(Project(Vertices[Index],Size)),FVector2f::ZeroVector,TerrainColors[Index]));
        const int32 Order=FMath::RoundToInt(FMath::UnwindDegrees(Heading)/45.f+8.f)%8;
        const auto& DrawIndices=HeadingIndices.IsValidIndex(Order)?HeadingIndices[Order]:Indices;
        if(!DrawIndices.IsEmpty())
        {
            if(CacheTerrainPaint){auto& Batch=TerrainPaintBatches.AddDefaulted_GetRef();Batch.Vertices=V;Batch.Indices=DrawIndices;}
            FSlateDrawElement::MakeCustomVerts(Out,++L,Resource,V,DrawIndices,nullptr,0,0);
        }
    }
    V.Reset();
    for(const auto& C:Contours)
    {
        const bool Major=FMath::RoundToInt(C.Key.Z/ContourStep)%5==0;
        Line(Project(C.Key,Size),Project(C.Value,Size),Edge.CopyWithNewOpacity(Major?.8f:.48f),Major?1.9f:1.4f);
    }
    Flush();
    CacheTerrainPaint=false;
    for(auto& Batch:TerrainPaintBatches)
    {Batch.Positions.Reserve(Batch.Vertices.Num());for(const auto& Vertex:Batch.Vertices)Batch.Positions.Add(Vertex.Position);}
    }
    // Gameplay building bounds form a separate, subdued structure layer. Only
    // authoritative visible membership is used; no Actor or mesh scan is needed.
    for(const auto& Group:MarkerGroups)
    {
        const auto& M=Group.Marker;if(M.BuildingExtent.X<=0||M.BuildingExtent.Y<=0)continue;
        const auto P=Project(M.Location,Size);if(P.X<-40||P.Y<-40||P.X>Size.X+40||P.Y>Size.Y+40)continue;
        const auto Tint=GuLiLocalTeamColors::GetUI(M.Team,Data->Team);
        TArray<FVector2D> Base,Roof;Base.Reserve(4);Roof.Reserve(4);
        const FRotator Rotation(0,M.Yaw,0);
        for(const FVector2D Corner:{FVector2D(-1,-1),FVector2D(1,-1),FVector2D(1,1),FVector2D(-1,1)})
        {
            const auto Position=M.Location+Rotation.RotateVector(FVector(Corner.X*M.BuildingExtent.X,Corner.Y*M.BuildingExtent.Y,0));
            Base.Add(Project(Position,Size));Roof.Add(Project(Position+FVector(0,0,bFull?0:2*M.BuildingExtent.Z),Size));
        }
        if(!bFull)for(int32 J=0;J<4;++J)
        {Polygon({Base[J],Base[(J+1)%4],Roof[(J+1)%4],Roof[J]},RaisedColor.CopyWithNewOpacity(.92f));Line(Base[J],Roof[J],Tint.CopyWithNewOpacity(.55f),.8f);}
        Polygon(Roof,FMath::Lerp(RaisedColor,Tint,.16f));
        for(int32 J=0;J<4;++J)Line(Roof[J],Roof[(J+1)%4],Tint.CopyWithNewOpacity(.85f),1.3f);
    }
    Flush();
    for(const auto& Group:MarkerGroups)
    {
        const auto& M=Group.Marker;const auto P=Project(M.Location,Size);if(P.X<4||P.Y<4||P.X>Size.X-4||P.Y>Size.Y-4)continue;
        const bool Point=M.Key.Kind==EGuLiHybridObjectKind::Territory;
        const auto Tint=Point?AccentColor:GuLiLocalTeamColors::GetUI(M.Team,Data->Team);
        const bool Building=!M.BuildingExtent.IsNearlyZero();
        const float R=(bFull?(Point?10:Building?4:7):(Point?7:Building?3:5))+(Point?0:FMath::Min(2.f,FMath::Sqrt(float(Group.Count-1))*.5f));
        if(Point){const TArray<FVector2D> Q{P+FVector2D(0,-R),P+FVector2D(R,0),P+FVector2D(0,R),P+FVector2D(-R,0)};Polygon(Q,PanelColor.CopyWithNewOpacity(.9f));for(int32 J=0;J<4;++J){Line(Q[J],Q[(J+1)%4],Tint.CopyWithNewOpacity(.18f),5);Line(Q[J],Q[(J+1)%4],GuLiGroundUI::Light(Tint,1.3f),2);}}
        else if(M.Team==Data->Team)
        {
            TArray<FVector2D,TInlineAllocator<20>> Disc;for(int32 J=0;J<20;++J)Disc.Add(P+CirclePoints[J]*R);
            Circle(P,R,Tint.CopyWithNewOpacity(.15f),5);Polygon(Disc,Tint);Circle(P,R,GuLiGroundUI::Light(SecondaryColor),1.2f);
        }
        else {Polygon({P+FVector2D(0,-R-2),P+FVector2D(R+2,R*.75+1),P+FVector2D(-R-2,R*.75+1)},PanelColor);Polygon({P+FVector2D(0,-R),P+FVector2D(R,R*.75),P+FVector2D(-R,R*.75)},Tint);}
        if(M.Key==Selected){Circle(P,R+8,AccentColor,1.5f);Line(P+FVector2D(-R-13,0),P+FVector2D(-R-7,0),AccentColor,2);Line(P+FVector2D(R+7,0),P+FVector2D(R+13,0),AccentColor,2);}
    }
    const auto P=Project(Data->Position,Size);const auto Self=FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("FEE4D9")));
    const double Angle=FMath::DegreesToRadians(double(Data->PlayerYaw)-(bFull?0.:Heading));
    const FVector2D Forward(FMath::Sin(Angle),-FMath::Cos(Angle)),Right(-Forward.Y,Forward.X);
    Polygon({P,P+Forward*61-Right*35,P+Forward*61+Right*35},LineColor.CopyWithNewOpacity(.22f));
    const FVector2D Tip=P+Forward*22,Left=P-Forward*12-Right*12,RightPoint=P-Forward*12+Right*12;
    Polygon({Tip,Left,P-Forward*4,RightPoint},Self);Line(Tip,Left,Edge,1.8f);Line(Tip,RightPoint,Edge,1.8f);
    // North pointer is independent from the map's rotating camera heading.
    const double R=FMath::DegreesToRadians(bFull?0.:double(Heading));const FVector2D North(-FMath::Sin(R),-FMath::Cos(R));
    const FVector2D N(bFull?55:Size.X-24,bFull?30:24);Line(N-North*7,N+North*9,SecondaryColor,2);
    Line(N+North*9,N+North*3+FVector2D(-North.Y,North.X)*4,SecondaryColor,2);
    if(bFull)Polygon({N+FVector2D(0,-13),N+FVector2D(-10,5),N,N+FVector2D(10,5)},AccentColor);
    Flush();
    FSlateDrawElement::MakeText(Out,++L,G.ToPaintGeometry(FVector2D(30,28),FSlateLayoutTransform(bFull?FVector2D(43,44):FVector2D(Size.X-52,15))),TEXT("N"),FCoreStyle::GetDefaultFontStyle("Bold",bFull?24:14),ESlateDrawEffect::None,Self);
    if(bFull)
    {
        const double Span=FMath::Max(Terrain->Cells.X,Terrain->Cells.Y)*GuLiGrid::CellSize/ZoomFactor;
        const double PixelsPerMeter=100*FMath::Min(Size.X,Size.Y)/Span;
        const double Rough=240/FMath::Max(.001,PixelsPerMeter),Power=FMath::Pow(10.,FMath::FloorToDouble(FMath::LogX(10.,Rough)));
        const double Meters=(Rough/Power>=5?5:Rough/Power>=2?2:1)*Power;const float Width=Meters*PixelsPerMeter;
        const FVector2D A(46,Size.Y-29),B=A+FVector2D(Width,0);
        const TArray<FVector2D> Plate{{A.X-22,A.Y-46},{B.X+25,A.Y-46},{B.X+38,A.Y-33},{B.X+38,A.Y+15},{A.X-9,A.Y+15},{A.X-22,A.Y+2}};
        Polygon(Plate,PanelColor.CopyWithNewOpacity(.96f));for(int32 J=0;J<Plate.Num();++J)Line(Plate[J],Plate[(J+1)%Plate.Num()],Edge.CopyWithNewOpacity(.7f));
        Line(A,B,Edge,4);for(int32 J=0;J<3;++J){const auto Tick=FMath::Lerp(A,B,J*.5);Line(Tick-FVector2D(0,7),Tick+FVector2D(0,2),Self,2);}Flush();
        for(int32 J=0;J<3;++J)
        {
            const FString Label=FString::Printf(TEXT("%.0f%s"),Meters*J*.5,J==2?TEXT(" m"):TEXT(""));
            FSlateDrawElement::MakeText(Out,++L,G.ToPaintGeometry(FVector2D(80,22),FSlateLayoutTransform(FMath::Lerp(A,B,J*.5)-FVector2D(J?18:0,34))),Label,FCoreStyle::GetDefaultFontStyle("Regular",16),ESlateDrawEffect::None,Self);
        }
    }
    return L;
}
void UGuLiGroundMapWidget::Pan(FVector2D D,FVector2D Size)
{
    if(!Terrain)return;const double Scale=FMath::Max(Terrain->Cells.X,Terrain->Cells.Y)*GuLiGrid::CellSize/ZoomFactor/FMath::Max(1.,FMath::Min(Size.X,Size.Y));
    MapPan+=FVector2D(D.Y,-D.X)*Scale;
    const auto Max=Terrain->Origin+FVector2D(Terrain->Cells)*GuLiGrid::CellSize;MapPan.X=FMath::Clamp(MapPan.X,Terrain->Origin.X,Max.X);MapPan.Y=FMath::Clamp(MapPan.Y,Terrain->Origin.Y,Max.Y);
    if(Surface)Surface->Invalidate(EInvalidateWidgetReason::Paint);
}
void UGuLiGroundMapWidget::Zoom(float D){ZoomFactor=FMath::Clamp(ZoomFactor*FMath::Pow(1.2,double(D)),1.,8.);CacheMarkers();if(Surface)Surface->Invalidate(EInvalidateWidgetReason::Paint);}
void UGuLiGroundMapWidget::Select(FVector2D P,FVector2D Size)
{
    if(!Data)return;const FGuLiGroundHUDMarker* Best=nullptr;double Dist=324;
    for(const auto& Group:MarkerGroups){const double D=FVector2D::DistSquared(Project(Group.Marker.Location,Size),P);if(D<Dist){Dist=D;Best=Data->Markers.Find(Group.Marker.Key);}}
    Selected=Best?Best->Key:FGuLiHybridObjectKey{};OnSelected.Broadcast(Best);CacheMarkers();if(Surface)Surface->Invalidate(EInvalidateWidgetReason::Paint);
}
