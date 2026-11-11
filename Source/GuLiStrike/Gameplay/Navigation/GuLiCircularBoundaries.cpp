#include "Gameplay/Navigation/GuLiCircularBoundaries.h"
#include "Gameplay/Data/GuLiAvoidanceCatalog.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"

namespace GuLiCircleAvoidance
{
bool BuildBuildingCircles(const FGuLiAvoidanceCatalog& Catalog,int32 Definition,uint32 Instance,
    uint8 PhaseMask,const FTransform& ActorTransform,uint64 Version,TArray<FCircle>& Out)
{
    Out.Reset();const auto* Profiles=Catalog.Buildings.Find(Definition);
    if (!Profiles || !Instance || ActorTransform.ContainsNaN()) return false;
    const FVector Scale=ActorTransform.GetScale3D().GetAbs();
    for (const auto& Profile:*Profiles) if (Profile.PhaseMask&PhaseMask)
    {
        if (Profile.Index<0 || Profile.Index>MAX_uint16) return false;
        FCircle C;C.Id=BuildingNamespace|(uint64(Instance)<<16)|uint64(Profile.Index);
        C.Version=Version;C.Center=ActorTransform.TransformPosition(Profile.LocalCenter);
        C.Radius=Profile.Radius*FMath::Max(Scale.X,Scale.Y);
        // Building placement permits yaw only. Heights are actor-local, independent of the circle center's Z.
        C.MinimumZ=ActorTransform.GetLocation().Z+Profile.MinimumHeight*Scale.Z;
        C.MaximumZ=ActorTransform.GetLocation().Z+Profile.MaximumHeight*Scale.Z;
        C.LayerMask=3;Out.Add(C);
    }
    // A construction-only definition may have no circles after conversion.
    return true;
}

uint32 FOreShapeCache::StageKey(uint8 Resource,uint8 Family,uint8 Amount)
{return (uint32(Resource)<<16)|(uint32(Family)<<8)|Amount;}

bool FOreShapeCache::Initialize(const UGuLiResourceEconomyConfig& Config,FString& Error)
{
    Shapes.Reset();ModelByStage.Reset();Error.Reset();
    for (auto Type:{EGuLiResourceType::Blue,EGuLiResourceType::Red})
        for (uint8 Family=0;Family<4;++Family) for (uint8 Amount=1;Amount<=3;++Amount)
        {
            const auto* Visual=Config.FindOreVisual(Type,Family,GuLiResources::AmountToVisualStage(Amount));
            if (!Visual) {Error=TEXT("Current ore stage has no configured model");return false;}
            ModelByStage.Add(StageKey(uint8(Type),Family,Amount),Visual->ModelId);
            if (Shapes.Contains(Visual->ModelId)) continue;
            const auto* Mesh=GuLiModels::Load<UStaticMesh>(nullptr,Visual->ModelId);
            const auto* Body=Mesh?Mesh->GetBodySetup():nullptr;
            if (!Body) {Error=TEXT("Current ore stage has no source collision geometry");return false;}
            FShape Shape;
            for (const auto& Hull:Body->AggGeom.ConvexElems)
                for (const auto& Vertex:Hull.VertexData) Shape.Vertices.Add(Hull.GetTransform().TransformPosition(Vertex));
            if (Shape.Vertices.IsEmpty()) {Error=TEXT("Current ore stage has an empty source collision hull");return false;}
            Shapes.Add(Visual->ModelId,MoveTemp(Shape));
        }
    return true;
}

bool FOreShapeCache::BuildNode(const FGuLiResourceNodeDefinition& Node,uint8 Remaining,uint64 Version,FCircle& Out) const
{
    Out={};if (!Remaining || Remaining>Node.InitialAmount) return false;
    const auto* Model=ModelByStage.Find(StageKey(uint8(Node.ResourceType),Node.FamilyIndex,Remaining));
    const auto* Shape=Model?Shapes.Find(*Model):nullptr;if (!Shape) return false;
    FBox Bounds(ForceInit);
    for (const auto& Vertex:Shape->Vertices) Bounds+=Node.WorldTransform.TransformPosition(Vertex);
    if (!Bounds.IsValid) return false;
    Out.Id=OreNamespace|Node.NodeId;Out.Version=Version;Out.Center=Bounds.GetCenter();
    double RadiusSquared=0;
    for (const auto& Vertex:Shape->Vertices)
        RadiusSquared=FMath::Max(RadiusSquared,FVector::DistSquared2D(Node.WorldTransform.TransformPosition(Vertex),Out.Center));
    Out.Radius=FMath::Max(1.f,float(FMath::Sqrt(RadiusSquared))+.001f);
    Out.MinimumZ=float(Bounds.Min.Z);Out.MaximumZ=float(Bounds.Max.Z);Out.LayerMask=3;return true;
}

SIZE_T FOreShapeCache::GetAllocatedSize() const
{
    SIZE_T Bytes=Shapes.GetAllocatedSize()+ModelByStage.GetAllocatedSize();
    for (const auto& Pair:Shapes) Bytes+=Pair.Value.Vertices.GetAllocatedSize();
    return Bytes;
}
}
