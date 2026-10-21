#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Network/GuLiHybridSessionSubsystem.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Engine/GameInstance.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMemory.h"

TSharedRef<FJsonObject> UGuLiCommanderNetSyncComponent::GetStreamCaptureSnapshot() const
{
    auto Row=MakeShared<FJsonObject>();
    Row->SetNumberField(TEXT("generation"),GetConnectionGeneration());
    Row->SetNumberField(TEXT("preparing_commands"),HybridInputs.Num());
    Row->SetNumberField(TEXT("pending_commands"),HybridPendingCommands.Num());
    Row->SetNumberField(TEXT("presentation_queue_chunks"),PendingPoseChunks.Num());
    Row->SetNumberField(TEXT("presentation_queue_bytes"),double(GetPresentationQueueBytes()));
    Row->SetNumberField(TEXT("move_endpoints"),MoveEndpoints.Items.Num());
    return Row;
}


void UGuLiPerformanceSubsystem::CaptureNetworkSample(bool ObservationOnly)
{
	check(IsInGameThread());
	const double Now = FPlatformTime::Seconds();
    if (ObservationOnly)
    {LastObservationSample=Now;if (ObservationSamples.Num()>=4096) {++DroppedObservationSamples;return;}}
    else
    {LastNetworkSample=Now;if (NetworkSamples.Num()>=4096) {++DroppedNetworkSamples;return;}}
	UWorld* World = GetWorld();
	auto Row = MakeShared<FJsonObject>();
	Row->SetNumberField(TEXT("wall_seconds"), Now);
    Row->SetNumberField(TEXT("qpc"),double(FPlatformTime::Cycles64()));
	Row->SetNumberField(TEXT("world_seconds"), World->GetTimeSeconds());
	Row->SetNumberField(TEXT("engine_frame"), double(GFrameCounter));
	Row->SetNumberField(TEXT("net_mode"), int32(World->GetNetMode()));
	Row->SetNumberField(TEXT("process_physical_bytes"), double(FPlatformMemory::GetStats().UsedPhysical));
    if (const auto* Session=World->GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>(); Session)
        Row->SetObjectField(TEXT("hybrid_transport"),Session->GetNetworkCaptureSnapshot());
    if (const auto* Hybrid=World->GetSubsystem<UGuLiHybridWorldSubsystem>())
    {
        Row->SetObjectField(TEXT("async_transactions"),Hybrid->GetTransactionCaptureSnapshot());
        Row->SetObjectField(TEXT("simulation_census"),Hybrid->GetSimulationCensus());
        const auto M=Hybrid->GetOwnershipMetrics();auto H=MakeShared<FJsonObject>();
        H->SetNumberField(TEXT("committed"),double(M.Committed));
        H->SetNumberField(TEXT("received_payload_bytes"),double(M.ReceivedPayloadBytes));
        H->SetNumberField(TEXT("queued_payload_bytes"),double(M.QueuedPayloadBytes));
        H->SetNumberField(TEXT("last_ms"),M.LastMilliseconds);
        Row->SetObjectField(TEXT("ownership_handoff"),H);
    }
    if (const auto* Tasks=World->GetSubsystem<UGuLiUnitTaskSubsystem>())
    {
        struct FTaskCounts { int32 Local=0,Peer=0,Active=0,Stopped=0,Queued=0,Cancelling=0,Planning=0; };
        TMap<uint16,FTaskCounts> Counts;
        for (const auto& [Unit,Task]:Tasks->States)
        {
            auto& C=Counts.FindOrAdd(Task.Context.UnitTypeId);
            if (!Tasks->HasTaskSimulation(Task)) {++C.Peer;continue;}
            ++C.Local;C.Active+=Task.Active.IsSet();C.Stopped+=Task.bStopped;
            C.Queued+=Task.Queue.Num();C.Cancelling+=Task.bCancelPending;
            C.Planning+=Task.PendingMove.IsSet()||(Task.Active.IsSet()&&Task.Active->bPlanning);
        }
        auto Activity=MakeShared<FJsonObject>();
        for (const auto& [Type,C]:Counts)
        {
            auto J=MakeShared<FJsonObject>();
            J->SetNumberField(TEXT("local"),C.Local);J->SetNumberField(TEXT("without_simulation"),C.Peer);
            J->SetNumberField(TEXT("active_orders"),C.Active);J->SetNumberField(TEXT("stopped"),C.Stopped);
            J->SetNumberField(TEXT("queued_orders"),C.Queued);J->SetNumberField(TEXT("cancelling"),C.Cancelling);
            J->SetNumberField(TEXT("planning"),C.Planning);Activity->SetObjectField(FString::Printf(TEXT("unit/%u"),Type),J);
        }
        Row->SetObjectField(TEXT("task_activity"),Activity);
    }
	const auto* State = World->GetGameState<AGuLiBattleGameState>();
	const auto* Flights = State ? State->FindComponentByClass<UGuLiCombatEffectReplicationComponent>() : nullptr;
	if (State)
	{
		Row->SetStringField(TEXT("match_id"), State->GetMatchId().ToString());

	}
	if (Flights)
	{
		auto F = MakeShared<FJsonObject>();
        F->SetStringField(TEXT("basis"),TEXT("Local query registry, including owned and received flights; not transport bytes"));
        F->SetNumberField(TEXT("observed"),double(Flights->GetObservedFlightCount()));
        F->SetNumberField(TEXT("finished"),double(Flights->GetFinishedFlightCount()));
        F->SetNumberField(TEXT("active"),Flights->GetActiveFlightCount());
        F->SetNumberField(TEXT("additional_active"),Flights->GetAdditionalActiveFlightCount());
		Row->SetObjectField(TEXT("flights"), F);
	}
	if (const auto* Visuals = World->GetSubsystem<UGuLiCombatEffectPresentationSubsystem>())
	{
		const auto C = Visuals->GetCounters();
		auto V = MakeShared<FJsonObject>();
		V->SetNumberField(TEXT("received_shots"), double(C.ReceivedShots));
		V->SetNumberField(TEXT("dropped_shots"), double(C.DroppedShots));
		V->SetNumberField(TEXT("shots_too_old"), double(C.ShotsTooOld));
		V->SetNumberField(TEXT("shots_from_future"), double(C.ShotsFromFuture));
		V->SetNumberField(TEXT("muzzle_accepted"), double(C.MuzzleAccepted));
		V->SetNumberField(TEXT("muzzle_born"), double(C.MuzzleBorn));
		V->SetNumberField(TEXT("muzzle_expired"), double(C.MuzzleExpired));
		V->SetNumberField(TEXT("muzzle_pose_unresolved"), double(C.MuzzlePoseUnresolved));
		V->SetNumberField(TEXT("muzzle_too_old"), double(C.MuzzleTooOld));
		V->SetNumberField(TEXT("muzzle_admission_late"), double(C.MuzzleAdmissionLate));
		V->SetNumberField(TEXT("muzzle_active"), C.MuzzleActive);
		V->SetNumberField(TEXT("flight_data_active"), C.ClientFlightDataActive);
		Row->SetObjectField(TEXT("visuals"), V);
	}
    for (auto It=World->GetPlayerControllerIterator();It;++It)
        if (const auto* PC=It->Get();PC && PC->IsLocalController())
            if (const auto* Sync=PC->FindComponentByClass<UGuLiCommanderNetSyncComponent>())
                Row->SetObjectField(TEXT("commander_input"),Sync->GetStreamCaptureSnapshot());
    auto Sample=MakeShared<FJsonValueObject>(Row);
    if (!ObservationOnly) NetworkSamples.Add(Sample);
    if (ObservationOnly || (ObservationDeadline>0 && Now-LastObservationSample>=1.0))
    {
        LastObservationSample=Now;
        if (ObservationSamples.Num()<4096) ObservationSamples.Add(Sample);else ++DroppedObservationSamples;
    }
}
