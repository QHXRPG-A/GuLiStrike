#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceActors.h"
#include "Gameplay/Resources/GuLiOreCollisionField.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "NavigationSystem.h"

bool UGuLiResourceWorldSubsystem::RequestOreObstacleResync()
{
    check(IsInGameThread());
    const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!Hybrid || !Hybrid->IsWorldReady() || !bHybridQuerySceneReady || bFatalInitializationError) return false;
    HandleHybridResourceDirectory();
    return true;
}

bool UGuLiResourceWorldSubsystem::InitializeNodeObstacles()
{
    check(IsInGameThread());
    ResetPendingOreRemovals();
    auto* Registry=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
    if (!Registry) { InitializationError=TEXT("Missing ore obstacle registry"); return false; }
    if (!OreCollisionField)
    {
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        OreCollisionField=GetWorld()->SpawnActor<AGuLiOreCollisionFieldActor>(
            AGuLiOreCollisionFieldActor::StaticClass(),FTransform::Identity,Params);
    }
    if (!OreCollisionField) { InitializationError=TEXT("Could not create ore collision field"); return false; }
    TArray<FGuLiOreNodeObstacle> Shapes;
    if (!OreCollisionField->InitializeNodes(*MapDefinition,*EconomyConfig,NodeRemaining,Shapes,InitializationError)) return false;
    TArray<TArray<FGuLiOreNodeObstacle>> ByCluster; ByCluster.SetNum(MapDefinition->Clusters.Num());
    for (const auto& Shape:Shapes) ByCluster[Shape.ClusterId-1].Add(Shape);
    ClusterObstacles.SetNum(MapDefinition->Clusters.Num());
    for (int32 I=0;I<MapDefinition->Clusters.Num();++I)
    {
        const auto& Cluster=MapDefinition->Clusters[I];
        const FTransform Transform(FRotator::ZeroRotator,Cluster.Center);
        if (!ClusterObstacles[I])
        {
            auto* Proxy=GetWorld()->SpawnActorDeferred<AGuLiOreClusterObstacleActor>(
                AGuLiOreClusterObstacleActor::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (!Proxy) { InitializationError=TEXT("Could not create ore navigation exporter"); return false; }
            Proxy->InitializeNodes(Cluster.ClusterId,ByCluster[I],NodeRemaining);
            Proxy->FinishSpawning(Transform); ClusterObstacles[I]=Proxy;
        }
        else ClusterObstacles[I]->InitializeNodes(Cluster.ClusterId,ByCluster[I],NodeRemaining);
    }
    Registry->InitializeOreObstacles(Shapes,NodeRemaining);
    return true;
}

void UGuLiResourceWorldSubsystem::ResetPendingOreRemovals()
{
    check(IsInGameThread());
    ++OreWorldGeneration;
    PendingOreRemovals.Reset(); QueuedOreNodes.Reset(); OreRemovalHead=0; OreBatchDueAt=0;
    LastOreBatchNodes=0; LastOreBatchMilliseconds=0;
}

void UGuLiResourceWorldSubsystem::ProcessPendingOreRemovals()
{
    check(IsInGameThread());
    const double Started=FPlatformTime::Seconds();
    auto* Capture=GetWorld()->GetSubsystem<UGuLiPerformanceSubsystem>();
    if (Capture && Capture->IsCapturing())
    {
        Capture->Record(TEXT("Ore.PendingNodes"),QueuedOreNodes.Num());
        Capture->Record(TEXT("Ore.WorldGeneration"),double(OreWorldGeneration));
        if (const auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
        {
            Capture->Record(TEXT("Ore.NavRemainingTasks"),Nav->GetNumRemainingBuildTasks());
            Capture->Record(TEXT("Ore.NavRunningTasks"),Nav->GetNumRunningBuildTasks());
            Capture->Record(TEXT("Ore.NavDirty"),Nav->HasDirtyAreasQueued()?1:0);
        }
    }
    if (QueuedOreNodes.IsEmpty() || Started<OreBatchDueAt || !OreCollisionField) return;
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiResources_RemoveOreObstacles);
    auto* Registry=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
    if (!Registry) return;
    const int32 Limit=FMath::Clamp(OreBatchMaxNodesPerFrame,1,64);
    const double Budget=FMath::Max(0.f,OreBatchSoftBudgetMilliseconds)*0.001;
    TArray<uint32> Removed; Removed.Reserve(Limit);
    TMap<int32,TArray<uint32>> ByCluster;
    const double OldestWait=(Started-PendingOreRemovals[OreRemovalHead].QueuedAt)*1000.;
    do
    {
        // Small FIFO slices bound physical work before checking the soft budget. Each ISM gets a batch call.
        TArray<uint32> Slice; Slice.Reserve(8);
        while (OreRemovalHead<PendingOreRemovals.Num() && Slice.Num()<FMath::Min(8,Limit-Removed.Num()))
        {
            const auto Entry=PendingOreRemovals[OreRemovalHead++]; QueuedOreNodes.Remove(Entry.NodeId);
            if (Entry.Generation==OreWorldGeneration && NodeRemaining.IsValidIndex(Entry.NodeId-1)
                && NodeRemaining[Entry.NodeId-1]==0) Slice.Add(Entry.NodeId);
        }
        if (!OreCollisionField->RemoveNodes(Slice))
        {
            bFatalInitializationError=true; InitializationError=TEXT("Ore collision instance removal failed; query scene suspended");
            if (auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()) Hybrid->Suspend(InitializationError);
            UE_LOG(LogTemp,Error,TEXT("%s"),*InitializationError);
            return;
        }
        for (uint32 Id:Slice) ByCluster.FindOrAdd(MapDefinition->Nodes[Id-1].ClusterId-1).Add(Id);
        Removed.Append(Slice);
    }
    while (OreRemovalHead<PendingOreRemovals.Num() && Removed.Num()<Limit && FPlatformTime::Seconds()-Started<Budget);

    // Commit every selected node consistently, even if this final atomic publication exceeds the soft budget.
    Registry->RemoveOreObstacles(Removed);
    for (const auto& Pair:ByCluster)
        if (ClusterObstacles.IsValidIndex(Pair.Key) && ClusterObstacles[Pair.Key]) ClusterObstacles[Pair.Key]->RemoveNodes(Pair.Value);
    LastOreBatchNodes=Removed.Num(); LastOreBatchMilliseconds=(FPlatformTime::Seconds()-Started)*1000.;
    if (Capture && Capture->IsCapturing())
    {
        Capture->Record(TEXT("Ore.BatchMainThreadMs"),LastOreBatchMilliseconds);
        Capture->Record(TEXT("Ore.BatchNodes"),LastOreBatchNodes);
        Capture->Record(TEXT("Ore.OldestBatchWaitMs"),OldestWait);
    }
    if (OreRemovalHead==PendingOreRemovals.Num())
    {
        PendingOreRemovals.Reset(); OreRemovalHead=0; OreBatchDueAt=0;
    }
    // A partial batch retains its original deadline; neither arrivals nor the budget reset its timer.
}

FGuLiOreObstacleStats UGuLiResourceWorldSubsystem::GetOreObstacleStats() const
{
    check(IsInGameThread());
    FGuLiOreObstacleStats Result;
    Result.PendingNodes=QueuedOreNodes.Num(); Result.LastBatchNodes=LastOreBatchNodes;
    Result.LastBatchMilliseconds=LastOreBatchMilliseconds; Result.WorldGeneration=OreWorldGeneration;
    if (PendingOreRemovals.IsValidIndex(OreRemovalHead))
        Result.OldestWaitMilliseconds=FMath::Max(0.,(FPlatformTime::Seconds()-PendingOreRemovals[OreRemovalHead].QueuedAt)*1000.);
    Result.AliveCollisionNodes=OreCollisionField?OreCollisionField->GetAliveCount():0;
    if (const auto* Registry=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>())
    {
        Result.AliveAvoidanceNodes=Registry->GetAliveOreCount(); Result.IndexPublications=Registry->GetOrePublicationCount();
    }
    return Result;
}
FString UGuLiResourceWorldSubsystem::GetOreObstacleDiagnostics() const
{
    const auto S=GetOreObstacleStats();
    const auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    return FString::Printf(TEXT("Ore obstacles generation=%llu pending=%d oldest_ms=%.2f last_batch_nodes=%d last_batch_gt_ms=%.3f collision_alive=%d avoidance_alive=%d ore_index_publications=%llu instance_map=%s nav_remaining=%d nav_running=%d nav_dirty=%d"),
        S.WorldGeneration,S.PendingNodes,S.OldestWaitMilliseconds,S.LastBatchNodes,S.LastBatchMilliseconds,
        S.AliveCollisionNodes,S.AliveAvoidanceNodes,S.IndexPublications,
        !OreCollisionField || OreCollisionField->ValidateInstanceIndexMap()?TEXT("valid"):TEXT("INVALID"),
        Nav?Nav->GetNumRemainingBuildTasks():0,Nav?Nav->GetNumRunningBuildTasks():0,Nav&&Nav->HasDirtyAreasQueued()?1:0);
}
