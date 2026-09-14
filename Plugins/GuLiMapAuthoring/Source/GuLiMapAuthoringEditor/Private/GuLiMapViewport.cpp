#include "GuLiMapViewport.h"
#include "GuLiMapGeometryEditor.h"
#include "GuLiMapAuthoring.h"
#include "GuLiMapAuthoringSubsystem.h"
#include "GuLiMapAuthoringSettings.h"
#include "GuLiMapDensityMap.h"
#include "GuLiMapMarker.h"
#include "Algo/Reverse.h"
#include "ComponentVisualizer.h"
#include "EdMode.h"
#include "Editor.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "EditorModeManager.h"
#include "EditorModeRegistry.h"
#include "EditorViewportClient.h"
#include "Engine/World.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PrimitiveDrawInterface.h"
#include "PrimitiveDrawingUtils.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

namespace
{
const FEditorModeID PlacementId=TEXT("EM_GuLiMapPlacement");
const FEditorModeID DensityPaintId=TEXT("EM_GuLiMapDensityPaint");
TMap<FName,TSharedPtr<IGuLiMapGeometryEditor>> Editors;
TWeakObjectPtr<UGuLiMapTypeDefinition> PlacementType;
TSharedPtr<FComponentVisualizer> RegisteredVisualizer;
struct FDensityPaintState
{
    TWeakObjectPtr<AGuLiMapDensityMap> Actor;
    FGuLiMapDensityBrushSettings Brush;
};
FDensityPaintState DensityPaintState;
constexpr int32 CenterHandle=-100;

class FBuiltInGeometryEditor : public IGuLiMapGeometryEditor
{
public:
    TArray<FGuLiMapEditHandle> GetHandles(const FInstancedStruct& S) const override
    {
        TArray<FGuLiMapEditHandle> H;
        if (auto* P=S.GetPtr<FGuLiMapCylinder>()) H.Append({{-3,FVector(P->Radius,0,0)},{-1,FVector(0,0,P->MinZ)},{-2,FVector(0,0,P->MaxZ)}});
        if (auto* P=S.GetPtr<FGuLiMapSphere>()) H.Add({-3,FVector(P->Radius,0,0)});
        if (auto* P=S.GetPtr<FGuLiMapBox>()) H.Append({{-4,FVector(P->HalfExtents.X,0,0)},{-5,FVector(0,P->HalfExtents.Y,0)},{-6,FVector(0,0,P->HalfExtents.Z)}});
        if (auto* P=S.GetPtr<FGuLiMapPolygonPrism>())
        {
            H.Append({{-1,FVector(0,0,P->MinZ)},{-2,FVector(0,0,P->MaxZ)}});
            for (int32 I=0;I<P->Vertices.Num();++I)
            {
                const auto V=P->Vertices[I], E=(V+P->Vertices[(I+1)%P->Vertices.Num()])*0.5;
                H.Add({I,FVector(V.X,V.Y,0)}); H.Add({10000+I,FVector(E.X,E.Y,0),true});
            }
        }
        return H;
    }
    bool MoveHandle(FInstancedStruct& S,int32 Id,const FVector& D) const override
    {
        if (auto* P=S.GetMutablePtr<FGuLiMapCylinder>())
        {
            if (Id==-3) P->Radius+=D.X; else if (Id==-1) P->MinZ+=D.Z; else if (Id==-2) P->MaxZ+=D.Z; else return false; return true;
        }
        if (auto* P=S.GetMutablePtr<FGuLiMapSphere>()) { if (Id!=-3) return false; P->Radius+=D.X; return true; }
        if (auto* P=S.GetMutablePtr<FGuLiMapBox>()) { if (Id>-4||Id<-6) return false; const int Axis=-Id-4; P->HalfExtents[Axis]+=D[Axis]; return true; }
        if (auto* P=S.GetMutablePtr<FGuLiMapPolygonPrism>())
        {
            if (Id==-1) P->MinZ+=D.Z; else if (Id==-2) P->MaxZ+=D.Z;
            else if (P->Vertices.IsValidIndex(Id)) P->Vertices[Id]+=FVector2D(D.X,D.Y);
            else return false; return true;
        }
        return false;
    }
    bool InsertVertex(FInstancedStruct& S,int32 Id) const override
    {
        auto* P=S.GetMutablePtr<FGuLiMapPolygonPrism>(); const int32 Edge=Id-10000;
        if (!P||!P->Vertices.IsValidIndex(Edge)) return false;
        P->Vertices.Insert((P->Vertices[Edge]+P->Vertices[(Edge+1)%P->Vertices.Num()])*0.5,Edge+1); return true;
    }
    bool DeleteVertex(FInstancedStruct& S,int32 Id) const override
    {
        auto* P=S.GetMutablePtr<FGuLiMapPolygonPrism>(); if (!P||P->Vertices.Num()<=3||!P->Vertices.IsValidIndex(Id)) return false;
        P->Vertices.RemoveAt(Id); return true;
    }
    bool ReverseWinding(FInstancedStruct& S) const override
    {
        auto* P=S.GetMutablePtr<FGuLiMapPolygonPrism>(); if (!P) return false; Algo::Reverse(P->Vertices); return true;
    }
};
struct HGuLiMapHandle : public HComponentVisProxy
{
    DECLARE_HIT_PROXY();
    FGuid RegionId; int32 Handle;
    HGuLiMapHandle(const UActorComponent* C,FGuid R,int32 H):HComponentVisProxy(C,HPP_UI),RegionId(R),Handle(H) {}
};
IMPLEMENT_HIT_PROXY(HGuLiMapHandle,HComponentVisProxy)

class FGuLiMapVisualizer : public FComponentVisualizer
{
    TWeakObjectPtr<AGuLiMapMarker> Actor; FGuid RegionId; int32 Handle=CenterHandle;
    TUniquePtr<FScopedTransaction> Transaction; FGuLiMapMarkerRecord Before;
    FGuLiMapRegionRecord* Region() const { return Actor.IsValid()?Actor->Record.Regions.FindByPredicate([&](const auto& R){return R.RegionId==RegionId;}):nullptr; }
    void Finish(bool Commit)
    {
        if (!Transaction) return;
        if (Actor.IsValid())
        {
            if (auto* R=Region())
            {
                auto H=GuLiMap::FindGeometry(R->Geometry); FString Error;
                Commit &= H && H->Validate(R->Geometry,Error) && !R->Translation.ContainsNaN() && !R->Rotation.ContainsNaN();
                if (!Commit && !Error.IsEmpty()) UE_LOG(LogTemp,Warning,TEXT("GuLiMap: drag rolled back: %s"),*Error);
            }
            else Commit=false;
            if (!Commit) Actor->Record=Before;
            Actor->RefreshVisuals();
            FPropertyChangedEvent E(FindFProperty<FProperty>(AGuLiMapMarker::StaticClass(),GET_MEMBER_NAME_CHECKED(AGuLiMapMarker,Record)));
            FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Actor.Get(),E);
        }
        if (!Commit) Transaction->Cancel();
        Transaction.Reset();
    }
    void Begin()
    {
        if (!Transaction&&Actor.IsValid())
        { Before=Actor->Record; Transaction=MakeUnique<FScopedTransaction>(NSLOCTEXT("GuLiMap","EditRegion","编辑地图区域")); Actor->Modify(); }
    }
public:
    ~FGuLiMapVisualizer() override { EndEditing(); }
    void DrawVisualization(const UActorComponent* C,const FSceneView* View,FPrimitiveDrawInterface* PDI) override
    {
        // UE can draw the cached selection after deletion/GC has invalidated its
        // component property path. GetComponent() then supplies nullptr here.
        if (!IsValid(C)) return;
        auto* A=Cast<AGuLiMapMarker>(C->GetOwner()); if (!IsValid(A)) return;
        for (const auto& R:A->Record.Regions)
        {
            auto H=GuLiMap::FindGeometry(R.Geometry); if (!H) continue; auto E=GuLiMapEditor::FindGeometryEditor(H->GetShapeType()); if (!E) continue;
            auto Handles=E->GetHandles(R.Geometry); Handles.Add({CenterHandle,FVector::ZeroVector});
            const FTransform W=R.GetTransform()*A->GetActorTransform();
            for (const auto& Point:Handles)
            {
                const bool Active=A==Actor.Get()&&RegionId==R.RegionId&&Handle==Point.Id;
                PDI->SetHitProxy(new HGuLiMapHandle(C,R.RegionId,Point.Id));
                PDI->DrawPoint(W.TransformPosition(Point.Position),Active?FLinearColor::Yellow:Point.bEdge?FLinearColor(0.3f,1,0.5f):FLinearColor::White,Point.bEdge?10:16,SDPG_Foreground);
                PDI->SetHitProxy(nullptr);
            }
        }
    }
    bool VisProxyHandleClick(FEditorViewportClient* Client,HComponentVisProxy* Proxy,const FViewportClick& Click) override
    {
        if (Click.IsAltDown()||!Proxy||!Proxy->IsA(HGuLiMapHandle::StaticGetType())) return false;
        // Hit proxies also outlive the actor until the viewport hit map refreshes.
        const UActorComponent* C=Proxy->Component.Get(); if (!IsValid(C)) return false;
        auto* HitActor=Cast<AGuLiMapMarker>(C->GetOwner()); if (!IsValid(HitActor)) return false;
        Finish(false); auto* Hit=static_cast<HGuLiMapHandle*>(Proxy);
        Actor=HitActor; RegionId=Hit->RegionId; Handle=Hit->Handle; return Actor.IsValid();
    }
    UActorComponent* GetEditedComponent() const override { return Actor.IsValid()?Actor->Visualization.Get():nullptr; }
    bool GetWidgetLocation(const FEditorViewportClient* Client,FVector& Out) const override
    {
        auto* R=Region(); if (!R) return false;
        const FTransform W=R->GetTransform()*Actor->GetActorTransform();
        if (Handle==CenterHandle) { Out=W.GetLocation(); return true; }
        auto H=GuLiMap::FindGeometry(R->Geometry); auto E=H?GuLiMapEditor::FindGeometryEditor(H->GetShapeType()):nullptr; if (!E) return false;
        for (const auto& P:E->GetHandles(R->Geometry)) if (P.Id==Handle) { Out=W.TransformPosition(P.Position); return true; }
        return false;
    }
    bool GetCustomInputCoordinateSystem(const FEditorViewportClient* Client,FMatrix& Out) const override
    {
        if (auto* R=Region()) { Out=(R->GetTransform()*Actor->GetActorTransform()).ToMatrixNoScale().RemoveTranslation(); return true; } return false;
    }
    void TrackingStarted(FEditorViewportClient* Client) override { if (Region()) Begin(); }
    void TrackingStopped(FEditorViewportClient* Client,bool Moved) override { Finish(Moved); }
    bool HandleInputDelta(FEditorViewportClient* Client,FViewport* Viewport,FVector& Translation,FRotator& Rotation,FVector& Scale) override
    {
        auto* R=Region(); if (!R) return false;
        Begin();
        if (Handle==CenterHandle)
        {
            R->Translation+=Actor->GetActorTransform().InverseTransformVectorNoScale(Translation);
            const FQuat World=(R->GetTransform()*Actor->GetActorTransform()).GetRotation();
            R->Rotation=(Actor->GetActorQuat().Inverse()*Rotation.Quaternion()*World).Rotator();
        }
        else
        {
            auto H=GuLiMap::FindGeometry(R->Geometry); auto E=H?GuLiMapEditor::FindGeometryEditor(H->GetShapeType()):nullptr;
            if (E) E->MoveHandle(R->Geometry,Handle,(R->GetTransform()*Actor->GetActorTransform()).InverseTransformVectorNoScale(Translation));
        }
        Actor->RefreshVisuals(); return true;
    }
    bool HandleInputKey(FEditorViewportClient* Client,FViewport* Viewport,FKey Key,EInputEvent Event) override
    {
        if (Event!=IE_Pressed||!Region()) return false;
        if (Key==EKeys::Escape) { EndEditing(); return true; }
        if (Key!=EKeys::Insert&&Key!=EKeys::Delete&&!(Key==EKeys::R&&Viewport->KeyState(EKeys::LeftControl))) return false;
        auto* R=Region(); auto H=GuLiMap::FindGeometry(R->Geometry); auto E=H?GuLiMapEditor::FindGeometryEditor(H->GetShapeType()):nullptr;
        if (!E) return false; Begin(); bool Changed=false;
        if (Key==EKeys::Insert) Changed=E->InsertVertex(R->Geometry,Handle);
        if (Key==EKeys::Delete) Changed=E->DeleteVertex(R->Geometry,Handle);
        if (Key==EKeys::R) Changed=E->ReverseWinding(R->Geometry);
        Finish(Changed); if (Changed) Handle=CenterHandle; return true;
    }
    void EndEditing() override { Finish(false); Actor.Reset(); RegionId.Invalidate(); Handle=CenterHandle; }
};
class FGuLiMapPlacementMode : public FEdMode
{
public:
    bool UsesToolkits() const override { return false; }
    bool HandleClick(FEditorViewportClient* Client,HHitProxy* Proxy,const FViewportClick& Click) override
    {
        if (Click.IsAltDown()||Click.GetKey()!=EKeys::LeftMouseButton||!PlacementType.IsValid()||!GEditor) return false;
        auto* Service=GEditor->GetEditorSubsystem<UGuLiMapAuthoringSubsystem>(); UWorld* W=Service->EditorWorld(); if (!W) return false;
        const auto* Settings=GetDefault<UGuLiMapAuthoringSettings>();
        FVector Point; FHitResult Hit; bool Found=false;
        if (Settings->bSurfacePlacement) Found=W->LineTraceSingleByChannel(Hit,Click.GetOrigin(),Click.GetOrigin()+Click.GetDirection()*1.e9,ECC_Visibility);
        if (Found) Point=Hit.ImpactPoint;
        else
        {
            if (FMath::Abs(Click.GetDirection().Z)<1.e-8) return true;
            const double Distance=(Settings->WorkPlaneZ-Click.GetOrigin().Z)/Click.GetDirection().Z;
            if (Distance<0) return true; Point=Click.GetOrigin()+Click.GetDirection()*Distance;
        }
        if (auto* A=Service->CreateMarker(PlacementType.Get(),Point)) { GEditor->SelectNone(false,true); GEditor->SelectActor(A,true,true); }
        return true;
    }
    bool InputKey(FEditorViewportClient* Client,FViewport* Viewport,FKey Key,EInputEvent Event) override
    {
        if (Key==EKeys::Escape&&Event==IE_Pressed) { GuLiMapEditor::EndInteraction(); return true; }
        return FEdMode::InputKey(Client,Viewport,Key,Event);
    }
    void Exit() override { PlacementType.Reset(); FEdMode::Exit(); }
};

class FGuLiMapDensityPaintMode : public FEdMode
{
    TUniquePtr<FScopedTransaction> Transaction;
    FGuLiMapDensityMapRecord Before;
    TArray<FVector2D> Samples;
    FVector CursorWorld = FVector::ZeroVector;
    bool bHasCursor = false;

    bool IsAltDown(const FViewport* Viewport) const
    {
        return Viewport && (Viewport->KeyState(EKeys::LeftAlt) || Viewport->KeyState(EKeys::RightAlt));
    }
    bool FindSurface(FEditorViewportClient* Client,FVector& Out) const
    {
        AGuLiMapDensityMap* Actor=DensityPaintState.Actor.Get();
        if (!Actor||!Client) return false;
        UWorld* World=Actor->GetWorld(); if (!World) return false;
        const FViewportCursorLocation Cursor=Client->GetCursorWorldLocationFromMousePos();
        FHitResult Hit;
        if (World->LineTraceSingleByChannel(Hit,Cursor.GetOrigin(),Cursor.GetOrigin()+Cursor.GetDirection()*1.e9,ECC_Visibility))
        { Out=Hit.ImpactPoint; return true; }
        if (FMath::Abs(Cursor.GetDirection().Z)<1.e-8) return false;
        const double Distance=(GetDefault<UGuLiMapAuthoringSettings>()->WorkPlaneZ-Cursor.GetOrigin().Z)/Cursor.GetDirection().Z;
        if (Distance<0.0) return false; Out=Cursor.GetOrigin()+Cursor.GetDirection()*Distance; return true;
    }
    void BroadcastChange(AGuLiMapDensityMap* Actor)
    {
        if (!Actor) return;
        FPropertyChangedEvent Changed(FindFProperty<FProperty>(AGuLiMapDensityMap::StaticClass(),GET_MEMBER_NAME_CHECKED(AGuLiMapDensityMap,Record)),EPropertyChangeType::ValueSet);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Actor,Changed);
    }
    bool RebuildStroke()
    {
        AGuLiMapDensityMap* Actor=DensityPaintState.Actor.Get(); if (!Actor||!Transaction) return false;
        FGuLiMapDensityMapRecord Candidate=Before; FString Error;
        if (!GuLiMap::ApplyDensityBrush(Candidate,DensityPaintState.Brush.LayerKey,Samples,DensityPaintState.Brush.RadiusCm,DensityPaintState.Brush.Strength01,DensityPaintState.Brush.Falloff01,DensityPaintState.Brush.bErase,Error))
        { UE_LOG(LogTemp,Warning,TEXT("GuLiMap density stroke rejected: %s"),*Error); return false; }
        Actor->Record=MoveTemp(Candidate); Actor->RefreshVisuals(false); return true;
    }
    void FinishStroke(bool bCommit)
    {
        if (!Transaction) return;
        AGuLiMapDensityMap* Actor=DensityPaintState.Actor.Get();
        if (!Actor) bCommit=false;
        if (Actor&&bCommit)
        {
            FGuLiMapSnapshot Check; Check.DensityMap=Actor->Record; TArray<FGuLiMapIssue> Issues; GuLiMap::ValidateDensity(Check,Issues);
            bCommit=!GuLiMap::HasErrors(Issues);
            if (!bCommit) UE_LOG(LogTemp,Warning,TEXT("GuLiMap density stroke rolled back: %s"),Issues.IsEmpty()?TEXT("validation failed"):*Issues[0].Message);
        }
        if (Actor)
        {
            if (!bCommit) Actor->Record=Before;
            GuLiMap::NormalizeDensityMap(Actor->Record); Actor->RefreshVisuals(false); BroadcastChange(Actor);
        }
        if (!bCommit) Transaction->Cancel();
        Transaction.Reset(); Samples.Reset();
    }
    bool UpdateCursor(FEditorViewportClient* Client,FViewport* Viewport,const bool bAppend)
    {
        if (IsAltDown(Viewport)) return false;
        FVector Position; if (!FindSurface(Client,Position)) { bHasCursor=false; return false; }
        CursorWorld=Position; bHasCursor=true;
        if (bAppend&&Transaction)
        {
            const FVector2D Point(Position.X,Position.Y);
            if (Samples.IsEmpty()||(Samples.Last()-Point).SizeSquared()>1.0) { Samples.Add(Point); RebuildStroke(); }
        }
        if (Viewport) Viewport->InvalidateDisplay();
        return true;
    }
public:
    bool UsesToolkits() const override { return false; }
    bool DisallowMouseDeltaTracking() const override { return Transaction.IsValid(); }
    bool GetCursor(EMouseCursor::Type& OutCursor) const override { OutCursor=EMouseCursor::Crosshairs; return true; }
    bool MouseMove(FEditorViewportClient* Client,FViewport* Viewport,int32,int32) override { UpdateCursor(Client,Viewport,Transaction.IsValid()); return false; }
    bool CapturedMouseMove(FEditorViewportClient* Client,FViewport* Viewport,int32,int32) override { return UpdateCursor(Client,Viewport,Transaction.IsValid()); }
    bool InputKey(FEditorViewportClient* Client,FViewport* Viewport,FKey Key,EInputEvent Event) override
    {
        if (!DensityPaintState.Actor.IsValid()) { FinishStroke(false); return false; }
        if (Key==EKeys::Escape&&Event==IE_Pressed)
        {
            if (Transaction) FinishStroke(false); else GuLiMapEditor::EndInteraction();
            return true;
        }
        if (Key!=EKeys::LeftMouseButton) return FEdMode::InputKey(Client,Viewport,Key,Event);
        if (Event==IE_Pressed)
        {
            if (IsAltDown(Viewport)||Transaction) return false;
            FVector Position; if (!FindSurface(Client,Position)) return true;
            AGuLiMapDensityMap* Actor=DensityPaintState.Actor.Get(); Before=Actor->Record; Samples={FVector2D(Position.X,Position.Y)};
            Transaction=MakeUnique<FScopedTransaction>(NSLOCTEXT("GuLiMap","DensityStroke","涂绘资源密度")); Actor->Modify(); CursorWorld=Position; bHasCursor=true; RebuildStroke();
            return true;
        }
        if (Event==IE_Released&&Transaction)
        {
            UpdateCursor(Client,Viewport,true); FinishStroke(true); return true;
        }
        return false;
    }
    void Render(const FSceneView* View,FViewport* Viewport,FPrimitiveDrawInterface* PDI) override
    {
        FEdMode::Render(View,Viewport,PDI);
        if (!bHasCursor||!DensityPaintState.Actor.IsValid()) return;
        const FLinearColor Color=DensityPaintState.Brush.bErase?FLinearColor(1.0f,0.15f,0.05f):FLinearColor(0.9f,0.9f,0.2f);
        DrawCircle(PDI,CursorWorld+FVector(0,0,20),FVector::ForwardVector,FVector::RightVector,Color,DensityPaintState.Brush.RadiusCm,96,SDPG_Foreground,2.0f,0.0f,true);
        const double Inner=DensityPaintState.Brush.RadiusCm*(1.0-DensityPaintState.Brush.Falloff01);
        if (Inner>1.0&&Inner<DensityPaintState.Brush.RadiusCm) DrawCircle(PDI,CursorWorld+FVector(0,0,21),FVector::ForwardVector,FVector::RightVector,Color*0.7f,Inner,64,SDPG_Foreground,1.0f,0.0f,true);
    }
    void Tick(FEditorViewportClient* Client,float DeltaTime) override
    {
        FEdMode::Tick(Client,DeltaTime);
        if (!DensityPaintState.Actor.IsValid()) { FinishStroke(false); DensityPaintState.Actor.Reset(); }
    }
    bool LostFocus(FEditorViewportClient* Client,FViewport* Viewport) override { FinishStroke(false); return FEdMode::LostFocus(Client,Viewport); }
    void Exit() override { FinishStroke(false); bHasCursor=false; DensityPaintState.Actor.Reset(); FEdMode::Exit(); }
};
}
namespace GuLiMapEditor
{
bool RegisterGeometryEditor(FName N,TSharedRef<IGuLiMapGeometryEditor> E) { if (Editors.Contains(N)) return false; Editors.Add(N,E); return true; }
void UnregisterGeometryEditor(FName N) { Editors.Remove(N); }
TSharedPtr<IGuLiMapGeometryEditor> FindGeometryEditor(FName N) { return Editors.FindRef(N); }
TSharedRef<FComponentVisualizer> CreateVisualizer() { return MakeShared<FGuLiMapVisualizer>(); }
void RegisterViewport()
{
    for (FName N:{FName(TEXT("Cylinder")),FName(TEXT("Sphere")),FName(TEXT("Box")),FName(TEXT("PolygonPrism"))}) RegisterGeometryEditor(N,MakeShared<FBuiltInGeometryEditor>());
    RegisteredVisualizer=CreateVisualizer(); GUnrealEd->RegisterComponentVisualizer(UGuLiMapVisualizationComponent::StaticClass()->GetFName(),RegisteredVisualizer); RegisteredVisualizer->OnRegister();
    FEditorModeRegistry::Get().RegisterMode<FGuLiMapPlacementMode>(PlacementId,NSLOCTEXT("GuLiMap","Mode","地图标记放置"),FSlateIcon(),false);
    FEditorModeRegistry::Get().RegisterMode<FGuLiMapDensityPaintMode>(DensityPaintId,NSLOCTEXT("GuLiMap","DensityMode","资源密度涂绘"),FSlateIcon(),false);
}
void BeginPlacement(UGuLiMapTypeDefinition* Type) { EndInteraction(); PlacementType=Type; GLevelEditorModeTools().ActivateMode(PlacementId); }
void BeginDensityPaint(AGuLiMapDensityMap* DensityMap,const FGuLiMapDensityBrushSettings& Settings)
{
    EndInteraction(); if (!IsValid(DensityMap)) return; DensityPaintState.Actor=DensityMap; DensityPaintState.Brush=Settings; GLevelEditorModeTools().ActivateMode(DensityPaintId);
}
void UpdateDensityBrush(const FGuLiMapDensityBrushSettings& Settings) { DensityPaintState.Brush=Settings; if (GEditor) GEditor->RedrawLevelEditingViewports(); }
bool IsDensityPainting() { return DensityPaintState.Actor.IsValid()&&GLevelEditorModeTools().IsModeActive(DensityPaintId); }
void EndInteraction()
{
    if (RegisteredVisualizer) RegisteredVisualizer->EndEditing();
    if (GEditor) { GLevelEditorModeTools().DeactivateMode(PlacementId); GLevelEditorModeTools().DeactivateMode(DensityPaintId); } PlacementType.Reset(); DensityPaintState.Actor.Reset();
}
void UnregisterViewport()
{
    EndInteraction(); FEditorModeRegistry::Get().UnregisterMode(PlacementId); FEditorModeRegistry::Get().UnregisterMode(DensityPaintId);
    if (GUnrealEd) GUnrealEd->UnregisterComponentVisualizer(UGuLiMapVisualizationComponent::StaticClass()->GetFName());
    RegisteredVisualizer.Reset(); Editors.Empty();
}
}
