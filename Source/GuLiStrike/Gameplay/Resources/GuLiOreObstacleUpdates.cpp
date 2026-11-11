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
#include "Gameplay/Navigation/GuLiOreClusterGeometry.h"
#include "Gameplay/Resources/GuLiMiningVehicleManager.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"

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
        MiningVisibilityParams.Reset();
    }
    if (!OreCollisionField) { InitializationError=TEXT("Could not create ore collision field"); return false; }
    if (!OreCollisionField->InitializeNodes(*MapDefinition,*EconomyConfig,NodeRemaining,InitializationError)
        || !OreShapes.Initialize(*EconomyConfig,InitializationError)) return false;
    ClusterObstacles.SetNum(MapDefinition->Clusters.Num());
    for (int32 I=0;I<MapDefinition->Clusters.Num();++I)
    {
        if (!ClusterObstacles[I])
        {
            FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            ClusterObstacles[I]=GetWorld()->SpawnActor<AGuLiOreClusterObstacleActor>(AGuLiOreClusterObstacleActor::StaticClass(),
                FTransform(MapDefinition->Clusters[I].Center),Params);
            if (!ClusterObstacles[I]) {InitializationError=TEXT("Could not create ore circle exporter");return false;}
        }
        if (!RefreshOreClusterCircles(I)) return false;
    }
    return true;
}

bool UGuLiResourceWorldSubsystem::RefreshOreClusterCircles(int32 ClusterIndex)
{
    FGuLiPerformanceScope Timing(GetWorld(),TEXT("Movement.GT.OreClusterMs"));
    using namespace GuLiCircleAvoidance;
    const auto* Units=GetWorld()->GetSubsystem<UGuLiUnitDataSubsystem>();
    auto* Registry=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
    if (!Units || !Registry || !MapDefinition->Clusters.IsValidIndex(ClusterIndex)) return false;
    const auto& Catalog=Units->GetAvoidanceCatalog();const auto& Cluster=MapDefinition->Clusters[ClusterIndex];
    float MinimumRadius=FLT_MAX;TArray<FProtectedPassage> Entrances;
    auto* Mining=GetWorld()->GetSubsystem<UGuLiMiningVehicleManager>();
    for (const auto& Unit:Catalog.Units)
    {
        const float Radius=Unit.Value.Solver.Radius+Unit.Value.Solver.Clearance+Unit.Value.Solver.ContactMargin;
        MinimumRadius=FMath::Min(MinimumRadius,Radius);
        // Only already-cached entrances belonging to this changed cluster.
        if (Mining) for (const auto& Pose:Mining->GetClusterSlotPoses(Cluster.ClusterId,Unit.Key))
        {auto& P=Entrances.AddDefaulted_GetRef();P.From=P.To=Pose.GetLocation();P.Radius=Radius;P.MinimumZ=P.From.Z;P.MaximumZ=P.From.Z+300;}
    }
    TArray<FCircle> Circles;FClusteringCounters Counts;
    if (!BuildCurrentOreCluster(OreShapes,*MapDefinition,Cluster,NodeRemaining,++OreCircleVersion,
        Catalog.OreMaximumRadiusRatio,Catalog.OreMaximumAreaRatio,MinimumRadius,Entrances,Circles,Counts))
    {InitializationError=TEXT("Could not derive current ore circles");return false;}
    if (!Registry->ReplaceCircleGroup(OreNamespace|Cluster.ClusterId,Circles,true)) return false;
    ClusterObstacles[ClusterIndex]->SetCircles(Cluster.ClusterId,Circles);
    if (auto* Capture=GetWorld()->GetSubsystem<UGuLiPerformanceSubsystem>();Capture && Capture->IsCapturing())
    {Capture->Record(TEXT("Ore.ClusterUpdates"),1);Capture->Record(TEXT("Ore.MemberPairChecks"),Counts.MemberPairChecks);
     Capture->Record(TEXT("Ore.Merges"),Counts.Merges);Capture->Record(TEXT("Ore.PassageRejections"),Counts.PassageRejections);}
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
    TArray<uint32> Updated;Updated.Reserve(Limit);
    TSet<int32> PublishedClusters;
    const double OldestWait=(Started-PendingOreRemovals[OreRemovalHead].QueuedAt)*1000.;
    do
    {
        TArray<uint32> Removed,Restored;TSet<int32> ChangedClusters;
        int32 Slice=0;
        while (OreRemovalHead<PendingOreRemovals.Num() && Slice<FMath::Min(8,Limit-Updated.Num()))
        {
            const auto Entry=PendingOreRemovals[OreRemovalHead++];QueuedOreNodes.Remove(Entry.NodeId);++Slice;
            if (Entry.Generation!=OreWorldGeneration || !NodeRemaining.IsValidIndex(Entry.NodeId-1)) continue;
            (NodeRemaining[Entry.NodeId-1]?Restored:Removed).Add(Entry.NodeId);
            ChangedClusters.Add(MapDefinition->Nodes[Entry.NodeId-1].ClusterId-1);Updated.Add(Entry.NodeId);
        }
        bool Valid=OreCollisionField->RemoveNodes(Removed) && OreCollisionField->RestoreNodes(Restored,*MapDefinition);
        for (int32 Cluster:ChangedClusters) if (!PublishedClusters.Contains(Cluster))
        {PublishedClusters.Add(Cluster);Valid=RefreshOreClusterCircles(Cluster) && Valid;}
        if (!Valid)
        {
            bFatalInitializationError=true;InitializationError=TEXT("Ore circle/collision publication failed; query scene suspended");
            if (auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>()) Hybrid->Suspend(InitializationError);
            UE_LOG(LogTemp,Error,TEXT("%s"),*InitializationError);return;
        }
    }
    while (OreRemovalHead<PendingOreRemovals.Num() && Updated.Num()<Limit && FPlatformTime::Seconds()-Started<Budget);

    LastOreBatchNodes=Updated.Num(); LastOreBatchMilliseconds=(FPlatformTime::Seconds()-Started)*1000.;
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
        for (uint8 Amount:NodeRemaining) Result.AliveAvoidanceNodes+=Amount>0;
        Result.IndexPublications=Registry->GetOrePublicationCount();
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
