// Opt-in player scene preparation. Ore changes still require real committed Hybrid extraction.
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
namespace
{
bool IsOreReviewWorld(UWorld* World)
{
    return World && World->IsGameWorld() && World->GetMapName().EndsWith(TEXT("LVL_CommanderMassPrototype"));
}
// Explicit local projection cost probe. This never fabricates a Hybrid commit or
// changes the resource ledger. Resync from that ledger after each measurement.
FAutoConsoleCommandWithWorldAndArgs ProbeOreProjection(TEXT("gs.Resources.OreReview.PerfProbe"),
    TEXT("Development-only local projection cost probe: node count [0..1024], delay seconds [1..30]. Requires an active performance capture. Run OreReview.Resync afterwards. This is NOT a mining/Hybrid transaction test."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
    {
        if (!IsOreReviewWorld(World) || Args.Num()!=2 || !Args[0].IsNumeric() || !Args[1].IsNumeric()) return;
        const int32 Count=FCString::Atoi(*Args[0]); const float Delay=FCString::Atof(*Args[1]);
        auto* Resources=World->GetSubsystem<UGuLiResourceWorldSubsystem>();
        auto* Capture=World->GetSubsystem<UGuLiPerformanceSubsystem>();
        if (Count<0 || Count>1024 || !FMath::IsFinite(Delay) || Delay<1 || Delay>30 || !Resources
            || !Resources->IsHybridQuerySceneReady() || !Capture || !Capture->IsCapturing()) return;
        const auto Stats=Resources->GetOreObstacleStats();
        if (Stats.PendingNodes || Resources->HasFatalInitializationError()) return;
        const auto* Definition=Resources->GetMapDefinition(); if (!Definition) return;
        TArray<uint32> Nodes;
        for (const auto& Node:Definition->Nodes)
            if (Resources->GetNodeRemainingRaw(Node.NodeId)>0) Nodes.Add(Node.NodeId);
        // Central deposits stay away from the two opening automatic mining sites.
        // Rendering uses the same fixed saved camera in both variants.
        const FVector Anchor=FVector::ZeroVector;
        Nodes.Sort([Definition,Anchor](uint32 A,uint32 B)
        {
            const auto& NA=Definition->Nodes[A-1]; const auto& NB=Definition->Nodes[B-1];
            const double DA=FVector::DistSquared2D(Definition->Clusters[NA.ClusterId-1].Center,Anchor);
            const double DB=FVector::DistSquared2D(Definition->Clusters[NB.ClusterId-1].Center,Anchor);
            return DA==DB?A<B:DA<DB;
        });
        if (Nodes.Num()<Count) return;
        Nodes.SetNum(Count,EAllowShrinking::No);
        FTimerHandle Handle;
        World->GetTimerManager().SetTimer(Handle,FTimerDelegate::CreateWeakLambda(Resources,
            [Resources,Nodes=MoveTemp(Nodes),Generation=Stats.WorldGeneration]
        {
            auto* Perf=Resources->GetWorld()->GetSubsystem<UGuLiPerformanceSubsystem>();
            if (!Perf || !Perf->IsCapturing() || Resources->GetOreObstacleStats().WorldGeneration!=Generation) return;
            auto* State=Resources->GetResourceWorldState(); if (!State) return;
            const double Started=FPlatformTime::Seconds(); int32 Applied=0;
            for (uint32 Id:Nodes) if (State->ApplyHybridNodeRemaining(Id,0)) ++Applied;
            Perf->Record(TEXT("Ore.ProbeDispatchMs"),(FPlatformTime::Seconds()-Started)*1000.);
            Perf->Record(TEXT("Ore.ProbeAppliedNodes"),Applied);
            Perf->Record(TEXT("Ore.ProbeRequestedNodes"),Nodes.Num());
        }),Delay,false);
        UE_LOG(LogTemp,Display,TEXT("Ore local projection cost probe armed nodes=%d delay=%.1f; no Hybrid ledger change; resync required after capture"),Count,Delay);
    }));
FAutoConsoleCommandWithWorldAndArgs FocusOreReview(TEXT("gs.Resources.OreReview.Focus"),
    TEXT("View saved ore review camera: single, channel, boundary, batch; player restores the player's camera."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
    {
        if (!IsOreReviewWorld(World) || Args.Num()!=1) return;
        auto* PC=World->GetFirstPlayerController(); if (!PC || !PC->IsLocalController()) return;
        if (Args[0]==TEXT("player")) { if (PC->GetPawn()) PC->SetViewTarget(PC->GetPawn()); return; }
        const FName Case(*FString::Printf(TEXT("OreReviewCase:%s"),*Args[0]));
        for (TActorIterator<ACameraActor> It(World);It;++It)
            if (It->ActorHasTag(TEXT("OreNavigationReview20261024")) && It->ActorHasTag(Case))
            { PC->SetViewTarget(*It); return; }
        UE_LOG(LogTemp,Warning,TEXT("Ore review camera not found: %s"),*Args[0]);
    }));
FAutoConsoleCommandWithWorldAndArgs PrepareOreMiners(TEXT("gs.Resources.OreReview.PrepareMiners"),
    TEXT("Teleport locally simulated miners to their existing legal mining reservation; optional cluster ID. Issue real mining orders first."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
    {
        if (!IsOreReviewWorld(World) || Args.Num()>1) return;
        if (Args.Num()==1 && (!Args[0].IsNumeric() || FCString::Atoi(*Args[0])<1)) return;
        const int32 Cluster=Args.IsEmpty()?0:FCString::Atoi(*Args[0]);
        auto* Hybrid=World->GetSubsystem<UGuLiHybridWorldSubsystem>(); if (!Hybrid || !Hybrid->IsWorldReady()) return;
        int32 Prepared=0,Unavailable=0;
        for (TActorIterator<AGuLiMiningVehiclePawn> It(World);It;++It)
        {
            auto* Miner=*It; if (Cluster && Miner->GetTargetClusterId()!=Cluster) continue;
            const auto* Binding=UGuLiHybridActorComponent::Find(Miner);
            if (!Binding || !Hybrid->HasSimulation(Binding->GetObjectKey(),true)) continue;
            FTransform Pose; auto* Travel=Miner->FindComponentByClass<UGuLiEngineeringTravelComponent>();
            if (!Travel || Travel->IsRouting() || !Miner->GetReservedMiningPose(Pose)) { ++Unavailable; continue; }
            Pose.SetScale3D(Miner->GetActorScale3D()); Miner->GetCharacterMovement()->StopMovementImmediately();
            if (Miner->SetActorTransform(Pose,false,nullptr,ETeleportType::TeleportPhysics)
                && Travel->BeginWorkMove(Pose.GetLocation(),GULI_MINING_SLOT_ARRIVAL_RADIUS_CM)) ++Prepared;
            else ++Unavailable;
        }
        UE_LOG(LogTemp,Display,TEXT("Ore review preparation: placed=%d unavailable=%d; node amounts, leases and extraction rules unchanged"),Prepared,Unavailable);
    }));
FAutoConsoleCommandWithWorld ResyncOreReview(TEXT("gs.Resources.OreReview.Resync"),
    TEXT("Discard pending local ore removals and reconstruct collision/navigation from the latest committed Hybrid directory."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        if (!IsOreReviewWorld(World)) return;
        auto* Resources=World->GetSubsystem<UGuLiResourceWorldSubsystem>();
        const bool Requested=Resources && Resources->RequestOreObstacleResync();
        UE_LOG(LogTemp,Display,TEXT("Ore review resync requested=%d (completion occurs on a later resource tick)"),Requested);
    }));
}
#endif
