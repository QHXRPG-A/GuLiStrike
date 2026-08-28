// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Components/ActorComponent.h"
#include "GuLiCommanderNetSyncComponent.generated.h"

class AGuLiCommanderPlayerController;
class AGuLiCommanderPlayerState;

DECLARE_MULTICAST_DELEGATE_OneParam(
	FGuLiCommanderSelectionChangedSignature,
	const FGuLiCommanderSelectionState&);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FGuLiCommanderCommandAckChangedSignature,
	const FGuLiCommandAck&);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FGuLiSoldierPoseChunkReceivedSignature,
	const FGuLiSoldierPoseChunk&);

/**
 * Owner-only command and bootstrap channel. Gameplay requests contain only
 * intent-only inputs and are revalidated by the authority subsystem.
 */
UCLASS(ClassGroup = (GuLiStrike), meta = (BlueprintSpawnableComponent))
class UGuLiCommanderNetSyncComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGuLiCommanderNetSyncComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Server entry used by GameMode and reconnect handling. */
	void StartServerBootstrap();

	/** Starts a fresh generation only when the authority MatchEpoch is not already covered. */
	void EnsureServerBootstrapForMatch(uint32 AuthorityMatchEpoch);

	/** Server-only 10 Hz owner-channel movement publication. */
	void SendPoseChunk(const FGuLiSoldierPoseChunk& Chunk);

	/** Refreshes alive/order summaries and removes fully destroyed cohorts. */
	bool RefreshServerSelection();

	/** Presentation drains received chunks every rendered frame. */
	void ConsumePendingPoseChunks(TArray<FGuLiSoldierPoseChunk>& OutChunks);

	/** Owning controller drains authoritative ACKs without latest-value coalescing. */
	void ConsumePendingCommandAcks(TArray<FGuLiCommandAck>& OutAcks);

	/** Low-latency idempotent command path with a delayed reliable fallback. */
	void SubmitSelectionRequest(const FGuLiSelectionRequest& Request);
	void SubmitMoveRequest(const FGuLiMoveRequest& Request);

	UFUNCTION(Server, Reliable)
	void ServerRequestSelection(const FGuLiSelectionRequest& Request);

	UFUNCTION(Server, Reliable)
	void ServerIssueMove(const FGuLiMoveRequest& Request);

	UFUNCTION(Server, Reliable)
	void ServerRequestBootstrap(uint32 ClientBootstrapRequestId);

	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeBootstrap(
		uint32 InSyncGeneration,
		uint16 ClientProtocolVersion,
		uint32 ClientMatchEpoch,
		uint16 AppliedRosterCount,
		uint32 AppliedSnapshotRevision);

	/** Placeholder fact-stream ACK; retained so the 8 KiB chunk stream can land without changing RPC ownership. */
	UFUNCTION(Server, Unreliable)
	void ServerAcknowledgeFacts(uint32 InSyncGeneration, uint32 HighestContiguousStreamSeq);

	const FGuLiCommanderSelectionState& GetSelectionState() const { return SelectionState; }

	const FGuLiCommandAck& GetLastCommandAck() const { return LastCommandAck; }

	uint32 GetSyncGeneration() const { return SyncGeneration; }

#if !UE_BUILD_SHIPPING
	/** Non-shipping acceptance evidence; counts unique admitted 10 Hz pose frames. */
	uint64 GetAcceptedPoseFrameCount() const { return AcceptedPoseFrameCount; }
	double GetLastAcceptedPoseReceiveTimeSeconds() const { return LastAcceptedPoseReceiveTimeSeconds; }
#endif

	FGuLiCommanderSelectionChangedSignature OnSelectionChanged;

	FGuLiCommanderCommandAckChangedSignature OnCommandAckChanged;

	FGuLiSoldierPoseChunkReceivedSignature OnPoseChunkReceived;

#if WITH_DEV_AUTOMATION_TESTS
	/** Test-only access to the shared bootstrap contract predicate. Not compiled into Shipping. */
	static bool TestOnly_IsBootstrapSnapshotCompatible(
		uint16 ProtocolVersion,
		uint32 GameStateMatchEpoch,
		uint32 SnapshotMatchEpoch,
		uint32 SnapshotRevision,
		int32 RosterCount,
		uint32 ExpectedMatchEpoch,
		uint32 ExpectedSnapshotRevision,
		uint16 ExpectedRosterCount);

	/** Test-only view of the authority lifecycle decision used across seamless travel. */
	static bool TestOnly_ShouldStartNewServerBootstrap(
		uint32 ExistingSyncGeneration,
		uint32 ExistingBootstrapMatchEpoch,
		uint32 AuthorityMatchEpoch,
		bool bPlayerSyncReady);

	/** Test-only injection through the same client ACK implementation used by the reliable RPC. */
	void TestOnly_ReceiveCommandAck(const FGuLiCommandAck& Ack);

	/** Test-only configuration for exercising the production pose admission gate. */
	void TestOnly_ConfigureClientPoseGate(bool bReady, uint32 AcceptedMatchEpoch);

	/** Test-only injection through the same client pose implementation used by the unreliable RPC. */
	void TestOnly_ReceivePoseChunk(const FGuLiSoldierPoseChunk& Chunk);
#endif

private:
	UFUNCTION(Server, Unreliable)
	void ServerRequestSelectionFast(const FGuLiSelectionRequest& Request);

	UFUNCTION(Server, Unreliable)
	void ServerIssueMoveFast(const FGuLiMoveRequest& Request);

	UFUNCTION(Client, Reliable)
	void ClientBootstrapStarted(
		uint32 NewSyncGeneration,
		uint16 ServerProtocolVersion,
		uint32 MatchEpoch,
		uint16 ExpectedRosterCount,
		uint32 ExpectedSnapshotRevision);

	UFUNCTION(Client, Reliable)
	void ClientReceiveCommandAck(const FGuLiCommandAck& Ack);

	UFUNCTION(Client, Unreliable)
	void ClientReceiveCommandAckFast(const FGuLiCommandAck& Ack);

	UFUNCTION(Client, Unreliable)
	void ClientReceiveSoldierPoseChunk(const FGuLiSoldierPoseChunk& Chunk);

	UFUNCTION()
	void OnRep_SelectionState();

	UFUNCTION()
	void OnRep_SyncGeneration();

	AGuLiCommanderPlayerController* GetCommanderController() const;
	AGuLiCommanderPlayerState* GetCommanderPlayerState() const;
	void BeginSelectionIntent(const FGuLiSelectionRequest& Request);
	void BeginMoveIntent(const FGuLiMoveRequest& Request);
	void TickPendingCommandRetries();
	void HandleSelectionRequest(const FGuLiSelectionRequest& Request, bool bReliableAck);
	void HandleMoveRequest(const FGuLiMoveRequest& Request, bool bReliableAck);
	void ReceiveCommandAck(const FGuLiCommandAck& Ack);
	bool CanProcessCommanderRequest(FGuLiCommandAck& InOutAck, const TCHAR* RpcName);
	bool ConsumeCommandRateLimit();
	bool ConsumeCachedAckReplayLimit(uint8& ReplayCount);
	void InitializeAck(FGuLiCommandAck& Ack, uint32 ClientId, EGuLiCommandKind CommandKind) const;
	void PublishAck(const FGuLiCommandAck& Ack, bool bReliableDelivery);
	void ResendServerBootstrapMarker();
	void TryCompleteClientBootstrap();
	void NotifySelectionChanged();
	void LogRejectedRpc(const TCHAR* RpcName, EGuLiCommandAckResult Result);
	void MirrorSyncReadyToRoleSlot(const AGuLiCommanderPlayerState& CommanderPlayerState, bool bReady) const;
	static bool IsNewerSerial(uint32 Candidate, uint32 Baseline);

	UPROPERTY(ReplicatedUsing = OnRep_SelectionState)
	FGuLiCommanderSelectionState SelectionState;

	FGuLiCommandAck LastCommandAck;

	UPROPERTY(ReplicatedUsing = OnRep_SyncGeneration)
	uint32 SyncGeneration = 0;

	uint32 LastSelectionRequestId = 0;
	uint32 LastMoveCommandId = 0;
	uint32 LastBootstrapRequestId = 0;
	uint32 HighestAckedStreamSeq = 0;
	uint32 BootstrapMatchEpoch = 0;
	uint16 BootstrapExpectedRosterCount = 0;
	uint32 BootstrapExpectedSnapshotRevision = 0;
	uint32 PendingBootstrapGeneration = 0;
	uint32 PendingBootstrapMatchEpoch = 0;
	uint16 PendingBootstrapRosterCount = 0;
	uint32 PendingBootstrapSnapshotRevision = 0;
	uint32 ClientAcceptedMatchEpoch = 0;
	bool bLoggedBootstrapProtocolMismatch = false;
	bool bClientPoseReady = false;
	bool bPendingSelectionIntent = false;
	bool bPendingSelectionFastRetry = false;
	bool bPendingSelectionReliableFallback = false;
	bool bPendingMoveIntent = false;
	bool bPendingMoveFastRetry = false;
	bool bPendingMoveReliableFallback = false;
	bool bQueuedMoveAfterSelection = false;
	double PendingSelectionFastRetryTimeSeconds = 0.0;
	double PendingSelectionReliableFallbackTimeSeconds = 0.0;
	double PendingMoveFastRetryTimeSeconds = 0.0;
	double PendingMoveReliableFallbackTimeSeconds = 0.0;
	FGuLiSelectionRequest PendingSelectionIntent;
	FGuLiMoveRequest PendingMoveIntent;
	FGuLiMoveRequest QueuedMoveAfterSelection;
	FGuLiSelectionRequest LastSelectionRequest;
	FGuLiMoveRequest LastMoveRequest;
	TArray<FGuLiSelectionRequest> QueuedSelectionIntents;
	FGuLiCommandAck LastSelectionAck;
	FGuLiCommandAck LastMoveAck;
	uint8 SelectionCachedFastReplayCount = 0u;
	uint8 MoveCachedFastReplayCount = 0u;
	EGuLiCommandKind LastDeliveredAckKind = EGuLiCommandKind::None;
	uint32 LastDeliveredAckCommandId = 0u;
	TArray<double> RecentCommanderRequestTimes;
	double LastSecurityLogTime = -1.0;
	uint32 SuppressedSecurityLogCount = 0;
	TArray<FGuLiCommandAck> PendingCommandAcks;
	TArray<FGuLiSoldierPoseChunk> PendingPoseChunks;
#if !UE_BUILD_SHIPPING
	uint64 AcceptedPoseFrameCount = 0u;
	uint32 LastAcceptedPoseFrameSequence = 0u;
	double LastAcceptedPoseReceiveTimeSeconds = 0.0;
#endif
};
