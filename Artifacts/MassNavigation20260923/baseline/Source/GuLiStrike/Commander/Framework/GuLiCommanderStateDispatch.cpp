#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Commander/Network/GuLiCommanderPoseMetrics.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "GuLiStrike.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

CSV_DECLARE_CATEGORY_EXTERN(GuLiCommanderPoseDispatch);

namespace
{
TAutoConsoleVariable<int32> CVarStateStreamDiagnostics(TEXT("guli.Commander.StateStreamDiagnostics"), 0,
	TEXT("Log per-connection Mass state/pose queue and budget counters once per second."));

/** Credit is already accounted for by UE. Only reserve the part of our estimate not yet visible there. */
struct FConnectionBudget
{
	UNetConnection* Connection;
	int64 UnreflectedBits = 0;
	int32 LocalRemaining = 16384;
	explicit FConnectionBudget(UNetConnection* InConnection) : Connection(InConnection) {}
	int64 UsedBits() const { return Connection ? int64(Connection->QueuedBits) + Connection->SendBuffer.GetNumBits() : 0; }
	int32 Available() const
	{
		if (!Connection) return LocalRemaining;
		if (!Connection->IsNetReady()) return 0;
		return int32(FMath::Clamp<int64>((-UsedBits() - UnreflectedBits) / 8, 0, MAX_int32));
	}
	int32 Reconcile(int64 Before, int32 EstimatedBytes, GuLiCommanderStateStream::FDiagnostics& Stats)
	{
		const int64 ObservedBits = Connection ? FMath::Max<int64>(0, UsedBits() - Before) : int64(EstimatedBytes) * 8;
		const int32 Charged = int32(FMath::Max<int64>(int64(EstimatedBytes) * 8, ObservedBits) + 7) / 8;
		UnreflectedBits += FMath::Max<int64>(0, int64(EstimatedBytes) * 8 - ObservedBits);
		LocalRemaining = FMath::Max(0, LocalRemaining - Charged);
		Stats.ObservedBytes += (ObservedBits + 7) / 8; Stats.ChargedBytes += Charged;
		return Charged;
	}
};
}

void UGuLiCommanderNetSyncComponent::ResetStateStream()
{
	StateSender.Reset(); StateReceiver.Reset(); ReceivedStateBatches.Reset();
	QueuedPoses.Reset(); StreamDiagnostics = {}; ClientAppliedIdHash = 0;
	PendingStateResyncGeneration = 0; NextStateResyncTime = 0;
}

void UGuLiCommanderNetSyncComponent::ServerRequestStateResync_Implementation(uint32 ConnectionGeneration, uint32 ExpectedSyncGeneration)
{
	EnsureServerConnectionBootstrap();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsConnectionReady()
		|| ConnectionGeneration != GetConnectionGeneration() || !ExpectedSyncGeneration
		|| ExpectedSyncGeneration != SyncGeneration || !ConsumeCommandRateLimit()) return;
	// Same request cannot restart the replacement generation; its expected generation is now stale.
	BootstrapMatchEpoch = 0;
	StartServerBootstrap();
}

void UGuLiCommanderNetSyncComponent::QueueSoldierRetirement(const FGuLiSoldierStateItem& FinalState, uint32 MatchEpoch)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || StateSender.Session.Epoch != MatchEpoch || !GetWorld()) return;
	StateSender.Retire(FinalState, GetWorld()->GetRealTimeSeconds());
	QueuedPoses.Remove(FinalState.SoldierId);
	StreamDiagnostics.LastSentSampleTimes.Remove(FinalState.SoldierId);
}

void UGuLiCommanderNetSyncComponent::ClientReceiveStateBatch_Implementation(const FGuLiEncodedStateBatch& Batch)
{
	GuLiCommanderStateStream::FHeader Header;
	if (!GuLiCommanderStateStream::ReadHeader(Batch, Header)) return;
	const uint32 Expected = PendingBootstrapGeneration ? PendingBootstrapGeneration : ClientAcceptedSyncGeneration;
	if (!Expected || Header.Generation != Expected) return;
	if (Header.Generation == StateReceiver.Session.Generation && StateReceiver.AppliedSequence
		&& !IsNewerSerial(Header.Sequence, StateReceiver.AppliedSequence)) return;
	// The sender cannot advance beyond this window without our application ACK.
	if (ReceivedStateBatches.Num() >= GuLiCommanderStateStream::MaxInFlight)
	{
		UE_LOG(LogGuLiStrike, Error, TEXT("Mass state receive window exceeded owner=%s generation=%u"), *GetNameSafe(GetOwner()), Header.Generation);
		return;
	}
	ReceivedStateBatches.Add(Batch);
	ApplyPendingStateBatches();
}

void UGuLiCommanderNetSyncComponent::ApplyPendingStateBatches()
{
	if (PendingStateResyncGeneration) return;
	const APlayerController* Controller = GetOwningPlayerController();
	const AGuLiBattleGameState* GameState = GetBattleGameState();
	if (!Controller || !Controller->IsLocalController() || !IsConnectionReady() || !GameState
		|| GameState->GetProtocolVersion() != GULI_COMMANDER_PROTOCOL_VERSION) return;
	const uint32 Generation = PendingBootstrapGeneration ? PendingBootstrapGeneration : ClientAcceptedSyncGeneration;
	const uint32 Epoch = PendingBootstrapMatchEpoch ? PendingBootstrapMatchEpoch : ClientAcceptedMatchEpoch;
	if (!Generation || GameState->GetMatchEpoch() != Epoch || SoldierBootstrapBinding.ConnectionGeneration != GetConnectionGeneration()
		|| SoldierBootstrapBinding.SoldierSyncGeneration != Generation || SyncGeneration != Generation) return;
	AGuLiSoldierStateReplicator* Roster = PoseRoster.Get();
	if (!Roster) for (TActorIterator<AGuLiSoldierStateReplicator> It(GetWorld()); It; ++It) { Roster = *It; break; }
	if (!Roster) return; // Reliable RPC and this always-relevant actor need not arrive in the same frame.
	BindPoseRoster(*Roster);
	if (!StateReceiver.Session.Generation)
	{
		StateReceiver.Session.ConnectionGeneration = GetConnectionGeneration();
		StateReceiver.Session.Generation = Generation; StateReceiver.Session.Epoch = Epoch;
	}
	while (!ReceivedStateBatches.IsEmpty())
	{
		GuLiCommanderStateStream::FHeader Header;
		GuLiCommanderStateStream::ReadHeader(ReceivedStateBatches[0], Header);
		TArray<GuLiCommanderStateStream::FRecord> Records;
		if (!StateReceiver.Apply(ReceivedStateBatches[0], Records))
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Mass state batch rejected owner=%s generation=%u expected=%u received=%u; readiness remains closed"),
				*GetNameSafe(GetOwner()), Generation, StateReceiver.AppliedSequence + 1, Header.Sequence);
			bClientPoseReady = false;
			PendingStateResyncGeneration = Generation;
			return; // Never acknowledge a partially decoded/applied batch.
		}
		ReceivedStateBatches.RemoveAt(0, 1, EAllowShrinking::No);
		if (!GetOwner()->HasAuthority())
		{
			if (Header.bStart) { Roster->ResetRemoteRoster(Epoch); MoveEndpoints.Items.Reset(); }
			TArray<FGuLiSoldierStateItem> Changed; TArray<FGuLiSoldierId> Removed;
			for (const auto& Record : Records)
			{
				const auto Id = Record.Value.State.SoldierId;
				const int32 Index = MoveEndpoints.Items.IndexOfByPredicate([Id](const auto& E) { return E.SoldierId == Id; });
				if (Record.bRemove || !Record.Value.Endpoint.IsSet())
				{
					if (Index != INDEX_NONE) MoveEndpoints.Items.RemoveAtSwap(Index, 1, EAllowShrinking::No);
				}
				else if (Index == INDEX_NONE) MoveEndpoints.Items.Add(Record.Value.Endpoint.GetValue());
				else MoveEndpoints.Items[Index] = Record.Value.Endpoint.GetValue();
				if (Record.bRemove) Removed.Add(Id); else Changed.Add(Record.Value.State);
			}
			// Endpoints and order state are committed before notifying presentation/UI consumers.
			Roster->ApplyRemoteDelta(Changed, Removed, Epoch, Header.Sequence);
			if (Header.bStart || !Records.IsEmpty()) OnRep_MoveEndpoints();
		}
	}
	TryCompleteClientBootstrap();
}

void UGuLiCommanderNetSyncComponent::TickStateStream()
{
	using namespace GuLiCommanderStateStream;
	ApplyPendingStateBatches();
	const APlayerController* Controller = GetOwningPlayerController();
	if (Controller && Controller->IsLocalController() && !Controller->HasAuthority() && IsConnectionReady() && GetWorld()
		&& PendingStateResyncGeneration && GetWorld()->GetRealTimeSeconds() >= NextStateResyncTime)
	{
		NextStateResyncTime = GetWorld()->GetRealTimeSeconds() + 1.0;
		ServerRequestStateResync(GetConnectionGeneration(), PendingStateResyncGeneration);
	}
	if (Controller && Controller->IsLocalController() && IsConnectionReady()
		&& StateReceiver.AppliedSequence != StateReceiver.AckSentSequence)
	{
		// One cumulative reliable ACK per client Tick; bootstrap uses this same window.
		StateReceiver.AckSentSequence = StateReceiver.AppliedSequence;
		ServerAcknowledgeFacts(StateReceiver.Session.Generation, StateReceiver.AppliedSequence);
	}
	if (Controller && !Controller->HasAuthority() && GetWorld() && StateReceiver.Session.Generation
		&& CVarStateStreamDiagnostics.GetValueOnGameThread() && GetWorld()->GetRealTimeSeconds() >= StreamDiagnostics.NextLogTime)
	{
		StreamDiagnostics.NextLogTime = GetWorld()->GetRealTimeSeconds() + 1.0;
		UE_LOG(LogGuLiStrike, Display, TEXT("MassStreamRX owner=%s epoch=%u gen=%u applied=%u roster=%d pendingBatches=%d poseBytes=%llu poseBlocks=%llu maxSampleGapMs=%.1f ready=%d"),
			*GetNameSafe(GetOwner()), StateReceiver.Session.Epoch, StateReceiver.Session.Generation, StateReceiver.AppliedSequence,
			StateReceiver.Values.Num(), ReceivedStateBatches.Num(), StreamDiagnostics.ReceivedPoseBytes, StreamDiagnostics.ReceivedPoseBlocks,
			StreamDiagnostics.MaxReceivedSampleGap * 1000, IsSoldierStreamReady());
	}
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsConnectionReady() || !GetWorld()
		|| !StateSender.Session.Generation || StateSender.Session.Generation != SyncGeneration) return;
	const auto* GameState = GetBattleGameState();
	AGuLiSoldierStateReplicator* Roster = PoseRoster.Get();
	if (!GameState || GameState->GetMatchEpoch() != StateSender.Session.Epoch || !Roster
		|| Roster->GetSnapshotMatchEpoch() != StateSender.Session.Epoch) return;
	const double Now = GetWorld()->GetRealTimeSeconds();
	StateSender.Refresh(Roster->GetItems(), MoveEndpoints, Now);
	// A teleport floor invalidates pre-landing samples even while its reliable event waits for credit.
	for (auto It = QueuedPoses.CreateIterator(); It; ++It)
	{
		const auto* State = Roster->FindSoldierState(It.Key());
		if (!State || (State->DisplacementFrameFloor && int32(It.Value().Frame - State->DisplacementFrameFloor) < 0)) It.RemoveCurrent();
	}
	UNetConnection* Connection = Controller ? Controller->GetNetConnection() : nullptr;
	if (!Connection && (!Controller || !Controller->IsLocalController())) return;
	FConnectionBudget Budget(Connection);
	const int32 InitialCredit = Budget.Available();
	const bool bReady = IsSoldierStreamReady();
	// Keep the completion barrier stable until its explicit bootstrap acknowledgment.
	const bool bCanSendState = !StateSender.CompleteSequence || bReady;
	bool bBudgetDeferred = false;
	auto SendStates = [&](int32 Allowance, bool bUrgentOnly)
	{
		int32 Spent = 0;
		while (bCanSendState && StateSender.InFlight.Num() < MaxInFlight)
		{
			if (StateSender.CompleteSequence && !IsSoldierStreamReady()) break;
			const int32 Limit = FMath::Min(Allowance - Spent, Budget.Available()) - RpcOverheadBytes;
			FPrepared Prepared;
			const bool bPrepared = StateSender.Prepare(Limit, bUrgentOnly, Now, Prepared);
			bBudgetDeferred |= Prepared.bBudgetLimited;
			if (!bPrepared) break;
			const int32 Cost = Prepared.Block.Data.Num() + 2 + RpcOverheadBytes;
			if (Cost > Budget.Available()) { bBudgetDeferred = true; break; }
			const int64 Before = Budget.UsedBits();
			StateSender.Commit(Prepared); // Commit before local listen-server RPC/ACK can re-enter.
			ClientReceiveStateBatch(Prepared.Block);
			Spent += Budget.Reconcile(Before, Cost, StreamDiagnostics);
			StreamDiagnostics.StateBytes += Prepared.Block.Data.Num() + 2;
		}
		return Spent;
	};
	const bool bHasPoses = bReady && !QueuedPoses.IsEmpty();
	SendStates(bHasPoses ? InitialCredit / 2 : InitialCredit, true);
	if (bHasPoses)
	{
		TArray<FGuLiSoldierId> Ids; QueuedPoses.GetKeys(Ids);
		Ids.Sort([&](auto A, auto B)
		{
			const auto& PA = QueuedPoses.FindChecked(A); const auto& PB = QueuedPoses.FindChecked(B);
			return PA.FirstWaitingTime == PB.FirstWaitingTime ? A < B : PA.FirstWaitingTime < PB.FirstWaitingTime;
		});
		const auto Admissible = [&](FGuLiSoldierId Id)
		{
			const auto* V = StateSender.Published.Find(Id);
			const auto* P = QueuedPoses.Find(Id);
			return P && V && V->PublishedSequence && StateSender.AckedSequence
				&& int32(StateSender.AckedSequence - V->PublishedSequence) >= 0 && V->State.ActiveOrderId == P->Sample.ActiveOrderId
				&& V->State.IsAlive() == (P->Sample.State != EGuLiSoldierPoseState::Destroyed)
				&& (!V->State.DisplacementFrameFloor || int32(P->Frame - V->State.DisplacementFrameFloor) >= 0);
		};
		while (Budget.Available() > RpcOverheadBytes)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GuLiPose_Send);
			GuLiCommanderPoseMetrics::FScope Measure(GuLiCommanderPoseMetrics::EScope::Send);
			const auto* First = Ids.FindByPredicate([&](auto Id) { return Admissible(Id); });
			if (!First) break;
			const FPendingPose Source = QueuedPoses.FindChecked(*First);
			FGuLiSoldierPoseChunk Chunk; Chunk.AuthorityEpoch = BootstrapMatchEpoch;
			Chunk.FrameSequence = Source.Frame; Chunk.ServerSimTick = Source.SimTick; Chunk.ServerTimeSeconds = Source.SampleTime;
			for (auto Id : Ids)
			{
				if (!Admissible(Id)) continue;
				const auto& P = QueuedPoses.FindChecked(Id);
				if (P.Frame == Source.Frame && P.SimTick == Source.SimTick && P.SampleTime == Source.SampleTime) Chunk.Samples.Add(P.Sample);
				if (Chunk.Samples.Num() == GULI_MAX_POSE_SAMPLES_PER_CHUNK) break;
			}
			GuLiCommanderPoseCodec::FPreparedBlock Prepared;
			if (!PoseSender.Prepare(Chunk, Budget.Available() - RpcOverheadBytes, Prepared)) { bBudgetDeferred = true; break; }
			const int32 Cost = Prepared.Block.Data.Num() + 2 + RpcOverheadBytes;
			if (Cost > Budget.Available()) { bBudgetDeferred = true; break; }
			const int64 Before = Budget.UsedBits();
			PoseSender.Commit(Prepared);
			CSV_CUSTOM_STAT(GuLiCommanderPoseDispatch, AttemptedChunks, 1, ECsvCustomStatOp::Accumulate);
			ClientReceiveEncodedPoseBlock(Prepared.Block);
			Budget.Reconcile(Before, Cost, StreamDiagnostics);
			StreamDiagnostics.PoseBytes += Prepared.Block.Data.Num() + 2;
			for (const auto& Sample : Prepared.Samples)
			{
				if (const auto* Last = StreamDiagnostics.LastSentSampleTimes.Find(Sample.SoldierId))
					StreamDiagnostics.MaxSampleGap = FMath::Max(StreamDiagnostics.MaxSampleGap, double(Source.SampleTime - *Last));
				StreamDiagnostics.LastSentSampleTimes.Add(Sample.SoldierId, Source.SampleTime);
				QueuedPoses.Remove(Sample.SoldierId);
			}
		}
	}
	SendStates(Budget.Available(), false);
	if (bBudgetDeferred || (!QueuedPoses.IsEmpty() && Budget.Available() <= RpcOverheadBytes))
	{
		++StreamDiagnostics.BudgetDeferrals;
		CSV_CUSTOM_STAT(GuLiCommanderPoseDispatch, SaturatedAtAttempt, 1, ECsvCustomStatOp::Accumulate);
	}
	if (StateSender.InFlight.Num() == MaxInFlight && !StateSender.Pending.IsEmpty()) ++StreamDiagnostics.WindowDeferrals;
	if (CVarStateStreamDiagnostics.GetValueOnGameThread() && Now >= StreamDiagnostics.NextLogTime)
	{
		StreamDiagnostics.NextLogTime = Now + 1.0;
		double PoseAge = 0; for (const auto& P : QueuedPoses) PoseAge = FMath::Max(PoseAge, Now - P.Value.FirstWaitingTime);
		UE_LOG(LogGuLiStrike, Display, TEXT("MassStream owner=%s epoch=%u gen=%u credit=%d stateBytes=%llu poseBytes=%llu observedBytes=%llu chargedBytes=%llu stateQueue=%d poseQueue=%d stateAgeMs=%.1f poseAgeMs=%.1f stateMerges=%llu poseMerges=%llu budgetDeferred=%llu windowDeferred=%llu inflight=%d ack=%u sent=%u maxSampleGapMs=%.1f"),
			*GetNameSafe(GetOwner()), BootstrapMatchEpoch, SyncGeneration, Budget.Available(), StreamDiagnostics.StateBytes,
			StreamDiagnostics.PoseBytes, StreamDiagnostics.ObservedBytes, StreamDiagnostics.ChargedBytes, StateSender.Pending.Num(), QueuedPoses.Num(),
			StateSender.OldestWait(Now) * 1000, PoseAge * 1000, StateSender.MergeCount, StreamDiagnostics.PoseMerges,
			StreamDiagnostics.BudgetDeferrals, StreamDiagnostics.WindowDeferrals, StateSender.InFlight.Num(), StateSender.AckedSequence,
			StateSender.LastSequence, StreamDiagnostics.MaxSampleGap * 1000);
	}
}
