// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Relay/GuLiWingmanRelayServer.h"
#include "Components/ActorComponent.h"
#include "GuLiWingmanRelayComponent.generated.h"

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanRelayReplicatedState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Relay")
	FGuLiWingmanLeaseState Lease;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Relay")
	FGuLiGroupAbilityConfigSnapshot AbilityConfig;

	/** Latest server-replayed carrier state identity; clients use it when producing a Candidate. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Relay")
	FGuLiCarrierSourceRef LatestCarrierSource;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Relay")
	uint32 MatchEpoch = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Relay")
	uint32 ConnectionGeneration = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Relay")
	uint32 RosterRevision = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Relay")
	FGuLiWingmanRelayValidationRevisions ValidationRevisions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Relay")
	FGuLiWingmanUploadRateGrant UploadRateGrant;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Relay")
	uint32 LastAcceptedCandidateSequence = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Relay")
	uint32 StateRevision = 0u;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGuLiWingmanBootstrapReceivedSignature,
	const FGuLiWingmanBootstrapBundle&, Bootstrap);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGuLiWingmanAcceptedBatchSignature,
	const FGuLiWingmanAcceptedBatch&, AcceptedBatch);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGuLiWingmanSubmissionResultSignature,
	int64, Sequence, EGuLiWingmanSubmissionDisposition, Disposition,
	EGuLiWingmanRejectReason, RejectReason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGuLiWingmanAtomicBatchResultSignature,
	const FGuLiWingmanAtomicBatchAcceptance&, Acceptance);
class FGuLiWingmanRelayAuthorityRegistry;
class UGuLiWingmanSimulationSubsystem;
struct FGuLiWingmanTargetObservation;

/**
 * PlayerController-owned RPC transport for a GameState-lifetime FGuLiWingmanRelayServer. Destroying this
 * component never destroys the six authoritative scopes. All mutation RPCs validate the owning PlayerState
 * GUID again on authority.
 */
UCLASS(ClassGroup = (GuLiStrike), meta = (BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiWingmanRelayComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGuLiWingmanRelayComponent();
	virtual ~UGuLiWingmanRelayComponent() override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * Authority-only setup. Installs the fail-closed World validator before publishing the
	 * deterministic 25-member, six-scope cut so a listen host cannot race its first atomic batch.
	 */
	bool ServerInitializeGroup(uint32 MatchEpoch, const FGuLiWingmanGroupHandle& Group,
		const FGuid& LeaseOwnerPlayerGuid, const FGuid& BackupPlayerGuid,
		const FGuLiGroupAbilityConfigSnapshot& AbilityConfig,
		FGuLiCandidateWorldValidator CandidateWorldValidator,
		const FGuLiWingmanRelayTuning& Tuning = FGuLiWingmanRelayTuning{});
	bool ServerPublishAbilityConfig(const FGuLiGroupAbilityConfigSnapshot& AbilityConfig);
	bool ServerBeginTakeover(const FGuid& NewOwnerPlayerGuid, const FGuid& NewBackupPlayerGuid);
	bool ServerBeginResume();
	/** Publishes an Active roster mutation without starting a Resume transaction. */
	bool ServerRefreshActiveRosterCut();
	bool ServerIssueHighRateUploadGrant(uint32 EffectiveClientSimTick,
		EGuLiWingmanUploadRateGrantReason Reason);
	void ServerRevokeGroup();
	/** Binds this transport to an already retained group after takeover/reconnect. */
	bool ServerAttachPersistentGroup(const FGuLiWingmanGroupHandle& Group);
	/** Delivers a preview to the exact proposed owner; it does not attach or commit the group. */
	bool ServerDeliverLeaseOffer(const FGuLiWingmanGroupHandle& Group);
	void ServerDetachPersistentGroup(const FGuLiWingmanGroupHandle& Group);
	bool CanServerAttachPersistentGroup() const;

	UFUNCTION(BlueprintCallable, Category = "Wingman|Relay")
	void SubmitCandidate(const FGuLiWingmanCandidateBatch& Candidate);

	UFUNCTION(BlueprintCallable, Category = "Wingman|Relay")
	void SubmitFireIntent(const FGuLiWingmanFireIntent& Intent);

	/** Requests same-Lease recovery from the last accepted per-Flight baseline. */
	UFUNCTION(BlueprintCallable, Category = "Wingman|Relay")
	void RequestResume();

	UFUNCTION(BlueprintPure, Category = "Wingman|Relay")
	const FGuLiWingmanRelayReplicatedState& GetRelayState() const { return ReplicatedState; }

	const FGuLiWingmanBootstrapBundle& GetLastClientBootstrap() const { return LastClientBootstrap; }
	FGuLiWingmanRelayServer* GetServerRelay() { return ServerRelay; }
	const FGuLiWingmanRelayServer* GetServerRelay() const { return ServerRelay; }
	/** Read-only counters populated only by an explicit Non-Shipping Listen smoke. */
	uint32 GetListenSmokeAtomicBuildAttemptCount() const { return ListenSmokeAtomicBuildAttemptCount; }
	uint32 GetListenSmokeAtomicBuildPreconditionFailureCount() const
	{
		return ListenSmokeAtomicBuildPreconditionFailureCount;
	}
	uint32 GetListenSmokeAtomicBuildMissingSimulationFailureCount() const
	{
		return ListenSmokeAtomicBuildMissingSimulationFailureCount;
	}
	uint32 GetListenSmokeAtomicBuildFlightCandidateFailureCount() const
	{
		return ListenSmokeAtomicBuildFlightCandidateFailureCount;
	}
	int32 GetListenSmokeAtomicBuildLastFailedFlightIndex() const
	{
		return ListenSmokeAtomicBuildLastFailedFlightIndex;
	}
	uint32 GetListenSmokeAtomicBuildEmptyFailureCount() const
	{
		return ListenSmokeAtomicBuildEmptyFailureCount;
	}
	uint32 GetListenSmokeAtomicBuildFragmentFailureCount() const
	{
		return ListenSmokeAtomicBuildFragmentFailureCount;
	}
	uint32 GetListenSmokeAtomicBuildSubmittedCount() const { return ListenSmokeAtomicBuildSubmittedCount; }
	uint32 GetListenSmokeAtomicResultCount() const { return ListenSmokeAtomicResultCount; }
	EGuLiWingmanSubmissionDisposition GetListenSmokeLastAtomicResultDisposition() const
	{
		return ListenSmokeLastAtomicResultDisposition;
	}
	EGuLiWingmanRejectReason GetListenSmokeLastAtomicResultRejectReason() const
	{
		return ListenSmokeLastAtomicResultRejectReason;
	}
	uint32 GetListenSmokeOwnerEligibleTickCount() const { return ListenSmokeOwnerEligibleTickCount; }
	uint32 GetListenSmokeOwnerInactiveGateCount() const { return ListenSmokeOwnerInactiveGateCount; }
	uint32 GetListenSmokeOwnerMissingRuntimeGateCount() const
	{
		return ListenSmokeOwnerMissingRuntimeGateCount;
	}
	uint32 GetListenSmokeNormalBuildAttemptCount() const { return ListenSmokeNormalBuildAttemptCount; }
	uint32 GetListenSmokeNormalBuildFailureCount() const { return ListenSmokeNormalBuildFailureCount; }
	int32 GetListenSmokeNormalBuildLastFailedFlightIndex() const
	{
		return ListenSmokeNormalBuildLastFailedFlightIndex;
	}
	uint32 GetListenSmokeNormalSubmittedCount() const { return ListenSmokeNormalSubmittedCount; }
	uint32 GetListenSmokeNormalResultCount() const { return ListenSmokeNormalResultCount; }
	uint32 GetListenSmokeNormalAcceptedCount() const { return ListenSmokeNormalAcceptedCount; }
	uint8 GetListenSmokeLastNormalFlightModeMask() const { return ListenSmokeLastNormalFlightModeMask; }
	uint8 GetListenSmokeLastAtomicFlightModeMask() const { return ListenSmokeLastAtomicFlightModeMask; }
	EGuLiWingmanSubmissionDisposition GetListenSmokeLastNormalResultDisposition() const
	{
		return ListenSmokeLastNormalResultDisposition;
	}
	EGuLiWingmanRejectReason GetListenSmokeLastNormalResultRejectReason() const
	{
		return ListenSmokeLastNormalResultRejectReason;
	}
	uint32 GetListenSmokeClientSimulationTick() const { return ClientSimulationTick; }
	uint32 GetListenSmokeNextFrameSequence(const uint8 FlightIndex) const
	{
		return FlightIndex < GULI_WINGMAN_FLIGHT_COUNT
			? NextClientFrameSequenceByFlight[FlightIndex] : 0u;
	}
	uint32 GetListenSmokeAcceptedSequence(const uint8 FlightIndex) const
	{
		return FlightIndex < GULI_WINGMAN_FLIGHT_COUNT
			? ClientAcceptedSequenceByFlight[FlightIndex] : 0u;
	}
	/** Authority-only Ship hook; stored durably in the GameState registry, not this transport. */
	bool SetServerCandidateWorldValidator(FGuLiCandidateWorldValidator InValidator);
	void SetServerFireIntentValidator(FGuLiFireIntentServerValidator InValidator);
	FGuLiServerFireIntentAcceptedSignature& OnServerFireIntentAccepted();

	UPROPERTY(BlueprintAssignable, Category = "Wingman|Relay")
	FGuLiWingmanBootstrapReceivedSignature OnBootstrapReceived;

	UPROPERTY(BlueprintAssignable, Category = "Wingman|Relay")
	FGuLiWingmanAcceptedBatchSignature OnAcceptedBatch;

	UPROPERTY(BlueprintAssignable, Category = "Wingman|Relay")
	FGuLiWingmanSubmissionResultSignature OnCandidateResult;

	UPROPERTY(BlueprintAssignable, Category = "Wingman|Relay")
	FGuLiWingmanSubmissionResultSignature OnFireIntentResult;

	UPROPERTY(BlueprintAssignable, Category = "Wingman|Relay")
	FGuLiWingmanAtomicBatchResultSignature OnAtomicBatchResult;

private:
	UFUNCTION(Server, Unreliable)
	void ServerSubmitCandidate(const FGuLiWingmanCandidateBatch& Candidate);

	UFUNCTION(Server, Reliable)
	void ServerSubmitFireIntent(const FGuLiWingmanFireIntent& Intent);

	UFUNCTION(Server, Reliable)
	void ServerSubmitAtomicCandidateFragment(const FGuLiWingmanAtomicCandidateBatchFragment& Fragment);

	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeAbilityConfig(const FGuLiGroupAbilityConfigAck& Ack);

	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeBootstrap(const FGuLiWingmanBootstrapCommit& Commit);

	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeTransferBootstrap(const FGuLiWingmanBootstrapCommit& Commit,
		const FGuLiWingmanTransferBaseline& TransferBaseline);

	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeLeaseOfferReady(const FGuLiWingmanGroupHandle& Group, uint32 OfferRevision);

	UFUNCTION(Server, Reliable)
	void ServerRequestResume(const FGuLiWingmanGroupHandle& Group, uint32 LeaseEpoch);

	UFUNCTION(Server, Unreliable)
	void ServerSendLeaseHeartbeat(const FGuLiWingmanGroupHandle& Group,
		uint32 ConnectionGeneration, uint32 LeaseEpoch);

	UFUNCTION(Client, Reliable)
	void ClientReceiveBootstrap(const FGuLiWingmanBootstrapBundle& Bootstrap);

	UFUNCTION(Client, Reliable)
	void ClientReceiveLeaseOffer(const FGuLiWingmanGroupHandle& Group, uint32 OfferRevision,
		double ReadyDeadlineSeconds, double OverallDeadlineSeconds);

	UFUNCTION(Client, Unreliable)
	void ClientReceiveCandidateResult(const FGuLiWingmanCandidateResultWire& Result);

	UFUNCTION(Client, Reliable)
	void ClientReceiveAtomicBatchResult(const FGuLiWingmanAtomicBatchAcceptance& Acceptance,
		const TArray<FGuLiWingmanAcceptedBatch>& AcceptedFlights);

	UFUNCTION(Client, Reliable)
	void ClientReceiveUploadRateGrant(const FGuLiWingmanUploadRateGrant& Grant);

	UFUNCTION(Client, Unreliable)
	void ClientReceiveFireIntentResult(uint32 Sequence, EGuLiWingmanSubmissionDisposition Disposition,
		EGuLiWingmanRejectReason RejectReason);

	UFUNCTION()
	void OnRep_RelayState();

	FGuid GetOwningPlayerGuid() const;
	double GetAuthorityTimeSeconds() const;
	EGuLiRelayCarrierLookupResult ResolveCarrierSource(const FGuLiCarrierSourceRef& Source,
		FGuLiRelayCarrierState& OutState) const;
	bool GetLatestCarrierSource(FGuLiCarrierSourceRef& OutSource) const;
	FGuLiWingmanRelayAuthorityRegistry* GetAuthorityRegistry() const;
	void InstallCarrierResolvers(const FGuLiWingmanGroupHandle& Group);
	bool TryCommitClientBootstrap();
	bool BuildAndSubmitAtomicClientBaseline();
	void ProcessServerBootstrapAcknowledgement(const FGuLiWingmanBootstrapCommit& Commit,
		const FGuLiWingmanTransferBaseline* TransferBaseline);
	bool ApplyAcceptedBatchToLocalOwner(const FGuLiWingmanAcceptedBatch& AcceptedBatch);
	void TickOwnerClientSimulation(float DeltaTime);
	uint32 AdvanceClientSimulationClock(float DeltaTime);
	void CapturePrivateOwnerTrajectory(UGuLiWingmanSimulationSubsystem& Simulation);
	void TickOwnerBasicWeapon(double NowSeconds);
	void GatherBasicWeaponTargets(
		double EstimatedServerNowSeconds,
		TArray<FGuLiWingmanTargetObservation>& OutTargets) const;
	bool HasClientLineOfSight(
		const FVector& SourceLocation,
		const FGuLiWingmanTargetObservation& Target) const;
	double GetEstimatedServerTimeSeconds() const;
	void DestroyClientOwnedGroup();
	void RefreshReplicatedState();
	bool BuildAndSendBootstrap();
	void HandleCandidateResult(const FGuLiWingmanSubmissionResult& Result);
	bool ConsumeValidatedCandidateResultWire(
		const FGuLiWingmanCandidateResultWire& Result,
		bool bBroadcastResult);
	uint32 GetCurrentConnectionGeneration() const;
	uint8 BuildClientRequiredMemberMask(uint8 FlightIndex) const;
	void ApplyClientUploadRateGrant(const FGuLiWingmanUploadRateGrant& Grant);

	UPROPERTY(ReplicatedUsing = OnRep_RelayState)
	FGuLiWingmanRelayReplicatedState ReplicatedState;

	UPROPERTY(Transient)
	FGuLiWingmanBootstrapBundle LastClientBootstrap;

	/** Non-owning pointer into AGuLiBattleGameState::WingmanRelayAuthorityRegistry. */
	FGuLiWingmanRelayServer* ServerRelay = nullptr;
	FGuLiWingmanGroupHandle BoundServerGroup;
	FGuLiServerFireIntentAcceptedSignature ServerFireIntentAccepted;
	FGuLiFireIntentServerValidator ServerFireIntentValidator;
	FGuLiWingmanTokenBucket AcknowledgementBucket;
	bool bClientBootstrapPending = false;
	bool bClientBootstrapLocallyApplied = false;
	bool bClientAtomicBaselineSubmitted = false;
	bool bClientAtomicBaselineAccepted = false;
	bool bClientAtomicBootstrapAcksSubmitted = false;
	double NextClientAtomicBaselineRetryTimeSeconds = 0.0;
	FGuLiWingmanGroupHandle LastAcknowledgedGroup;
	uint32 LastAcknowledgedLeaseEpoch = 0u;
	uint64 LastAcknowledgedCutId = 0u;
	double ClientCandidateAccumulator = 0.0;
	double ClientHeartbeatAccumulator = 0.0;
	uint32 LastRequestedResumeLeaseEpoch = 0u;
	uint32 NextClientCandidateSequence = 1u;
	uint32 ClientSimulationTick = 1u;
	/** Persistent fair scheduler for ordinary per-Flight uploads. */
	uint8 NextClientFlightUploadCursor = 0u;
	FGuLiAcceptedStateRef LastAppliedAcceptedState;
	FGuLiWingmanAcceptedBatch LastClientAcceptedBatch;
	TStaticArray<FGuLiWingmanAcceptedBatch, GULI_WINGMAN_FLIGHT_COUNT> LastClientAcceptedByFlight{};
	TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> NextClientFrameSequenceByFlight{};
	TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> ClientAcceptedSequenceByFlight{};
	TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> LastSubmittedClientTickByFlight{};
	/** Owner-private 5 Hz rolling history; only a four-sample selection ever reaches the wire. */
	TStaticArray<TArray<FGuLiWingmanCandidateTrailSample>, GULI_WINGMAN_FLIGHT_COUNT>
		RetainedTrajectoryByFlight{};
	TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> LastRetainedTrajectoryTickByFlight{};
	FGuLiWingmanUploadRateGrant ClientUploadRateGrant;
	uint64 NextClientAtomicBatchId = 1u;
	TArray<double> NextBasicTargetScanSeconds;
	uint32 ListenSmokeAtomicBuildAttemptCount = 0u;
	uint32 ListenSmokeAtomicBuildPreconditionFailureCount = 0u;
	uint32 ListenSmokeAtomicBuildMissingSimulationFailureCount = 0u;
	uint32 ListenSmokeAtomicBuildFlightCandidateFailureCount = 0u;
	int32 ListenSmokeAtomicBuildLastFailedFlightIndex = INDEX_NONE;
	uint32 ListenSmokeAtomicBuildEmptyFailureCount = 0u;
	uint32 ListenSmokeAtomicBuildFragmentFailureCount = 0u;
	uint32 ListenSmokeAtomicBuildSubmittedCount = 0u;
	uint32 ListenSmokeAtomicResultCount = 0u;
	EGuLiWingmanSubmissionDisposition ListenSmokeLastAtomicResultDisposition =
		EGuLiWingmanSubmissionDisposition::Rejected;
	EGuLiWingmanRejectReason ListenSmokeLastAtomicResultRejectReason = EGuLiWingmanRejectReason::None;
	uint32 ListenSmokeOwnerEligibleTickCount = 0u;
	uint32 ListenSmokeOwnerInactiveGateCount = 0u;
	uint32 ListenSmokeOwnerMissingRuntimeGateCount = 0u;
	uint32 ListenSmokeNormalBuildAttemptCount = 0u;
	uint32 ListenSmokeNormalBuildFailureCount = 0u;
	int32 ListenSmokeNormalBuildLastFailedFlightIndex = INDEX_NONE;
	uint32 ListenSmokeNormalSubmittedCount = 0u;
	uint32 ListenSmokeNormalResultCount = 0u;
	uint32 ListenSmokeNormalAcceptedCount = 0u;
	uint8 ListenSmokeLastNormalFlightModeMask = 0u;
	uint8 ListenSmokeLastAtomicFlightModeMask = 0u;
	EGuLiWingmanSubmissionDisposition ListenSmokeLastNormalResultDisposition =
		EGuLiWingmanSubmissionDisposition::Rejected;
	EGuLiWingmanRejectReason ListenSmokeLastNormalResultRejectReason = EGuLiWingmanRejectReason::None;
};
