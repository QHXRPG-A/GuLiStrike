#include "Gameplay/Ship/UI/GuLiXuntianSceneGuide.h"
#include "Gameplay/Ship/Aiming/GuLiXuntianGunnery.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMeshBuilder.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "MeshElementCollector.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "RenderingThread.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "UObject/ConstructorHelpers.h"

CSV_DEFINE_CATEGORY(GuLiXuntianCoverage,true);

namespace
{
constexpr double TerrainLift=12.;
FIntPoint GroundCellCenter(const FVector& Origin,int32 Snap)
{
    return FIntPoint(FMath::RoundToInt(Origin.X/Snap)*Snap,FMath::RoundToInt(Origin.Y/Snap)*Snap);
}
const FLinearColor Air(FColor::FromHex(TEXT("6AA4BE")));
const FLinearColor Amber(FColor::FromHex(TEXT("EE9D58")));
const FLinearColor Closed(FColor::FromHex(TEXT("EABCBA")));

using EAirBadge = EGuLiXuntianAirBadge;

// Positions use an eye-relative origin to retain subpixel precision. UV0 is measured in
// design pixels and UV1 identifies the shape drawn by the shared analytic ink material.
struct FMarkerView
{
    FVector Right, Up, Forward;
    double PixelAtUnitDepth;

    FVector2D Project(const FVector& Relative, double Depth) const
    {
        return FVector2D(Relative.Dot(Right),Relative.Dot(Up))/(Depth*PixelAtUnitDepth);
    }
};

void AddInkQuad(FDynamicMeshBuilder& Mesh, const FVector& A, const FVector& B, const FVector& HalfA, const FVector& HalfB,
                float HalfWidth, float StartUV, float EndUV, FVector2f Style, FColor Ink)
{
    auto Add = [&](const FVector& Position, float X, float Y)
    {
        FDynamicMeshVertex Vertex(FVector3f(Position),FVector2f(X,Y),Ink);
        Vertex.TextureCoordinate[1] = Style;
        return Mesh.AddVertex(Vertex);
    };
    const int32 V0=Add(A-HalfA,-HalfWidth,StartUV), V1=Add(A+HalfA,HalfWidth,StartUV);
    const int32 V2=Add(B+HalfB,HalfWidth,EndUV), V3=Add(B-HalfB,-HalfWidth,EndUV);
    Mesh.AddTriangle(V0,V1,V2);
    Mesh.AddTriangle(V0,V2,V3);
}

void AddAirMarker(FDynamicMeshBuilder& Mesh, const FGuLiXuntianAirMarker& Marker, const FVector& RelativeCenter,
                  double HullHeight, double Depth, const FMarkerView& View)
{
    const bool bClosed=Marker.Badge==EAirBadge::Closed, bBelly=Marker.Badge==EAirBadge::Belly;
    const FColor Ink=(bClosed ? Closed : bBelly ? Amber : Air).ToFColor(false);
    const double Pixel=Depth*View.PixelAtUnitDepth;
    const FVector Top=RelativeCenter-View.Right*(View.Right.GetAbs().Dot(Marker.Extent)+8*Pixel);
    const FVector Center=Top-View.Right*(15*Pixel), X=View.Right*(12*Pixel), Y=View.Up*((bBelly?1.:-1.)*12*Pixel);
    AddInkQuad(Mesh,Center-Y,Center+Y,X,X,12,-12,12,FVector2f(bClosed?1.f:0.f,0),Ink);

    const double RelativeHullHeight=RelativeCenter.Z+(HullHeight-Marker.Center.Z);
    FVector Foot(Top.X,Top.Y,RelativeHullHeight);
    double FootDepth=Foot.Dot(View.Forward);
    if (FootDepth>1)
    {
        const double FootPixel=FootDepth*View.PixelAtUnitDepth;
        const FVector Cap=View.Right*(7*FootPixel), Width=View.Up*(3*FootPixel);
        AddInkQuad(Mesh,Foot-Cap,Foot+Cap,Width,Width,3,-7,7,FVector2f(3,0),Ink);
    }
    else
    {
        Foot=FMath::Lerp(Top,Foot,(Depth-1)/(Depth-FootDepth));
        FootDepth=1;
    }
    const FVector2D Segment=View.Project(Foot,FootDepth)-View.Project(Top,Depth);
    const double Length=Segment.Size();
    if (Length<1) return;
    const FVector Side=(View.Up*Segment.X-View.Right*Segment.Y)*(3*View.PixelAtUnitDepth/Length);
    // The pitch stays fixed until the continuous 24-dash limit is reached.
    AddInkQuad(Mesh,Top,Foot,Side*Depth,Side*FootDepth,3,0,float(Length),
               FVector2f(2,float(FMath::Max(9.,Length/24.))),Ink);
}

// Pilot marks use the regular depth-tested geometry pass, including in non-debug builds.
class FPilotMarkProxy final : public FPrimitiveSceneProxy
{
public:
    FPilotMarkProxy(const UGuLiXuntianSceneGuide& Guide, const TArray<FGuLiXuntianAirMarker>& AirMarkers,
                    double InHeight, double InReach, UMaterialInterface* Ink)
        : FPrimitiveSceneProxy(&Guide), Marks(Guide.BatchedLines), Markers(AirMarkers), HullHeight(InHeight), Reach(InReach)
    {
        bWillEverBeLit=false;
        if (Ink)
        {
            InkProxy=Ink->GetRenderProxy();
            InkRelevance=Ink->GetRelevance_Concurrent(GetScene().GetShaderPlatform());
        }
    }
    virtual SIZE_T GetTypeHash() const override { static uint8 Type; return reinterpret_cast<SIZE_T>(&Type); }
    void Update(TArray<FGuLiXuntianAirMarker>&& InMarkers, double InHeight, double InReach,
                TOptional<TArray<FBatchedLine>>&& SurveyLines)
    {
        Markers = MoveTemp(InMarkers);
        HullHeight = InHeight;
        Reach = InReach;
        if (SurveyLines) Marks = MoveTemp(SurveyLines.GetValue());
    }
    virtual uint32 GetMemoryFootprint() const override { return sizeof(*this)+Marks.GetAllocatedSize()+Markers.GetAllocatedSize(); }
    virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
    {
        FPrimitiveViewRelevance R;
        InkRelevance.SetPrimitiveViewRelevance(R);
        R.bDrawRelevance=IsShown(View); R.bDynamicRelevance=true; R.bNormalTranslucency=true;
        R.bRenderInMainPass=ShouldRenderInMainPass();
        return R;
    }
    virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views,const FSceneViewFamily& Family,
                                         uint32 Visible,FMeshElementCollector& Collector) const override
    {
        for (int32 I=0;I<Views.Num();++I)
        {
            if (!(Visible & (1u<<I))) continue;
            const FSceneView& View = *Views[I];
            const FIntRect Rect=View.UnscaledViewRect;
            const float Scale=FMath::Min(Rect.Width()/1920.f,Rect.Height()/1080.f);
            auto* PDI=Collector.GetPDI(I);
            for (const auto& M : Marks)
                PDI->DrawTranslucentLine(M.Start,M.End,M.Color,SDPG_World,M.Thickness*Scale,0,true);
            const double ProjY = View.ViewMatrices.GetProjectionMatrix().M[1][1];
            if (!InkProxy || Markers.IsEmpty() || Scale<=0 || ProjY<=0) continue;
            const FMarkerView MarkerView{View.GetViewRight(),View.GetViewUp(),View.GetViewDirection(),2.*Scale/(Rect.Height()*ProjY)};
            const FVector Eye = View.ViewMatrices.GetViewOrigin();
            FDynamicMeshBuilder Mesh(View.GetFeatureLevel(),2);
            Mesh.ReserveVertices(12*Markers.Num());
            Mesh.ReserveTriangles(6*Markers.Num());
            int32 BadgeCount=0, CrossCount=0;
            for (const auto& Marker : Markers)
            {
                const FVector Offset = Marker.Center-Eye;
                const double Depth = Offset.Dot(MarkerView.Forward);
                if (Depth>1 && Offset.SizeSquared()<=FMath::Square(Reach))
                {
                    AddAirMarker(Mesh,Marker,Offset,HullHeight,Depth,MarkerView);
                    ++BadgeCount;
                    CrossCount+=Marker.Badge==EAirBadge::Closed;
                }
            }
            if (BadgeCount)
            {
                Mesh.GetMesh(FTranslationMatrix(Eye),InkProxy,SDPG_World,true,false,false,I,Collector);
                CSV_CUSTOM_STAT(GuLiXuntianCoverage,AirGunBadges,BadgeCount-CrossCount,ECsvCustomStatOp::Accumulate);
                CSV_CUSTOM_STAT(GuLiXuntianCoverage,AirCrossBadges,CrossCount,ECsvCustomStatOp::Accumulate);
                CSV_CUSTOM_STAT(GuLiXuntianCoverage,AirBadgeBatches,1,ECsvCustomStatOp::Accumulate);
            }
        }
    }
private:
    TArray<FBatchedLine> Marks;
    TArray<FGuLiXuntianAirMarker> Markers;
    const FMaterialRenderProxy* InkProxy=nullptr;
    FMaterialRelevance InkRelevance;
    double HullHeight, Reach;
};

void PrepareGuideMesh(UPrimitiveComponent& Mesh, USceneComponent& Parent)
{
    Mesh.SetupAttachment(&Parent);
    Mesh.SetAbsolute(true,true,true);
    Mesh.SetOnlyOwnerSee(true);
    Mesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh.SetGenerateOverlapEvents(false);
    Mesh.SetCanEverAffectNavigation(false);
    Mesh.SetCastShadow(false);
    Mesh.SetReceivesDecals(false);
    Mesh.bVisibleInRayTracing=false;
    Mesh.bVisibleInReflectionCaptures=false;
}
void Line(TArray<FBatchedLine>& Lines, const FVector& From, const FVector& To, FLinearColor Color, float Width)
{
    Lines.Emplace(From,To,Color,-1,Width,SDPG_World);
}
}

UGuLiXuntianSceneGuide::UGuLiXuntianSceneGuide()
{
    SetOnlyOwnerSee(true);
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetCanEverAffectNavigation(false);
    bCalculateAccurateBounds=false;
    PrimaryComponentTick.bCanEverTick=false;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Ground(TEXT("/Game/GuLiStrike/Ship/UI/Xuntian/M_XuntianCoverage"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> AirGuide(TEXT("/Game/GuLiStrike/Ship/UI/Xuntian/M_XuntianAirGuide"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Badge(TEXT("/Game/GuLiStrike/Ship/UI/Xuntian/M_XuntianAirMarker"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
    GroundInk=Ground.Object; SkyInk=AirGuide.Object; DomeMesh=Sphere.Object;
    MarkerInk=Badge.Object;
}

FPrimitiveSceneProxy* UGuLiXuntianSceneGuide::CreateSceneProxy()
{
    return new FPilotMarkProxy(*this,AirMarkers,MarkerHullHeight,MarkerReach,MarkerInk);
}

void UGuLiXuntianSceneGuide::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
    Super::GetUsedMaterials(OutMaterials,bGetDebugMaterials);
    if (MarkerInk) OutMaterials.Add(MarkerInk);
}

void UGuLiXuntianSceneGuide::SendRenderDynamicData_Concurrent()
{
    Super::SendRenderDynamicData_Concurrent();
    if (auto* Proxy = static_cast<FPilotMarkProxy*>(GetSceneProxy()))
    {
        TOptional<TArray<FBatchedLine>> Lines;
        if (bSurveyLinesDirty) Lines.Emplace(BatchedLines);
        bSurveyLinesDirty = false;
        ENQUEUE_RENDER_COMMAND(GuLiXuntianUpdatePilotMarks)(
            [Proxy, Markers=AirMarkers, Height=MarkerHullHeight, Reach=MarkerReach, Lines=MoveTemp(Lines)]
            (FRHICommandListImmediate&) mutable
            {
                Proxy->Update(MoveTemp(Markers),Height,Reach,MoveTemp(Lines));
            });
    }
}

void UGuLiXuntianSceneGuide::OnRegister()
{
    Super::OnRegister();
    if (IsTemplate() || !GetWorld() || !GetWorld()->IsGameWorld() || GetWorld()->IsNetMode(NM_DedicatedServer)) return;
    FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.AddUObject(this,&ThisClass::UpdateAirMarkersBeforeRender);
    Surface=NewObject<UProceduralMeshComponent>(GetOwner(),NAME_None,RF_Transient);
    PrepareGuideMesh(*Surface,*this);
    Surface->SetTranslucentSortPriority(-5);
    Surface->SetMaterial(0,GroundInk);
    Surface->RegisterComponent();
    Sky=NewObject<UStaticMeshComponent>(GetOwner(),NAME_None,RF_Transient);
    PrepareGuideMesh(*Sky,*this);
    Sky->SetStaticMesh(DomeMesh);
    Sky->SetMaterial(0,SkyInk);
    Sky->SetTranslucentSortPriority(-20);
    Sky->SetWorldScale3D(FVector(6400));
    Sky->RegisterComponent();
    bSurveyPending=true;
}

void UGuLiXuntianSceneGuide::OnUnregister()
{
    FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.RemoveAll(this);
    CoverageTask={}; // An outstanding job owns its immutable inputs and cannot touch this component.
    GroundPatch.Reset();
    DisplayedPatch.Reset();
    Terrain.Reset();
    MissingSurfaceKeys.Reset();
    AirContacts.Reset();
    AirMarkers.Reset();
    BatchedLines.Reset();
    AirContactsRevision=MAX_uint64;
    bSurveyLinesDirty=false;
    bAirUpdatePending=bCheckAirCoverage=false;
    NextSurvey=NextMissingSurfaceRetry=0;
    LastBellyCount=INDEX_NONE;
    if (IsValid(Sky)) Sky->DestroyComponent();
    if (IsValid(Surface)) Surface->DestroyComponent();
    Sky=nullptr; Surface=nullptr;
    Super::OnUnregister();
}

void UGuLiXuntianSceneGuide::SurveyGround(const UGuLiXuntianGunnery& Aim, const FVector& Origin, int32 BellyCount)
{
    if (!Surface || CoverageTask.IsValid()) return;
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiXuntian_SurveyGround);
    const auto* Ship=CastChecked<AGuLiStrikeShip>(GetOwner());
    const FQuat Rotation=Ship->GetActorQuat();
    const double Now=GetWorld()->GetTimeSeconds();
    if (Now>=NextMissingSurfaceRetry && MissingSurfaceKeys.IsEmpty())
    {
        NextMissingSurfaceRetry=Now+5;
        for (const auto& Entry : Terrain)
            if (!Entry.Value.bValid) MissingSurfaceKeys.Add(Entry.Key);
    }
    const FIntPoint Fine=GroundCellCenter(Origin,3200), Coarse=GroundCellCenter(Origin,12800);
    int32 TraceBudget=180;
    const bool FootprintChanged=!GroundPatch || Fine!=FineCenter || Coarse!=CoarseCenter;
    if (!FootprintChanged && !bSurveyPending && !MissingSurfaceKeys.IsEmpty())
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiXuntian_SurveyMissingSurface);
        while (TraceBudget>0 && !MissingSurfaceKeys.IsEmpty())
        {
            const FIntPoint Key=MissingSurfaceKeys.Pop(EAllowShrinking::No);
            FSample* Sample=Terrain.Find(Key);
            if (!Sample || Sample->bValid) continue;
            --TraceBudget;
            Sample->bValid=Aim.SurfaceAt(FVector(Key.X,Key.Y,Origin.Z),Sample->Point);
            bSurveyPending|=Sample->bValid;
        }
    }
    const bool Rebuild=bSurveyPending || FootprintChanged;
    const bool Moved=FVector::DistSquared(LastPosition,Origin)>FMath::Square(60.) ||
        LastOrientation.AngularDistance(Rotation)>.005;
    const bool CoverageChanged=Moved || BellyCount!=LastBellyCount || LastLoadout!=Ship->GetLoadoutRevision() ||
        LastMin!=Aim.GroundMinDepression || LastMax!=Aim.GroundMaxDepression;
    if (!Rebuild && !CoverageChanged) return;

    if (Rebuild)
    {
        const auto PreviousPatch=GroundPatch;
        FineCenter=Fine; CoarseCenter=Coarse;
        RebuildGroundPatch(Aim,Origin,TraceBudget);
        if (GroundPatch==PreviousPatch && !CoverageChanged) return;
    }
    LastBellyCount=BellyCount; LastLoadout=Ship->GetLoadoutRevision();
    LastPosition=Origin; LastOrientation=Rotation;
    LastMin=Aim.GroundMinDepression; LastMax=Aim.GroundMaxDepression;
    if (GroundPatch->Indices.IsEmpty())
    {
        if (!bSurveyPending && DisplayedPatch) { Surface->ClearMeshSection(0); DisplayedPatch.Reset(); }
        return;
    }
    CoverageStarted=FPlatformTime::Seconds();
    CoverageTask=UE::Tasks::Launch(UE_SOURCE_LOCATION,[Patch=GroundPatch,Coverage=Aim.SnapshotGroundCoverage()]
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiXuntian_CoverageSolve);
        TArray<FVector2D> UV;
        UV.Reserve(Patch->Vertices.Num());
        const double HasBand=Coverage.MinDepression<Coverage.MaxDepression ? 1. : 0.;
        for (const FVector& Vertex : Patch->Vertices)
            UV.Emplace(Coverage.MarginAt(Vertex-FVector(0,0,TerrainLift)),HasBand);
        return UV;
    });
}

void UGuLiXuntianSceneGuide::RebuildGroundPatch(const UGuLiXuntianGunnery& Aim, const FVector& Origin, int32& TraceBudget)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiXuntian_SurveyTopology);
    auto Patch=MakeShared<FGroundPatch,ESPMode::ThreadSafe>();
    auto& Vertices=Patch->Vertices;
    auto& Indices=Patch->Indices;
    bSurveyPending=false;
    TMap<FIntPoint,int32> Used;
    auto Vertex=[&](const FIntPoint& Key) -> int32
    {
        if (const int32* Existing=Used.Find(Key)) return *Existing;
        FSample* S=Terrain.Find(Key);
        if (!S)
        {
            if (TraceBudget==0) { bSurveyPending=true; Used.Add(Key,INDEX_NONE); return INDEX_NONE; }
            --TraceBudget;
            S=&Terrain.Add(Key);
            S->bValid=Aim.SurfaceAt(FVector(Key.X,Key.Y,Origin.Z),S->Point);
        }
        if (!S->bValid) { Used.Add(Key,INDEX_NONE); return INDEX_NONE; }
        const int32 I=Vertices.Add(S->Point+FVector(0,0,TerrainLift));
        Used.Add(Key,I);
        return I;
    };
    constexpr int32 Half=16;
    FIntPoint FineMin=FIntPoint::ZeroValue, FineMax=FIntPoint::ZeroValue;
    int32 FineStep=0;
    for (const int32 Step : {800,3200,12800})
    {
        // The fine borders coincide with coarse cell edges. Split adjoining coarse
        // edges at the fine samples so even sloping receivers have a watertight join.
        const FIntPoint Center=Step==800 ? FineCenter : CoarseCenter;
        for (int32 Ring=0;Ring<=Half;++Ring)
        for (int32 X=-Ring;X<=Ring;++X)
        for (int32 Y=-Ring;Y<=Ring;++Y)
        {
            if (X>=Half || Y>=Half || FMath::Max(FMath::Abs(X),FMath::Abs(Y))!=Ring) continue;
            const FIntPoint A=Center+FIntPoint(X*Step,Y*Step);
            if (FineStep && A.X>=FineMin.X && A.Y>=FineMin.Y && A.X+Step<=FineMax.X && A.Y+Step<=FineMax.Y) continue;
            const FIntPoint Corners[]={A,A+FIntPoint(Step,0),A+FIntPoint(Step,Step),A+FIntPoint(0,Step)};
            TArray<int32,TInlineAllocator<16>> Border;
            for (int32 K=0;K<4;++K)
            {
                const FIntPoint S=Corners[K], E=Corners[(K+1)%4];
                Border.Add(Vertex(S));
                const bool AlongX=S.Y==E.Y && (S.Y==FineMin.Y || S.Y==FineMax.Y) &&
                    FMath::Min(S.X,E.X)>=FineMin.X && FMath::Max(S.X,E.X)<=FineMax.X;
                const bool AlongY=S.X==E.X && (S.X==FineMin.X || S.X==FineMax.X) &&
                    FMath::Min(S.Y,E.Y)>=FineMin.Y && FMath::Max(S.Y,E.Y)<=FineMax.Y;
                if (FineStep && (AlongX || AlongY))
                    for (int32 I=1;I<Step/FineStep;++I) Border.Add(Vertex(S+(E-S)*I/(Step/FineStep)));
            }
            if (Border.Contains(INDEX_NONE)) continue;
            if (Border.Num()==4)
            {
                Indices.Append({Border[0],Border[2],Border[1],Border[0],Border[3],Border[2]});
            }
            else
            {
                const int32 Middle=Vertex(A+FIntPoint(Step/2,Step/2));
                if (Middle!=INDEX_NONE)
                    for (int32 K=0;K<Border.Num();++K) Indices.Append({Middle,Border[(K+1)%Border.Num()],Border[K]});
            }
        }
        FineStep=Step;
        const FIntPoint Extent(Half*Step,Half*Step);
        FineMin=Center-Extent; FineMax=Center+Extent;
    }
    if (Terrain.Num()>5000)
        for (auto It=Terrain.CreateIterator();It;++It)
            if (!Used.Contains(It.Key())) It.RemoveCurrent();
    if (!GroundPatch || GroundPatch->Indices!=Indices || GroundPatch->Vertices!=Vertices)
        GroundPatch=MoveTemp(Patch);
}

void UGuLiXuntianSceneGuide::PublishGroundCoverage(const UGuLiXuntianGunnery& Aim)
{
    if (!CoverageTask.IsValid() || !CoverageTask.IsCompleted()) return;
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiXuntian_CoveragePublish);
    const auto* Ship=CastChecked<AGuLiStrikeShip>(GetOwner());
    const FVector Position=Ship->GetActorLocation();
    if (LastLoadout!=Ship->GetLoadoutRevision() || LastMin!=Aim.GroundMinDepression ||
        LastMax!=Aim.GroundMaxDepression || FineCenter!=GroundCellCenter(Position,3200) ||
        CoarseCenter!=GroundCellCenter(Position,12800))
    {
        CoverageTask={};
        LastBellyCount=INDEX_NONE;
        return;
    }
    const auto& UV=CoverageTask.GetResult();
    if (DisplayedPatch==GroundPatch)
        Surface->UpdateMeshSection(0,GroundPatch->Vertices,{},UV,{},{});
    else
    {
        Surface->CreateMeshSection(0,GroundPatch->Vertices,GroundPatch->Indices,{},UV,{},{},false);
        DisplayedPatch=GroundPatch;
    }
    CSV_CUSTOM_STAT(GuLiXuntianCoverage,PublishedVertices,UV.Num(),ECsvCustomStatOp::Set);
    CSV_CUSTOM_STAT(GuLiXuntianCoverage,PublishLatencyMs,float((FPlatformTime::Seconds()-CoverageStarted)*1000.),ECsvCustomStatOp::Set);
    CoverageTask={};
}

namespace
{
void AddLevelMarks(TArray<FBatchedLine>& Lines,const FVector& Origin)
{
    for (const double Radius : {18000.,42000.})
    for (int32 I=0;I<36;++I)
    {
        const double Base=I*(UE_DOUBLE_PI/18.);
        auto Mark=[&](double A,double Height=0.) {
            return Origin+FVector(Radius*FMath::Cos(Base+A),Radius*FMath::Sin(Base+A),Height);
        };
        Line(Lines,Mark(-.055),Mark(-.013),Air*.68f,0);
        Line(Lines,Mark(.013),Mark(.055),Air*.68f,0);
        Line(Lines,Mark(0),Mark(0,Radius*.008),Air*.68f,0);
    }
}

}

FVector UGuLiXuntianSceneGuide::FAirContact::InterpolatedCenter(double Now) const
{
    if (BlendSeconds<=0) return Sample;
    return FMath::Lerp(From,Sample,FMath::Clamp((Now-SampleTime)/BlendSeconds,0.,1.));
}

void UGuLiXuntianSceneGuide::RefreshAirContacts(const UGuLiXuntianGunnery& Aim, EGuLiTeam Team, double Now)
{
    AirContactsRevision = Aim.GetContactsRevision();
    if (Team!=AirViewerTeam) AirContacts.Reset();
    AirViewerTeam = Team;
    for (const auto& C : Aim.GetContacts())
    {
        if (C.Team==Team || (C.Handle.Kind!=EGuLiTargetKind::Wingman && C.Handle.Kind!=EGuLiTargetKind::Ship)) continue;
        auto& Track = AirContacts.FindOrAdd(C.Handle);
        Track.Revision = AirContactsRevision;
        Track.Radius = C.CollisionRadius;
        const AActor* Actor = C.CollisionActor.Get();
        if (const auto* Wingman = Cast<AGuLiWingmanPawn>(Actor)) Track.Body = Wingman->GetBodyMesh();
        else if (const auto* Ship = Cast<AGuLiStrikeShip>(Actor)) Track.Body = Ship->GetHullMeshComponent();
        else Track.Body.Reset();

        const FVector Center = C.GetHitCenter();
        if (Track.SampleTime<0 || Center!=Track.Sample)
        {
            // Contacts without a local mesh arrive at the gunnery sampling rate.
            const bool bSnap = Track.SampleTime<0 || FVector::DistSquared(Center,Track.Sample)>FMath::Square(10000.);
            Track.From = bSnap ? Center : Track.InterpolatedCenter(Now);
            Track.BlendSeconds = bSnap ? 0. : FMath::Clamp(Now-Track.SampleTime,.08,.12);
            Track.Sample = Center;
            Track.SampleTime = Now;
            if (bSnap) Track.PendingSince = -1;
        }
    }
    for (auto It=AirContacts.CreateIterator(); It; ++It)
        if (It.Value().Revision!=AirContactsRevision) It.RemoveCurrent();
}

void UGuLiXuntianSceneGuide::UpdateAirMarkersBeforeRender(UWorld* World)
{
    // Hybrid wingmen publish their displayed pose at OnWorldTickEnd, after TG_PostUpdateWork.
    if (!bAirUpdatePending || World!=GetWorld() || World->bInTick) return;
    bAirUpdatePending = false;
    const bool bCheckCoverage = bCheckAirCoverage;
    bCheckAirCoverage = false;
    const auto* Ship = Cast<AGuLiStrikeShip>(GetOwner());
    if (!IsVisible() || !IsValid(Ship) || !Ship->GetXuntianGunnery()) return;
    const auto& Aim = *Ship->GetXuntianGunnery();
    const double HullHeight = Ship->GetHullMeshComponent()->Bounds.Origin.Z;
    const double Now = World->GetTimeSeconds();
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiXuntian_UpdateAirMarkers);
    bool bChanged = MarkerHullHeight!=HullHeight || MarkerReach!=Aim.Reach;
    MarkerHullHeight = HullHeight;
    MarkerReach = Aim.Reach;
    int32 Written = 0;
    for (auto& Pair : AirContacts)
    {
        auto& Track = Pair.Value;
        const auto* Body = Track.Body.Get();
        if (!Body && Track.Body.IsStale()) continue;
        if (Body && (!IsValid(Body->GetOwner()) || !Body->IsVisible() || Body->GetOwner()->IsHidden())) continue;
        const FVector Center = Body ? Body->Bounds.Origin : Track.InterpolatedCenter(Now);
        const FVector Extent = Body && Pair.Key.Kind==EGuLiTargetKind::Ship ? Body->Bounds.BoxExtent : FVector(Track.Radius);
        if (Track.PendingSince<0 && !bCheckCoverage) continue;
        if (bCheckCoverage)
        {
            // Only the displayed badge has a deadband at the hull plane and a dwell at the firing arc.
            constexpr double HeightDeadbandCm = 150., ConfirmSeconds = .15;
            const double DZ = Center.Z-HullHeight;
            const bool bNew = Track.PendingSince<0;
            if (bNew || FMath::Abs(DZ)>HeightDeadbandCm) Track.bBelow = DZ<0;
            const EAirBadge Desired = Track.bBelow
                ? (Aim.CoverageAt(Center)>0 ? EAirBadge::Belly : EAirBadge::Closed)
                : (AirGunCount>0 ? EAirBadge::Air : EAirBadge::Closed);
            if (bNew || Desired!=Track.Pending)
            {
                Track.Pending = Desired;
                Track.PendingSince = Now;
            }
            if (bNew || Now-Track.PendingSince>=ConfirmSeconds) Track.Badge = Desired;
        }
        const FGuLiXuntianAirMarker Marker{Center,Extent,Track.Badge};
        if (Written==AirMarkers.Num()) { AirMarkers.Add(Marker); bChanged=true; }
        else
        {
            auto& Last = AirMarkers[Written];
            bChanged |= Last.Center!=Center || Last.Extent!=Extent || Last.Badge!=Track.Badge;
            Last = Marker;
        }
        ++Written;
    }
    bChanged |= Written!=AirMarkers.Num();
    AirMarkers.SetNum(Written,EAllowShrinking::No);
    if (bChanged || bSurveyLinesDirty) MarkRenderDynamicDataDirty();
}

void UGuLiXuntianSceneGuide::UpdateGuide(const UGuLiXuntianGunnery& Aim)
{
    const auto* Ship=CastChecked<AGuLiStrikeShip>(GetOwner());
    const auto* PC=Cast<APlayerController>(Ship->GetController());
    if (!PC || !PC->PlayerCameraManager) return;
    const auto& View=PC->PlayerCameraManager->GetCameraCacheView();
    if (Sky) Sky->SetWorldLocation(View.Location);
    PublishGroundCoverage(Aim);
    const double Now=GetWorld()->GetTimeSeconds();
    const auto Team=Ship->GetCombatHealthComponent()->GetCombatTeam();
    const bool bNewContacts=AirContactsRevision!=Aim.GetContactsRevision() || Team!=AirViewerTeam;
    if (bNewContacts) RefreshAirContacts(Aim,Team,Now);
    const FVector Hull=Ship->GetHullMeshComponent()->Bounds.Origin;
    const bool bSurvey=Now>=NextSurvey;
    int32 BellyGuns=0;
    if (bNewContacts || bSurvey)
    {
        AirGunCount=0;
        for (const auto& G : Aim.GetMounts())
            if (G.Part.IsValid() && G.bHasGeometry) { if (G.bGround) ++BellyGuns; else ++AirGunCount; }
        bCheckAirCoverage=true;
    }
    if (bSurvey)
    {
        NextSurvey=Now+.06;
        SurveyGround(Aim,Ship->GetActorLocation(),BellyGuns);
    }
    if (bSurvey && (BatchedLines.IsEmpty() || Hull!=LevelMarkOrigin || Aim.bGroundHint || bHadGroundHint))
    {
        BatchedLines.Reset(224);
        AddLevelMarks(BatchedLines,Hull);
        LevelMarkOrigin=Hull;
        bHadGroundHint=Aim.bGroundHint;
        bSurveyLinesDirty=true;
        if (Aim.bGroundHint)
        {
            const double Side=FMath::Clamp(FVector::Dist(View.Location,Aim.GroundHint)*.014,120.,800.);
            for (int32 X : {-1,1}) for (int32 Y : {-1,1})
            {
                const FVector Corner=Aim.GroundHint+FVector(X*Side,Y*Side,0);
                FVector A,B,C;
                if (Aim.SurfaceAt(Corner,A) && Aim.SurfaceAt(Corner-FVector(X*Side*.45,0,0),B) &&
                    Aim.SurfaceAt(Corner-FVector(0,Y*Side*.45,0),C))
                {
                    const FVector Lift(0,0,18);
                    Line(BatchedLines,A+Lift,B+Lift,Amber,2); Line(BatchedLines,A+Lift,C+Lift,Amber,2);
                }
            }
        }
    }
    bAirUpdatePending=true;
}
