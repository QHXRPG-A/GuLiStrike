#include "Gameplay/Navigation/GuLiFactoryFloorNavigationComponent.h"
#include "AI/NavigationSystemHelpers.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "GameFramework/Actor.h"

UGuLiFactoryFloorNavigationComponent::UGuLiFactoryFloorNavigationComponent()
{
    PrimaryComponentTick.bCanEverTick=false;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetCustomNavigableGeometry(EHasCustomNavigableGeometry::EvenIfNotCollidable);
    SetCanEverAffectNavigation(true);
    SetVisibility(false);SetHiddenInGame(true);SetCastShadow(false);
}

bool UGuLiFactoryFloorNavigationComponent::InitializeFromFactory(AActor& Factory)
{
    check(IsInGameThread());Hulls.Reset();HullTransforms.Reset();WorldBounds=FBox(ForceInit);
    TArray<AActor*> Parts;Factory.GetAllChildActors(Parts,true);
    // The current factory's floor and approach ramp are the only collision
    // hulls below 60 cm with less than 50 cm height. Roof/lintel/walls are
    // excluded; their movement boundary is supplied by the authored circles.
    for (auto* Part:Parts)
    {
        TInlineComponentArray<UStaticMeshComponent*> Meshes(Part);
        for (const auto* Mesh:Meshes)
        {
            const UStaticMesh* Asset=Mesh->GetStaticMesh();const auto* Body=Asset?Asset->GetBodySetup():nullptr;
            if (!Body || Mesh->GetCollisionEnabled()==ECollisionEnabled::NoCollision) continue;
            for (const auto& Hull:Body->AggGeom.ConvexElems)
            {
                FBox LocalBounds(ForceInit),HullBounds(ForceInit);
                for (const auto& Vertex:Hull.VertexData)
                {
                    const FVector World=Mesh->GetComponentTransform().TransformPosition(Hull.GetTransform().TransformPosition(Vertex));
                    HullBounds+=World;LocalBounds+=Factory.GetActorTransform().InverseTransformPosition(World);
                }
                if (!LocalBounds.IsValid || LocalBounds.Min.Z<-.1 || LocalBounds.Max.Z>60 || LocalBounds.GetSize().Z>50) continue;
                Hulls.Add(Hull);HullTransforms.Add(Mesh->GetComponentTransform());WorldBounds+=HullBounds;
            }
        }
    }
    UpdateBounds();return !Hulls.IsEmpty();
}

FBoxSphereBounds UGuLiFactoryFloorNavigationComponent::CalcBounds(const FTransform&) const
{return WorldBounds.IsValid?FBoxSphereBounds(WorldBounds):FBoxSphereBounds(FVector::ZeroVector,FVector::ZeroVector,0);}

bool UGuLiFactoryFloorNavigationComponent::DoCustomNavigableGeometryExport(FNavigableGeometryExport& Export) const
{
    for (int32 I=0;I<Hulls.Num();++I) Export.ExportChaosConvexMesh(&Hulls[I],HullTransforms[I]);
    return false;
}
