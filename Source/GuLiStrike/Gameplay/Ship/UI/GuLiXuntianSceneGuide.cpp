#include "Gameplay/Ship/UI/GuLiXuntianSceneGuide.h"
#include "Gameplay/Ship/Aiming/GuLiXuntianGunnery.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "MeshElementCollector.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
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
const FLinearColor Muted(FColor::FromHex(TEXT("D5C09C")));
const FLinearColor Closed(FColor::FromHex(TEXT("EABCBA")));
const FLinearColor Dark(FColor::FromHex(TEXT("2C3735")));

// Pilot marks use the regular depth-tested geometry pass, including in non-debug builds.
class FPilotMarkProxy final : public FPrimitiveSceneProxy
{
public:
    explicit FPilotMarkProxy(const UGuLiXuntianSceneGuide& Guide) : FPrimitiveSceneProxy(&Guide), Marks(Guide.BatchedLines)
    {
        bWillEverBeLit=false;
    }
    virtual SIZE_T GetTypeHash() const override { static uint8 Type; return reinterpret_cast<SIZE_T>(&Type); }
    virtual uint32 GetMemoryFootprint() const override { return sizeof(*this)+Marks.GetAllocatedSize(); }
    virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
    {
        FPrimitiveViewRelevance R;
        R.bDrawRelevance=IsShown(View); R.bDynamicRelevance=true; R.bNormalTranslucency=true;
        return R;
    }
    virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views,const FSceneViewFamily& Family,
                                         uint32 Visible,FMeshElementCollector& Collector) const override
    {
        for (int32 I=0;I<Views.Num();++I)
        {
            if (!(Visible & (1u<<I))) continue;
            const FIntRect Rect=Views[I]->UnscaledViewRect;
            const float Scale=FMath::Min(Rect.Width()/1920.f,Rect.Height()/1080.f);
            auto* PDI=Collector.GetPDI(I);
            for (const auto& M : Marks)
                PDI->DrawTranslucentLine(M.Start,M.End,M.Color,SDPG_World,M.Thickness*Scale,0,true);
        }
    }
private:
    TArray<FBatchedLine> Marks;
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
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
    GroundInk=Ground.Object; SkyInk=AirGuide.Object; DomeMesh=Sphere.Object;
}

FPrimitiveSceneProxy* UGuLiXuntianSceneGuide::CreateSceneProxy()
{
    return BatchedLines.IsEmpty()?nullptr:new FPilotMarkProxy(*this);
}

void UGuLiXuntianSceneGuide::OnRegister()
{
    Super::OnRegister();
    if (IsTemplate() || !GetWorld() || !GetWorld()->IsGameWorld() || GetWorld()->IsNetMode(NM_DedicatedServer)) return;
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
    CoverageTask={}; // An outstanding job owns its immutable inputs and cannot touch this component.
    GroundPatch.Reset();
    DisplayedPatch.Reset();
    Terrain.Reset();
    MissingSurfaceKeys.Reset();
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

void AddAirBadge(TArray<FBatchedLine>& Lines, const FVector& P, const FVector& Right, const FVector& Up,
                 double Unit, bool Ground, int32 Count, int32 Total)
{
    const FLinearColor Ink=Count ? (Ground?Amber:Air) : Closed;
    auto Point=[&](double X,double Y) { return P+(Right*X-Up*Y)*Unit; };
    for (int32 Pass=0;Pass<2;++Pass)
    {
        auto Stroke=[&](double X,double Y,double U,double V)
        { Line(Lines,Point(X,Y),Point(U,V),Pass?Ink:Dark,Pass?1.7f:3.8f); };
        if (Count==0) { Stroke(-7,-7,7,7); Stroke(-7,7,7,-7); continue; }
        const int32 S=Ground?-1:1;
        Stroke(-8,6*S,5,6*S); Stroke(0,3*S,5,-5*S); Stroke(3,4*S,8,-4*S);
        Stroke(-6,4*S,-4,0); Stroke(-4,0,0,0); Stroke(0,0,2,4*S); Stroke(2,4*S,-6,4*S);
        if (Ground)
            for (int32 I=0;I<Total;++I)
            {
                const double X=(I-(Total-1)*.5)*7;
                Line(Lines,Point(X-2,10),Point(X+2,10),Pass?(I<Count?Amber:Muted):Dark,Pass?(I<Count?2.f:1.f):3.5f);
            }
    }
}
}

void UGuLiXuntianSceneGuide::UpdateGuide(const UGuLiXuntianGunnery& Aim)
{
    auto* Ship=CastChecked<AGuLiStrikeShip>(GetOwner());
    const auto* PC=Cast<APlayerController>(Ship->GetController());
    if (!PC || !PC->PlayerCameraManager) return;
    const auto& View=PC->PlayerCameraManager->GetCameraCacheView();
    if (Sky) Sky->SetWorldLocation(View.Location);
    PublishGroundCoverage(Aim);
    const double Now=GetWorld()->GetTimeSeconds();
    if (Now<NextSurvey) return;
    NextSurvey=Now+.06;
    int32 AirCount=0, BellyCount=0;
    for (const auto& G : Aim.GetMounts())
        if (G.Part.IsValid() && G.bHasGeometry) { if (G.bGround) ++BellyCount; else ++AirCount; }
    const FVector Hull=Ship->GetHullMeshComponent()->Bounds.Origin;
    SurveyGround(Aim,Ship->GetActorLocation(),BellyCount);
    TArray<FBatchedLine> Lines;
    Lines.Reserve(260+40*Aim.GetContacts().Num());
    AddLevelMarks(Lines,Hull);
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
                Line(Lines,A+Lift,B+Lift,Amber,2); Line(Lines,A+Lift,C+Lift,Amber,2);
            }
        }
    }
    const FRotationMatrix Basis(View.Rotation);
    const FVector Right=Basis.GetUnitAxis(EAxis::Y), Up=Basis.GetUnitAxis(EAxis::Z), Forward=Basis.GetUnitAxis(EAxis::X);
    double PixelAtUnitDepth=2.*FMath::Tan(FMath::DegreesToRadians(27.))/1080.;
    if (const auto* LP=PC->GetLocalPlayer();LP && LP->ViewportClient && LP->ViewportClient->Viewport)
    {
        FSceneViewProjectionData Projection;
        if (LP->GetProjectionData(LP->ViewportClient->Viewport,Projection))
        {
            const FIntRect Rect=Projection.GetConstrainedViewRect();
            if (Rect.Height()>0 && Projection.ProjectionMatrix.M[1][1]>0)
            {
                const double Scale=FMath::Min(Rect.Width()/1920.,Rect.Height()/1080.);
                PixelAtUnitDepth=2.*Scale/(Rect.Height()*Projection.ProjectionMatrix.M[1][1]);
            }
        }
    }
    for (const auto& C : Aim.GetContacts())
    {
        if (C.Team==Ship->GetCombatHealthComponent()->GetCombatTeam() ||
            (C.Handle.Kind!=EGuLiTargetKind::Ship && C.Handle.Kind!=EGuLiTargetKind::Wingman)) continue;
        FVector P=C.GetHitCenter(), Extent(C.CollisionRadius);
        if (const auto* Other=Cast<AGuLiStrikeShip>(C.CollisionActor.Get()))
        {
            const auto& TargetBounds=Other->GetHullMeshComponent()->Bounds;
            P=TargetBounds.Origin; Extent=TargetBounds.BoxExtent;
        }
        const FVector Offset=P-View.Location;
        const double Depth=Offset.Dot(Forward);
        if (Depth<=1 || Offset.SizeSquared()>FMath::Square(double(Aim.Reach))) continue;
        const double Pixel=Depth*PixelAtUnitDepth;
        const bool Ground=P.Z<Hull.Z;
        const int32 Count=Ground?Aim.CoverageAt(P):AirCount;
        const FLinearColor Ink=Count?(Ground?Amber:Air):Closed;
        const FVector Top=P-Right*(Right.GetAbs().Dot(Extent)+8*Pixel);
        const FVector Foot(Top.X,Top.Y,Hull.Z);
        Line(Lines,Foot-Right*(7*Pixel),Foot+Right*(7*Pixel),Ink*.82f,0);
        const int32 Dashes=FMath::Clamp(FMath::CeilToInt(FMath::Abs(Hull.Z-P.Z)/(Pixel*9)),0,24);
        for (int32 I=0;I<Dashes;++I)
            Line(Lines,FMath::Lerp(Top,Foot,(I+.05)/Dashes),FMath::Lerp(Top,Foot,(I+.66)/Dashes),Ink*.82f,0);
        AddAirBadge(Lines,Top-Right*(15*Pixel),Right,Up,Pixel*28./32,Ground,Count,BellyCount);
    }
    Flush();
    DrawLines(Lines);
}
