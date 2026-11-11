#include "Gameplay/Resources/GuLiOreCollisionField.h"
#include "Gameplay/Navigation/GuLiCircleCollisionFiltering.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Models/GuLiModelRegistrySubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "AI/NavigationModifier.h"
#include "AI/Navigation/NavigationRelevantData.h"
#include "NavAreas/NavArea_Null.h"

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
    FString& OutError)
{
    check(IsInGameThread());
    for (const auto& Component:CollisionMeshes) Component->ClearInstances();
    for (auto& Ids:NodeIdsByInstance) Ids.Reset();
    NodeRefs.Init(FInstanceRef{},Definition.Nodes.Num()); AliveCount=0;
    InitialComponentByNode.Init(INDEX_NONE,Definition.Nodes.Num());
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
            Component->SetMaskFilterOnBodyInstance(GuLiCircleCollisionFiltering::CircularBoundaryMask);
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
        InitialComponentByNode[Node.NodeId-1]=ComponentIndex;
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

bool AGuLiOreCollisionFieldActor::RestoreNodes(TConstArrayView<uint32> NodeIds,const UGuLiResourceMapDefinition& Definition)
{
    check(IsInGameThread());
    for (uint32 Id:NodeIds)
    {
        if (!Id || !NodeRefs.IsValidIndex(Id-1) || !Definition.Nodes.IsValidIndex(Id-1)) return false;
        if (NodeRefs[Id-1].Component!=INDEX_NONE) continue;
        const int32 C=InitialComponentByNode[Id-1];if (!CollisionMeshes.IsValidIndex(C)) return false;
        const int32 I=CollisionMeshes[C]->AddInstance(Definition.Nodes[Id-1].WorldTransform,true);
        if (I!=NodeIdsByInstance[C].Num()) return false;
        NodeIdsByInstance[C].Add(Id);NodeRefs[Id-1]={C,I};++AliveCount;
    }
    return true;
}
