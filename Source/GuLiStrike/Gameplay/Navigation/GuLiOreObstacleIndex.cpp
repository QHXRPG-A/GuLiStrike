#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Gameplay/Navigation/Grid/GuLiGroundGridSubsystem.h"
#include "Engine/World.h"

namespace
{
FGuLiHybridObjectKey OreGridKey(uint32 NodeId)
{
    return {EGuLiHybridObjectKind::ResourceNode,FGuid(0x47524944,0x4F425354,0,0x80000000u|NodeId),1};
}
FBox OreBounds(const FGuLiOreNodeObstacle& Node)
{
    return FBox::BuildAABB(Node.Obstacle.Location,
        FVector(Node.Obstacle.RadiusCentimeters,Node.Obstacle.RadiusCentimeters,Node.HalfHeight));
}
void AddOreGridContribution(UGuLiGroundGridSubsystem* Grid,const FGuLiOreNodeObstacle& Node)
{
    if (!Grid || !Grid->IsReady()) return;
    const auto Snapshot=Grid->Snapshot();
    const FVector2D Center(Node.Obstacle.Location), Radius(Node.Obstacle.RadiusCentimeters);
    const FVector2D Min=(Center-Radius-Snapshot->Origin)/GuLiGrid::CellSize;
    const FVector2D Max=(Center+Radius-Snapshot->Origin)/GuLiGrid::CellSize;
    Grid->SetStaticObstacle(OreGridKey(Node.NodeId),
        {{FMath::FloorToInt32(Min.X),FMath::FloorToInt32(Min.Y)},
         {FMath::FloorToInt32(Max.X)+1,FMath::FloorToInt32(Max.Y)+1}});
}
}

const FGuLiOreNodeObstacle* FGuLiOreObstacleIndex::Find(uint32 NodeId) const
{
    if (!NodeId || !Topology || !Topology->NodeAddresses.IsValidIndex(NodeId-1)) return nullptr;
    const FIntPoint Address=Topology->NodeAddresses[NodeId-1];
    if (!Chunks.IsValidIndex(Address.X) || !Chunks[Address.X]
        || !Chunks[Address.X]->Nodes.IsValidIndex(Address.Y)) return nullptr;
    const auto& Chunk=*Chunks[Address.X];
    return (Chunk.AliveMask&(1u<<Address.Y)) ? &Chunk.Nodes[Address.Y] : nullptr;
}

void FGuLiOreObstacleIndex::Query(const FVector& Position,float Radius,TArray<uint32>& Out) const
{
    Out.Reset();
    if (!Topology || !AliveCount) return;
    const auto Min=FGuLiDynamicObstacleSnapshot::Cell(Position-FVector(Radius,Radius,0));
    const auto Max=FGuLiDynamicObstacleSnapshot::Cell(Position+FVector(Radius,Radius,0));
    TArray<int32,TInlineAllocator<16>> Nearby;
    if ((int64(Max.X)-Min.X+1)*(int64(Max.Y)-Min.Y+1)>Topology->Cells.Num())
    {
        for (int32 I=0;I<Chunks.Num();++I) Nearby.Add(I);
    }
    else
    {
        for (int32 X=Min.X;X<=Max.X;++X) for (int32 Y=Min.Y;Y<=Max.Y;++Y)
            if (const auto* Bucket=Topology->Cells.Find({X,Y}))
                for (int32 I:*Bucket) Nearby.AddUnique(I);
    }
    for (int32 I:Nearby)
    {
        if (!Chunks[I] || !Chunks[I]->AliveMask) continue;
        const auto& Chunk=*Chunks[I];
        for (int32 J=0;J<Chunk.Nodes.Num();++J)
        {
            const auto& Node=Chunk.Nodes[J];
            if ((Chunk.AliveMask&(1u<<J)) && FVector::DistSquared2D(Position,Node.Obstacle.Location)
                <=FMath::Square(double(Radius)+Node.Obstacle.RadiusCentimeters)) Out.Add(Node.NodeId);
        }
    }
}

void UGuLiDynamicObstacleRegistrySubsystem::InitializeOreObstacles(
    TConstArrayView<FGuLiOreNodeObstacle> Nodes,TConstArrayView<uint8> Remaining)
{
    check(IsInGameThread());
    auto* Grid=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();
    const UGuLiGroundGridSubsystem::FScopedObstacleBatch GridBatch(Grid);
    ClearOreObstacles();
    auto Next=MakeShared<FGuLiOreObstacleIndex,ESPMode::ThreadSafe>();
    auto Topology=MakeShared<FGuLiOreObstacleTopology,ESPMode::ThreadSafe>();
    Topology->NodeAddresses.Init(FIntPoint(INDEX_NONE,INDEX_NONE),Remaining.Num());
    TArray<TSharedPtr<FGuLiOreObstacleChunk,ESPMode::ThreadSafe>> Chunks;
    for (const auto& Node:Nodes)
    {
        check(Node.NodeId>0 && Node.NodeId<0x80000000u && Remaining.IsValidIndex(Node.NodeId-1) && Node.ClusterId>0);
        const int32 Cluster=Node.ClusterId-1;
        if (Chunks.Num()<=Cluster) Chunks.SetNum(Cluster+1);
        if (!Chunks[Cluster]) Chunks[Cluster]=MakeShared<FGuLiOreObstacleChunk,ESPMode::ThreadSafe>();
        auto& Chunk=*Chunks[Cluster];
        const int32 Local=Chunk.Nodes.Add(Node);
        check(Local<32);
        Chunk.Nodes[Local].Obstacle.Handle.Value=0x80000000u|Node.NodeId;
        Topology->NodeAddresses[Node.NodeId-1]={Cluster,Local};
        const FBox Bounds=OreBounds(Node);
        const auto Min=FGuLiDynamicObstacleSnapshot::Cell(Bounds.Min),Max=FGuLiDynamicObstacleSnapshot::Cell(Bounds.Max);
        for (int32 X=Min.X;X<=Max.X;++X) for (int32 Y=Min.Y;Y<=Max.Y;++Y)
            Topology->Cells.FindOrAdd({X,Y}).AddUnique(Cluster);
        if (Remaining[Node.NodeId-1]>0)
        {
            Chunk.AliveMask|=1u<<Local; ++Next->AliveCount;
            AddOreGridContribution(Grid,Node);
            DirtyObstacleCells(Node.Obstacle);
        }
    }
    for (const auto& Chunk:Chunks)
    {
        Next->Chunks.Add(Chunk);
        FBox Bounds(ForceInit);
        if (Chunk) for (int32 I=0;I<Chunk->Nodes.Num();++I)
            if (Chunk->AliveMask&(1u<<I)) Bounds+=OreBounds(Chunk->Nodes[I]);
        if (Bounds.IsValid) NotifyStaticGeometryChanged(Bounds.ExpandBy(FVector(0,0,100000)));
    }
    Next->Topology=Topology; Next->Revision=++OrePublications;
    OreIndex=Next; PublishChange();
}

void UGuLiDynamicObstacleRegistrySubsystem::RemoveOreObstacles(TConstArrayView<uint32> NodeIds)
{
    check(IsInGameThread());
    if (!OreIndex || NodeIds.IsEmpty()) return;
    auto* Grid=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();
    const UGuLiGroundGridSubsystem::FScopedObstacleBatch GridBatch(Grid);
    auto Next=MakeShared<FGuLiOreObstacleIndex,ESPMode::ThreadSafe>(*OreIndex);
    TMap<int32,TSharedPtr<FGuLiOreObstacleChunk,ESPMode::ThreadSafe>> Changed;
    TMap<int32,FBox> DirtyBounds;
    for (uint32 Id:NodeIds)
    {
        const auto* Node=OreIndex->Find(Id); if (!Node) continue;
        const FIntPoint Address=OreIndex->Topology->NodeAddresses[Id-1];
        auto& Chunk=Changed.FindOrAdd(Address.X);
        if (!Chunk) Chunk=MakeShared<FGuLiOreObstacleChunk,ESPMode::ThreadSafe>(*OreIndex->Chunks[Address.X]);
        if (!(Chunk->AliveMask&(1u<<Address.Y))) continue;
        Chunk->AliveMask&=~(1u<<Address.Y); --Next->AliveCount;
        if (Grid) Grid->RemoveStaticObstacle(OreGridKey(Id));
        DirtyObstacleCells(Node->Obstacle);
        auto* Bounds=DirtyBounds.Find(Address.X);
        if (Bounds) *Bounds+=OreBounds(*Node); else DirtyBounds.Add(Address.X,OreBounds(*Node));
    }
    if (Changed.IsEmpty()) return;
    for (const auto& Pair:Changed) Next->Chunks[Pair.Key]=Pair.Value;
    Next->Revision=++OrePublications; OreIndex=Next;
    bOreNavigationFinished=false;
    for (const auto& Pair:DirtyBounds)
    {
        const FBox Bounds=Pair.Value.ExpandBy(FVector(0,0,100000));
        if (auto* Pending=PendingOreNavigationRegions.Find(Pair.Key)) *Pending+=Bounds;
        else PendingOreNavigationRegions.Add(Pair.Key,Bounds);
        NotifyStaticGeometryOpened(Bounds);
    }
    PublishChange();
}

void UGuLiDynamicObstacleRegistrySubsystem::ClearOreObstacles()
{
    check(IsInGameThread());
    PendingOreNavigationRegions.Reset();
    bOreNavigationFinished=false;
    if (!OreIndex) return;
    auto* Grid=GetWorld()->GetSubsystem<UGuLiGroundGridSubsystem>();
    const UGuLiGroundGridSubsystem::FScopedObstacleBatch GridBatch(Grid);
    for (const auto& Chunk:OreIndex->Chunks)
    {
        if (!Chunk) continue;
        FBox Bounds(ForceInit);
        for (int32 I=0;I<Chunk->Nodes.Num();++I) if (Chunk->AliveMask&(1u<<I))
        {
            const auto& Node=Chunk->Nodes[I];
            if (Grid) Grid->RemoveStaticObstacle(OreGridKey(Node.NodeId));
            DirtyObstacleCells(Node.Obstacle); Bounds+=OreBounds(Node);
        }
        if (Bounds.IsValid) NotifyStaticGeometryChanged(Bounds.ExpandBy(FVector(0,0,100000)));
    }
    OreIndex.Reset(); ++OrePublications; PublishChange();
}
