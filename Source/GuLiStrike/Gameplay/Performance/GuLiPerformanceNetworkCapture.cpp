#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectReplicationComponent.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/Channel.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMemory.h"

TSharedRef<FJsonObject> UGuLiCommanderNetSyncComponent::GetStreamCaptureSnapshot() const
{
	auto Row = MakeShared<FJsonObject>();
	const double Now = GetWorld()->GetRealTimeSeconds();
	Row->SetNumberField(TEXT("generation"), StateSender.Session.Generation);
	Row->SetNumberField(TEXT("receive_generation"), StateReceiver.Session.Generation);
	Row->SetNumberField(TEXT("state_bytes"), double(StreamDiagnostics.StateBytes));
	Row->SetNumberField(TEXT("pose_bytes"), double(StreamDiagnostics.PoseBytes));
	Row->SetNumberField(TEXT("charged_bytes"), double(StreamDiagnostics.ChargedBytes));
	Row->SetNumberField(TEXT("observed_bytes"), double(StreamDiagnostics.ObservedBytes));
	Row->SetNumberField(TEXT("budget_deferrals"), double(StreamDiagnostics.BudgetDeferrals));
	Row->SetNumberField(TEXT("window_deferrals"), double(StreamDiagnostics.WindowDeferrals));
	Row->SetNumberField(TEXT("pose_merges"), double(StreamDiagnostics.PoseMerges));
	Row->SetNumberField(TEXT("received_state_bytes"), double(StreamDiagnostics.ReceivedStateBytes));
	Row->SetNumberField(TEXT("received_pose_bytes"), double(StreamDiagnostics.ReceivedPoseBytes));
	Row->SetNumberField(TEXT("received_state_blocks"), double(StreamDiagnostics.ReceivedStateBlocks));
	Row->SetNumberField(TEXT("received_pose_blocks"), double(StreamDiagnostics.ReceivedPoseBlocks));
	Row->SetNumberField(TEXT("pending_states"), StateSender.Pending.Num());
	Row->SetNumberField(TEXT("pending_poses"), QueuedPoses.Num());
	Row->SetNumberField(TEXT("inflight_batches"), StateSender.InFlight.Num());
	Row->SetNumberField(TEXT("oldest_state_wait_ms"), StateSender.OldestWait(Now) * 1000);
	double PoseWait = 0;
	for (const auto& Pair : QueuedPoses) PoseWait = FMath::Max(PoseWait, Now - Pair.Value.FirstWaitingTime);
	Row->SetNumberField(TEXT("oldest_pose_wait_ms"), PoseWait * 1000);
	Row->SetNumberField(TEXT("max_sent_sample_gap_ms"), StreamDiagnostics.MaxSampleGap * 1000);
	Row->SetNumberField(TEXT("max_received_sample_gap_ms"), StreamDiagnostics.MaxReceivedSampleGap * 1000);
	return Row;
}

void UGuLiPerformanceSubsystem::CaptureNetworkSample()
{
	check(IsInGameThread());
	const double Now = FPlatformTime::Seconds();
	LastNetworkSample = Now;
	if (NetworkSamples.Num() >= 4096) { ++DroppedNetworkSamples; return; }
	UWorld* World = GetWorld();
	auto Row = MakeShared<FJsonObject>();
	Row->SetNumberField(TEXT("wall_seconds"), Now);
	Row->SetNumberField(TEXT("world_seconds"), World->GetTimeSeconds());
	Row->SetNumberField(TEXT("engine_frame"), double(GFrameCounter));
	Row->SetNumberField(TEXT("net_mode"), int32(World->GetNetMode()));
	Row->SetNumberField(TEXT("process_physical_bytes"), double(FPlatformMemory::GetStats().UsedPhysical));
	const auto* State = World->GetGameState<AGuLiBattleGameState>();
	const auto* Flights = State ? State->FindComponentByClass<UGuLiCombatEffectReplicationComponent>() : nullptr;
	if (State)
	{
		Row->SetStringField(TEXT("match_id"), State->GetMatchId().ToString());
		Row->SetNumberField(TEXT("estimated_server_seconds"), State->GetServerWorldTimeSeconds());
#if WITH_EDITOR
		// Direct same-process comparison is diagnostic only, and unavailable outside PIE.
		if (GEngine && World->WorldType == EWorldType::PIE && World->GetNetMode() == NM_Client)
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				const UWorld* Peer = Context.World();
				const auto* PeerState = Peer ? Peer->GetGameState<AGuLiBattleGameState>() : nullptr;
				if (Peer && Peer->WorldType == EWorldType::PIE && Peer->GetNetMode() == NM_DedicatedServer
					&& PeerState && State->GetMatchId().IsValid() && State->GetMatchId() == PeerState->GetMatchId())
					Row->SetNumberField(TEXT("pie_clock_estimate_error_ms"), (State->GetServerWorldTimeSeconds() - Peer->GetTimeSeconds()) * 1000);
			}
#endif
	}
	if (Flights)
	{
		auto F = MakeShared<FJsonObject>();
		F->SetNumberField(TEXT("created"), double(Flights->GetCreatedFlightCount()));
		F->SetNumberField(TEXT("ended"), double(Flights->GetEndedFlightCount()));
		F->SetNumberField(TEXT("produced_record_bytes"), double(Flights->GetProducedFlightRecordBytes()));
		F->SetNumberField(TEXT("byte_accounting_failures"), double(Flights->GetFlightByteAccountingFailures()));
		F->SetNumberField(TEXT("active"), Flights->GetActiveFlightCount());
		const auto& R = Flights->GetFlightReceiveCounters();
		F->SetNumberField(TEXT("received_bytes"), double(R.PayloadBytes));
		F->SetNumberField(TEXT("received_batches"), double(R.Batches));
		F->SetNumberField(TEXT("received_record_bytes"), double(R.PayloadBytes - R.Batches));
		F->SetNumberField(TEXT("received_events"), double(R.Events));
		F->SetNumberField(TEXT("received_bootstrap"), double(R.BootstrapEvents));
		F->SetNumberField(TEXT("received_stale_epoch"), double(R.StaleEpochEvents));
		F->SetNumberField(TEXT("age_samples"), double(R.AgeSamples));
		F->SetNumberField(TEXT("age_sum_ms"), R.AgeMilliseconds);
		TArray<TSharedPtr<FJsonValue>> Buckets;
		for (uint64 Count : R.AgeBuckets) Buckets.Add(MakeShared<FJsonValueNumber>(double(Count)));
		F->SetArrayField(TEXT("age_histogram"), Buckets);
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
	TArray<TSharedPtr<FJsonValue>> Connections;
	auto SampleConnection = [&](UNetConnection* Connection)
	{
		if (!Connection || Connection->GetConnectionState() != USOCK_Open) return;
		auto C = MakeShared<FJsonObject>();
		C->SetStringField(TEXT("connection"), Connection->GetPathName());
		APlayerController* PC = Connection->PlayerController;
		const auto* Player = PC ? PC->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
		if (Player)
		{
			C->SetStringField(TEXT("player_guid"), Player->GetPlayerGuid().ToString());
			C->SetNumberField(TEXT("rtt_ms"), Player->GetPingInMilliseconds());
		}
		const uint32 Totals[] = { uint32(Connection->InTotalBytes), uint32(Connection->OutTotalBytes),
			uint32(Connection->InTotalPackets), uint32(Connection->OutTotalPackets),
			uint32(Connection->InTotalPacketsLost), uint32(Connection->OutTotalPacketsLost) };
		const TCHAR* Names[] = { TEXT("receive_bytes_per_second"), TEXT("send_bytes_per_second"),
			TEXT("receive_packets_per_second"), TEXT("send_packets_per_second"),
			TEXT("receive_lost_per_second"), TEXT("send_lost_per_second") };
		auto& Previous = ConnectionSamples.FindOrAdd(Connection);
		const double Elapsed = Now - Previous.WallSeconds;
		bool bValid = Previous.WallSeconds >= 0 && Elapsed > 0;
		for (int32 I = 0; I < 6; ++I) bValid &= Totals[I] - Previous.Totals[I] <= MAX_int32;
		C->SetBoolField(TEXT("rates_valid"), bValid);
		C->SetNumberField(TEXT("interval_seconds"), bValid ? Elapsed : 0);
		for (int32 I = 0; I < 6; ++I)
		{
			if (bValid) C->SetNumberField(Names[I], (Totals[I] - Previous.Totals[I]) / Elapsed);
			Previous.Totals[I] = Totals[I];
		}
		Previous.WallSeconds = Now;
		C->SetNumberField(TEXT("budget_bytes_per_second"), Connection->CurrentNetSpeed);
		C->SetNumberField(TEXT("queued_bits_raw"), Connection->QueuedBits);
		C->SetNumberField(TEXT("send_buffer_bits"), double(Connection->SendBuffer.GetNumBits()));
		C->SetNumberField(TEXT("bandwidth_debt_bits"), FMath::Max(0, Connection->QueuedBits));
		int32 Reliable = 0;
		for (const UChannel* Channel : Connection->OpenChannels) if (Channel) Reliable += FMath::Max(0, Channel->NumOutRec);
		C->SetNumberField(TEXT("unacked_reliable_bunches"), Reliable);
		if (Flights)
		{
			const auto P = Flights->GetFlightPeerStats(Connection);
			if (P.bAvailable)
			{
				auto F = MakeShared<FJsonObject>();
				F->SetNumberField(TEXT("channel_serial"), P.ChannelSerial);
				F->SetNumberField(TEXT("queued_events"), P.QueuedEvents);
				F->SetNumberField(TEXT("head_event_age_ms"), P.HeadEventAgeMilliseconds);
				F->SetNumberField(TEXT("enqueued_events"), double(P.EnqueuedEvents));
				F->SetNumberField(TEXT("enqueued_record_bytes"), double(P.EnqueuedRecordBytes));
				F->SetNumberField(TEXT("bootstrap_events"), double(P.BootstrapEvents));
				F->SetNumberField(TEXT("bootstrap_record_bytes"), double(P.BootstrapRecordBytes));
				F->SetNumberField(TEXT("sent_events"), double(P.SentEvents));
				F->SetNumberField(TEXT("sent_bytes"), double(P.SentBytes));
				F->SetNumberField(TEXT("sent_record_bytes"), double(P.SentBytes - P.SentBatches));
				F->SetNumberField(TEXT("sent_batches"), double(P.SentBatches));
				F->SetNumberField(TEXT("flush_calls"), double(P.FlushCalls));
				F->SetNumberField(TEXT("budget_deferrals"), double(P.BudgetDeferrals));
				F->SetNumberField(TEXT("batch_limit_hits"), double(P.BatchLimitHits));
				C->SetObjectField(TEXT("flight_peer"), F);
			}
		}
		if (const auto* Sync = PC ? PC->FindComponentByClass<UGuLiCommanderNetSyncComponent>() : nullptr)
			C->SetObjectField(TEXT("unit_stream"), Sync->GetStreamCaptureSnapshot());
		Connections.Add(MakeShared<FJsonValueObject>(C));
	};
	if (UNetDriver* Driver = World->GetNetDriver())
	{
		if (Driver->ServerConnection) SampleConnection(Driver->ServerConnection);
		else for (UNetConnection* Connection : Driver->ClientConnections) SampleConnection(Connection);
	}
	for (auto It = ConnectionSamples.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
	Row->SetArrayField(TEXT("connections"), Connections);
	NetworkSamples.Add(MakeShared<FJsonValueObject>(Row));
}
