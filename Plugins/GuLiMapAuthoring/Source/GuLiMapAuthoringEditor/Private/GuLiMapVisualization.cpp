#include "GuLiMapMarker.h"
#include "GuLiMapAuthoring.h"
#include "DynamicMeshBuilder.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "MeshElementCollector.h"
#include "PrimitiveDrawingUtils.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "SceneInterface.h"
#include "SceneView.h"

namespace
{
struct FRegionRenderData { FGuLiMapGeometryMesh Mesh; FLinearColor Color; };
class FGuLiMapSceneProxy final : public FPrimitiveSceneProxy
{
    TArray<FRegionRenderData> Regions;
    const FMaterialRenderProxy* FillMaterial=nullptr;
    FMaterialRelevance FillRelevance;
public:
    explicit FGuLiMapSceneProxy(const UGuLiMapVisualizationComponent* C):FPrimitiveSceneProxy(C)
    {
        bWillEverBeLit=false;
        FillMaterial=GEngine&&GEngine->DebugMeshMaterial?GEngine->DebugMeshMaterial->GetRenderProxy():nullptr;
        if (FillMaterial) FillRelevance=GEngine->DebugMeshMaterial->GetRelevance_Concurrent(GetScene().GetShaderPlatform());
        if (const auto* A=Cast<AGuLiMapMarker>(C->GetOwner()))
        {
            const auto* T=A->Record.Type.LoadSynchronous(); const FLinearColor Base=T?T->Color:FLinearColor::White;
            for (const auto& R:A->Record.Regions) if (auto H=GuLiMap::FindGeometry(R.Geometry))
            {
                FRegionRenderData Data; Data.Mesh=H->BuildMesh(R.Geometry); FString Error;
                Data.Color=H->Validate(R.Geometry,Error)?Base:FLinearColor::Red;
                if (!R.bEnabled || !A->Record.bEnabled) Data.Color=FLinearColor(0.4f,0.4f,0.4f);
                for (auto& V:Data.Mesh.Vertices) V=R.GetTransform().TransformPosition(V);
                for (auto& V:Data.Mesh.Lines) V=R.GetTransform().TransformPosition(V);
                Regions.Add(MoveTemp(Data));
            }
        }
    }
    SIZE_T GetTypeHash() const override { static size_t Unique; return reinterpret_cast<size_t>(&Unique); }
    uint32 GetMemoryFootprint() const override { return sizeof(*this)+GetAllocatedSize(); }
    FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
    {
        FPrimitiveViewRelevance R; R.bDrawRelevance=IsShown(View)&&!View->Family->EngineShowFlags.Game;
        R.bDynamicRelevance=true;
        // UE 5.7's DynamicEditorMeshElements pass accepts opaque/masked meshes only.
        // Use the regular translucent pass for region fill; the actor and component
        // are still editor-only and IsShown/Game-view gating keeps them out of play.
        R.bEditorPrimitiveRelevance=false;
        // The engine debug material renders After DOF, not in normal translucency.
        // Derive the actual pass flags from its material rather than hard-coding them.
        FillRelevance.SetPrimitiveViewRelevance(R); R.bOpaque=true; return R;
    }
    void GetDynamicMeshElements(const TArray<const FSceneView*>& Views,const FSceneViewFamily& Family,uint32 Visibility,FMeshElementCollector& Collector) const override
    {
        for (int32 I=0;I<Views.Num();++I) if (Visibility&(1<<I))
        {
            auto* PDI=Collector.GetPDI(I);
            DrawWireDiamond(PDI,GetLocalToWorld(),80,FLinearColor::White,SDPG_Foreground);
            for (const auto& R:Regions)
            {
                // Screen-space thickness stays readable at map-scale camera distances.
                for (int32 L=0;L+1<R.Mesh.Lines.Num();L+=2) PDI->DrawLine(GetLocalToWorld().TransformPosition(R.Mesh.Lines[L]),GetLocalToWorld().TransformPosition(R.Mesh.Lines[L+1]),R.Color,SDPG_World,2,0,true);
                if (FillMaterial&&!R.Mesh.Triangles.IsEmpty())
                {
                    auto* Material=new FColoredMaterialRenderProxy(FillMaterial,FLinearColor(R.Color.R,R.Color.G,R.Color.B,0.08f));
                    Collector.RegisterOneFrameMaterialProxy(Material);
                    FDynamicMeshBuilder Mesh(Views[I]->GetFeatureLevel());
                    for (const auto& V:R.Mesh.Vertices) Mesh.AddVertex(FVector3f(V),FVector2f::ZeroVector,FVector3f(1,0,0),FVector3f(0,1,0),FVector3f(0,0,1),FColor(255,255,255,24));
                    for (int32 K=0;K+2<R.Mesh.Triangles.Num();K+=3) Mesh.AddTriangle(R.Mesh.Triangles[K],R.Mesh.Triangles[K+1],R.Mesh.Triangles[K+2]);
                    Mesh.GetMesh(GetLocalToWorld(),Material,SDPG_World,true,false,I,Collector);
                }
            }
        }
    }
};
}
UGuLiMapVisualizationComponent::UGuLiMapVisualizationComponent()
{
    PrimaryComponentTick.bCanEverTick=false; SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetGenerateOverlapEvents(false); SetCanEverAffectNavigation(false); CastShadow=false;
    bUseEditorCompositing=false; bHiddenInGame=true; bIsEditorOnly=true;
}
FPrimitiveSceneProxy* UGuLiMapVisualizationComponent::CreateSceneProxy() { return new FGuLiMapSceneProxy(this); }
FBoxSphereBounds UGuLiMapVisualizationComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    FBox Box(FVector(-100),FVector(100));
    if (const auto* A=Cast<AGuLiMapMarker>(GetOwner())) for (const auto& R:A->Record.Regions) if (auto H=GuLiMap::FindGeometry(R.Geometry))
    {
        for (const auto& V:H->BuildMesh(R.Geometry).Vertices) Box+=R.GetTransform().TransformPosition(V);
    }
    return FBoxSphereBounds(Box).TransformBy(LocalToWorld);
}
