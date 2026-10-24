#include "Gameplay/Resources/GuLiOreCollisionField.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "AI/NavigationModifier.h"
#include "AI/Navigation/NavigationRelevantData.h"
#include "NavAreas/NavArea_Null.h"

UGuLiOreNavigationComponent::UGuLiOreNavigationComponent()
{
    bAttachToOwnersRoot=false;
    bNavigationRelevant=false;
}
void UGuLiOreNavigationComponent::InitializeNodes(TConstArrayView<FGuLiOreNodeObstacle> InNodes,TConstArrayView<uint8> Remaining)
{
    check(IsInGameThread()); check(InNodes.Num()<=32);
    Nodes.Reset(InNodes.Num()); Nodes.Append(InNodes.GetData(),InNodes.Num()); AliveMask=0;
    for (int32 I=0;I<Nodes.Num();++I)
        if (Remaining.IsValidIndex(Nodes[I].NodeId-1) && Remaining[Nodes[I].NodeId-1]>0) AliveMask|=1u<<I;
    Publish();
}
void UGuLiOreNavigationComponent::RemoveNodes(TConstArrayView<uint32> NodeIds)
{
    check(IsInGameThread());
    const uint32 Previous=AliveMask;
    for (int32 I=0;I<Nodes.Num();++I) if (NodeIds.Contains(Nodes[I].NodeId)) AliveMask&=~(1u<<I);
    if (Previous!=AliveMask) Publish();
}
void UGuLiOreNavigationComponent::SetNodesEnabled(bool bEnabled)
{
    check(IsInGameThread());
    AliveMask=bEnabled ? uint32((uint64(1)<<Nodes.Num())-1) : 0;
    Publish();
}
int32 UGuLiOreNavigationComponent::GetAliveCount() const
{
    return FPlatformMath::CountBits(AliveMask);
}
void UGuLiOreNavigationComponent::Publish()
{
    // Single octree update per changed cluster. Recast owns subsequent asynchronous tile generation.
    bNavigationRelevant=AliveMask!=0;
    UpdateNavigationBounds();
    RefreshNavigationModifiers();
}
void UGuLiOreNavigationComponent::CalcAndCacheBounds() const
{
    Bounds=FBox(ForceInit);
    for (const auto& Node:Nodes)
        Bounds+=FBox::BuildAABB(Node.Obstacle.Location,FVector(Node.Obstacle.RadiusCentimeters,
            Node.Obstacle.RadiusCentimeters,Node.HalfHeight));
}
void UGuLiOreNavigationComponent::GetNavigationData(FNavigationRelevantData& Data) const
{
    for (int32 I=0;I<Nodes.Num();++I) if (AliveMask&(1u<<I))
    {
        const auto& Node=Nodes[I];
        FAreaNavModifier Area(Node.Obstacle.RadiusCentimeters,Node.HalfHeight*2.f,
            FTransform(Node.Obstacle.Location),UNavArea_Null::StaticClass());
        Area.SetIncludeAgentHeight(true);
        Data.Modifiers.Add(Area);
    }
}

AGuLiOreCollisionFieldActor::AGuLiOreCollisionFieldActor()
{
    bReplicates=false;
    PrimaryActorTick.bCanEverTick=false;
    SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent=SceneRoot;
    SetActorHiddenInGame(true);
}

bool AGuLiOreCollisionFieldActor::InitializeNodes(const UGuLiResourceMapDefinition& Definition,
    const UGuLiResourceEconomyConfig& Config,TConstArrayView<uint8> Remaining,
    TArray<FGuLiOreNodeObstacle>& OutShapes,FString& OutError)
{
    check(IsInGameThread());
    for (const auto& Component:CollisionMeshes) Component->ClearInstances();
    for (auto& Ids:NodeIdsByInstance) Ids.Reset();
    NodeRefs.Init(FInstanceRef{},Definition.Nodes.Num()); AliveCount=0;
    OutShapes.Reset(Definition.Nodes.Num());
    for (const auto& Node:Definition.Nodes)
    {
        const auto* Visual=Config.FindOreVisual(Node.ResourceType,Node.FamilyIndex,
            GuLiResources::AmountToVisualStage(Node.InitialAmount));
        if (!Visual || !Remaining.IsValidIndex(Node.NodeId-1))
        { OutError=TEXT("Invalid initial ore model or node remaining state"); return false; }
        int32 ComponentIndex=ComponentByModel.FindRef(Visual->ModelId);
        if (!ComponentByModel.Contains(Visual->ModelId))
        {
            auto* Mesh=GuLiModels::Load<UStaticMesh>(this,Visual->ModelId);
            const auto* Body=Mesh?Mesh->GetBodySetup():nullptr;
            if (!Body || Body->AggGeom.ConvexElems.IsEmpty() || CollisionMeshes.Num()>=24
                || Body->GetCollisionTraceFlag()==CTF_UseComplexAsSimple)
            { OutError=FString::Printf(TEXT("Ore ModelId %d requires simple convex collision (max 24 models)"),Visual->ModelId); return false; }
            auto* Component=NewObject<UInstancedStaticMeshComponent>(this,
                FName(*FString::Printf(TEXT("OreCollision_%d"),Visual->ModelId)));
            Component->SetupAttachment(SceneRoot);
            Component->SetMobility(EComponentMobility::Movable);
            Component->SetStaticMesh(Mesh);
            Component->SetCanEverAffectNavigation(false);
            Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            Component->SetCollisionObjectType(ECC_WorldStatic);
            Component->SetCollisionResponseToAllChannels(ECR_Block);
            Component->SetGenerateOverlapEvents(false);
            Component->SetVisibility(false);
            Component->SetHiddenInGame(true);
            Component->SetCastShadow(false);
            Component->SetRemoveSwap();
            AddInstanceComponent(Component); Component->RegisterComponent();
            ComponentIndex=CollisionMeshes.Add(Component);
            ComponentByModel.Add(Visual->ModelId,ComponentIndex);
            NodeIdsByInstance.AddDefaulted();
        }
        auto* Component=CollisionMeshes[ComponentIndex].Get();
        const auto* Body=Component->GetStaticMesh()->GetBodySetup();
        TArray<FVector> Vertices; FBox Bounds(ForceInit);
        for (const auto& Convex:Body->AggGeom.ConvexElems)
            for (const FVector& Vertex:Convex.VertexData)
            {
                const FVector World=Node.WorldTransform.TransformPosition(Convex.GetTransform().TransformPosition(Vertex));
                Vertices.Add(World); Bounds+=World;
            }
        if (!Bounds.IsValid || Vertices.IsEmpty())
        { OutError=FString::Printf(TEXT("Ore ModelId %d has no usable convex vertices"),Visual->ModelId); return false; }
        FGuLiOreNodeObstacle Shape; Shape.NodeId=Node.NodeId; Shape.ClusterId=Node.ClusterId;
        Shape.Obstacle.Handle.Value=0x80000000u|Node.NodeId;
        Shape.Obstacle.Location=Bounds.GetCenter();
        double RadiusSquared=0;
        for (const FVector& Vertex:Vertices)
            RadiusSquared=FMath::Max(RadiusSquared,FVector::DistSquared2D(Vertex,Shape.Obstacle.Location));
        Shape.Obstacle.RadiusCentimeters=FMath::Max(1.f,float(FMath::Sqrt(RadiusSquared)));
        Shape.HalfHeight=FMath::Max(1.f,float(Bounds.GetExtent().Z)); Shape.Obstacle.HalfHeight=Shape.HalfHeight;
        OutShapes.Add(Shape);
        if (Remaining[Node.NodeId-1]==0) continue;
        const int32 Instance=Component->AddInstance(Node.WorldTransform,true);
        if (Instance==INDEX_NONE) { OutError=TEXT("Could not create an ore collision instance"); return false; }
        check(Instance==NodeIdsByInstance[ComponentIndex].Num());
        NodeIdsByInstance[ComponentIndex].Add(Node.NodeId);
        NodeRefs[Node.NodeId-1]={ComponentIndex,Instance}; ++AliveCount;
    }
    return true;
}

bool AGuLiOreCollisionFieldActor::RemoveNodes(TConstArrayView<uint32> NodeIds)
{
    check(IsInGameThread());
    TMap<int32,TArray<int32>> ByComponent;
    for (uint32 Id:NodeIds) if (Id && NodeRefs.IsValidIndex(Id-1))
    {
        const auto& Ref=NodeRefs[Id-1];
        if (Ref.Component!=INDEX_NONE) ByComponent.FindOrAdd(Ref.Component).AddUnique(Ref.Instance);
    }
    for (auto& Pair:ByComponent)
    {
        Pair.Value.Sort(TGreater<int32>());
        if (!CollisionMeshes[Pair.Key]->RemoveInstances(Pair.Value,true)) return false;
        auto& Ids=NodeIdsByInstance[Pair.Key];
        // Mirror ISM's descending RemoveAtSwap exactly, including a moved node removed later in this batch.
        for (int32 Index:Pair.Value)
        {
            const uint32 Removed=Ids[Index], Moved=Ids.Last();
            Ids.RemoveAtSwap(Index,1,EAllowShrinking::No);
            if (Ids.IsValidIndex(Index)) NodeRefs[Moved-1].Instance=Index;
            NodeRefs[Removed-1]=FInstanceRef{}; --AliveCount;
        }
    }
    return true;
}
bool AGuLiOreCollisionFieldActor::ValidateInstanceIndexMap() const
{
    int32 Count=0;
    for (int32 C=0;C<CollisionMeshes.Num();++C)
    {
        if (CollisionMeshes[C]->GetInstanceCount()!=NodeIdsByInstance[C].Num()) return false;
        for (int32 I=0;I<NodeIdsByInstance[C].Num();++I)
        {
            const uint32 Id=NodeIdsByInstance[C][I];
            if (!Id || !NodeRefs.IsValidIndex(Id-1) || NodeRefs[Id-1].Component!=C || NodeRefs[Id-1].Instance!=I) return false;
            ++Count;
        }
    }
    return Count==AliveCount;
}
