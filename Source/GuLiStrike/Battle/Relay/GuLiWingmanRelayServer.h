// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Relay/GuLiWingmanRelayTypes.h"

struct GULISTRIKE_API FGuLiRelayCarrierState
{
	FTransform Transform = FTransform::Identity;
	FVector Velocity = FVector::ZeroVector;
	double ServerWorldTimeSeconds = 0.0;

	bool IsWellFormed() const;
};

using FGuLiCarrierSourceResolver = TFunction<EGuLiRelayCarrierLookupResult(
	const FGuLiCarrierSourceRef& Source, FGuLiRelayCarrierState& OutState)>;
using FGuLiFireIntentServerValidator = TFunction<EGuLiWingmanRejectReason(
	const FGuLiWingmanFireIntent& Intent, const FGuLiWingmanAcceptedBatch& SourceBatch)>;
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiServerFireIntentAcceptedSignature,
	const FGuLiWingmanFireIntent&);

/** One member's accepted-to-candidate line segment. First observations validate as a zero-length endpoint. */
struct GULISTRIKE_API FGuLiWingmanCandidateWorldSegment
{
	FGuLiWingmanHandle Wingman;
	FVector PreviousPosition = FVector::ZeroVector;
	FVector CurrentPosition = FVector::ZeroVector;
	bool bHasPreviousAcceptedSample = false;
};

/**
 * UObject-free input for the injected authority World/FlightNav gate. All pointers are valid only for
 * the synchronous callback. The callback may inspect the world, but must never create movement state.
 */
struct GULISTRIKE_API FGuLiWingmanCandidateWorldValidationContext
{
	const FGuLiWingmanCandidateBatch* Candidate = nullptr;
	const FGuLiRelayCarrierState* Carrier = nullptr;
	const FGuLiGroupAbilityConfigSnapshot* ConfirmedAbilityConfig = nullptr;
	TArray<FGuLiWingmanCandidateWorldSegment> Segments;

	bool IsWellFormed() const;
};

using FGuLiCandidateWorldValidator = TFunction<EGuLiWingmanRejectReason(
	const FGuLiWingmanCandidateWorldValidationContext& Context)>;

/** Server-only validation and storage tuning. Values are runtime state, never accepted from a client. */
struct GULISTRIKE_API FGuLiWingmanRelayTuning
{
	double CarrierPendingTimeoutSeconds = GULI_CARRIER_SOURCE_PENDING_TIMEOUT_SECONDS;
	/** Lease watchdog is scheduled by GameState exactly once per second. */
	double LeaseMaintenanceIntervalSeconds = 1.0;
	double InitialCandidateDeadlineSeconds = 3.0;
	double OfferReadyDeadlineSeconds = 3.0;
	double TakeoverAcknowledgementDeadlineSeconds = 3.0;
	double TakeoverCandidateDeadlineSeconds = 3.0;
	double TakeoverOverallDeadlineSeconds = 10.0;
	double ActiveToStaleSeconds = 1.0;
	double StaleToUnavailableSeconds = 2.0;
	double UnavailableToRevokeSeconds = 3.0;
	double HeartbeatIntervalSeconds = 1.0;
	double CandidateBucketCapacity = 60.0;
	double CandidateTokensPerSecond = 55.0;
	double FireBucketCapacity = 100.0;
	double FireTokensPerSecond = 50.0;
	double MaximumFireSourceAgeSeconds = 0.35;
	double MaximumCarrierDistanceCentimeters = GULI_WINGMAN_MAXIMUM_CARRIER_DISTANCE_CENTIMETERS;
	double MaximumWingmanSpeedCentimetersPerSecond = 12000.0;
	/** Quantization allowance around the ability-authorized endpoint speed. */
	double SpeedEnvelopeSlackCentimetersPerSecond = 10.0;
	/** Angular quantization allowance added once per received Candidate packet. */
	double TurnEnvelopeSlackDegrees = 0.5;
	/** Position quantization/integration allowance added to the physical reachability envelope. */
	double PositionEnvelopeSlackCentimeters = 100.0;
	/** Bounds how far client fixed-step time may run ahead of elapsed authoritative receive time. */
	double MaximumClientSimulationLeadSeconds = 0.25;
	double MaximumCaptureFutureSkewSeconds = 0.1;
	/**
	 * Covers a legal Resume submitted just before the three-second lease revoke boundary,
	 * including cross-channel delivery skew. This remains bounded and is paired with the
	 * Ship's 256-entry, 60 Hz canonical carrier history (4.25 seconds between endpoints).
	 */
	double MaximumCaptureHistorySeconds = 3.5;
	double UploadIntervalToleranceSeconds = 0.01;
	double HighRateGrantDurationSeconds = 2.0;
	double ClientSimulationHz = 30.0;
	int32 MaximumAcceptedHistoryBatches = 64;

	bool IsWellFormed() const;
};

enum class EGuLiWingmanPendingLeaseState : uint8
{
	None = 0,
	AwaitingOfferReady
};

enum class EGuLiWingmanActiveLeaseTransactionState : uint8
{
	None = 0,
	AwaitingBaselineAck,
	AwaitingTakeoverBatch,
	NoOwner
};

enum class EGuLiWingmanLeaseEventType : uint8
{
	Maintenance = 0,
	InitialCandidateExpired,
	BecameStale,
	BecameUnavailable,
	ActiveLeaseRevoked,
	OfferStarted,
	OfferReadyExpired,
	OfferCommitted,
	BaselineAcknowledged,
	AtomicBatchAccepted,
	TransactionExpired,
	NoOwner,
	ResumeStarted,
	HeartbeatReceived,
	BaselineRevised
};

/** Pending preview. It never changes or pauses the active lease/freshness clock. */
struct GULISTRIKE_API FGuLiWingmanPendingLeaseOffer
{
	EGuLiWingmanPendingLeaseState State = EGuLiWingmanPendingLeaseState::None;
	uint32 OfferRevision = 0u;
	FGuid ProposedOwnerPlayerGuid;
	FGuid ProposedBackupPlayerGuid;
	double IssuedTimeSeconds = 0.0;
	double ReadyDeadlineSeconds = 0.0;
	double OverallDeadlineSeconds = 0.0;
	uint32 PreviewRosterRevision = 0u;
	uint64 PreviewAcceptedSnapshotHash = 0u;
	uint32 PreviewAbilityConfigRevision = 0u;
	uint64 PreviewAbilityConfigHash = 0u;

	bool IsPending() const { return State == EGuLiWingmanPendingLeaseState::AwaitingOfferReady; }
	bool IsWellFormed() const;
};

/** Post-commit transaction. The frozen six-scope cut is held by OutstandingBootstrap. */
struct GULISTRIKE_API FGuLiWingmanActiveLeaseTransaction
{
	EGuLiWingmanActiveLeaseTransactionState State = EGuLiWingmanActiveLeaseTransactionState::None;
	EGuLiWingmanAtomicBatchKind Kind = EGuLiWingmanAtomicBatchKind::Bootstrap;
	double OverallDeadlineSeconds = 0.0;
	double BaselineAckDeadlineSeconds = 0.0;
	double CandidateDeadlineSeconds = 0.0;
	uint32 FrozenRosterRevision = 0u;
	uint32 FrozenBaselineRevision = 0u;
	uint64 FrozenBaselineHash = 0u;
	uint64 FrozenAcceptedSnapshotHash = 0u;
	uint64 FrozenFireHighWaterHash = 0u;
	uint32 FrozenAbilityConfigRevision = 0u;
	uint64 FrozenAbilityConfigHash = 0u;

	bool IsTransfer() const { return Kind == EGuLiWingmanAtomicBatchKind::Takeover; }
	bool IsResume() const { return Kind == EGuLiWingmanAtomicBatchKind::Resume; }
};

/** Audit record for every watchdog/transaction boundary. Detection lag is never hidden. */
struct GULISTRIKE_API FGuLiWingmanLeaseEvent
{
	EGuLiWingmanLeaseEventType Type = EGuLiWingmanLeaseEventType::Maintenance;
	FGuLiWingmanGroupHandle Group;
	uint32 LeaseEpoch = 0u;
	double TheoreticalDeadlineSeconds = 0.0;
	double DetectedTimeSeconds = 0.0;
	double DetectionLagSeconds = 0.0;
};

/**
 * Game-thread, server-only ClientValidatedRelay core. It validates owner-produced samples and stores
 * exact accepted values. There is deliberately no movement tick, steering, integration, or extrapolation API.
 */
class GULISTRIKE_API FGuLiWingmanRelayServer
{
public:
	bool InitializeGroup(uint32 MatchEpoch, const FGuLiWingmanGroupHandle& Group,
		const FGuid& InitialOwnerPlayerGuid, const FGuid& BackupPlayerGuid,
		const FGuLiGroupAbilityConfigSnapshot& InitialAbilityConfig, double NowSeconds,
		const FGuLiWingmanRelayTuning& InTuning = FGuLiWingmanRelayTuning{});

	bool PublishAbilityConfig(const FGuLiGroupAbilityConfigSnapshot& NewConfig, double NowSeconds);
	/** Enables the strict Flight contract. Kept separate from InitializeGroup for UObject-free legacy fixtures. */
	bool ConfigureStrictFlightContract(uint32 ConnectionGeneration,
		const FGuLiWingmanRelayValidationRevisions& Revisions, double NowSeconds);
	/** Authority-only signal (combat/hazard), never called from a client Candidate request. */
	bool IssueHighRateGrant(uint32 EffectiveClientSimTick,
		EGuLiWingmanUploadRateGrantReason Reason, double NowSeconds,
		FGuLiWingmanUploadRateGrant& OutGrant);
	/** Creates a preview only. The current active lease and its freshness continue unchanged. */
	bool BeginLeaseOffer(const FGuid& ProposedOwnerPlayerGuid, const FGuid& ProposedBackupPlayerGuid,
		double NowSeconds, FGuLiWingmanPendingLeaseOffer& OutOffer);
	/** Exact Ready gate. Success commits the new lease and freezes its first takeover baseline. */
	bool AcknowledgeLeaseOfferReady(const FGuid& SenderPlayerGuid, uint32 OfferRevision,
		double NowSeconds);
	/** Explicit retained no-owner state; authoritative scopes are not destroyed. */
	bool EnterNoOwner(double NowSeconds);
	bool BeginTakeover(const FGuid& NewOwnerPlayerGuid, const FGuid& NewBackupPlayerGuid, double NowSeconds);
	/** Invalidates a disconnected owner's epoch while retaining all six authoritative scopes. */
	bool SuspendForOwnerLoss(const FGuid& NoOwnerSentinelGuid, double NowSeconds);
	/**
	 * Starts an atomic same-owner recovery. A watchdog-revoked active lease may
	 * reclaim its retained state only while no backup Offer or committed transfer
	 * transaction is pending.
	 */
	bool BeginResume(double NowSeconds);
	/** Explicit whole-group ability discontinuity; never reachable from Candidate/EmergencyRebase RPCs. */
	bool BeginExternalControl(double NowSeconds);
	bool CommitExternalGroupDisplacement(const TMap<FGuLiWingmanHandle, FTransform>& Positions,
		const FGuLiCarrierSourceRef& CarrierSource, double NowSeconds, TArray<FGuLiWingmanAcceptedBatch>& OutBaselines);
	bool PrepareExternalGroupDisplacement(const TMap<FGuLiWingmanHandle, FTransform>& Positions,
		const FGuLiCarrierSourceRef& CarrierSource, double NowSeconds, TArray<FGuLiWingmanAcceptedBatch>& OutBaselines) const;
	void ReleaseExternalControl(double NowSeconds);
	bool AcknowledgeExternalDisplacement(const FGuid& Owner, uint32 Epoch, double NowSeconds);
	bool IsPhased() const { return bPhased; }
	bool IsExternallyControlled() const { return bExternalActionsLocked || bExternalBaselineAwaitingAck; }
	uint32 GetExternalDisplacementRevision() const { return ExternalDisplacementRevision; }
	/** Builds the reliable ACK barrier requested by an Active roster mutation. */
	bool RefreshActiveRosterCut(double NowSeconds, FGuLiWingmanBootstrapBundle& OutBundle);
	bool RecordLeaseHeartbeat(const FGuid& SenderPlayerGuid, uint32 RequestConnectionGeneration,
		uint32 RequestLeaseEpoch, double NowSeconds);
	void Revoke(double NowSeconds);

	bool BuildBootstrap(FGuLiWingmanBootstrapBundle& OutBundle);
	bool AcknowledgeAbilityConfig(const FGuid& SenderPlayerGuid, const FGuLiGroupAbilityConfigAck& Ack,
		double NowSeconds);
	bool AcknowledgeBootstrap(const FGuid& SenderPlayerGuid, const FGuLiWingmanBootstrapCommit& AppliedCommit,
		const FGuLiWingmanTransferBaseline* AppliedTransferBaseline, double NowSeconds);

	FGuLiWingmanSubmissionResult SubmitCandidate(const FGuid& SenderPlayerGuid,
		const FGuLiWingmanCandidateBatch& Candidate, double NowSeconds,
		const FGuLiCarrierSourceResolver& CarrierResolver,
		const FGuLiCandidateWorldValidator& WorldValidator = FGuLiCandidateWorldValidator{});
	FGuLiWingmanSubmissionResult SubmitFireIntent(const FGuid& SenderPlayerGuid,
		const FGuLiWingmanFireIntent& Intent, double NowSeconds,
		const FGuLiFireIntentServerValidator& AdditionalValidator = FGuLiFireIntentServerValidator{});
	/**
	 * Validates one stuck member against the current lease and Accepted Store, then
	 * chooses an authority-owned safe point. The client never supplies a destination.
	 */
	FGuLiWingmanEmergencyRebaseResponse SubmitEmergencyRebase(
		const FGuid& SenderPlayerGuid,
		const FGuLiWingmanEmergencyRebaseRequest& Request,
		double NowSeconds,
		const FGuLiCarrierSourceResolver& CarrierResolver,
		const FGuLiCandidateWorldValidator& WorldValidator,
		FGuLiWingmanAcceptedBatch& OutAcceptedBatch);
	FGuLiWingmanAtomicBatchAcceptance SubmitAtomicCandidateFragment(
		const FGuid& SenderPlayerGuid,
		const FGuLiWingmanAtomicCandidateBatchFragment& Fragment,
		double NowSeconds,
		const FGuLiCarrierSourceResolver& CarrierResolver,
		const FGuLiCandidateWorldValidator& WorldValidator = FGuLiCandidateWorldValidator{});

	/** Packet/assembler deadlines only. Safe at transport cadence; never evaluates a lease. */
	void AdvancePacketDeadlines(double NowSeconds, const FGuLiCarrierSourceResolver& CarrierResolver);
	/** Compatibility/test facade. Production transports call AdvancePacketDeadlines instead. */
	void AdvanceTime(double NowSeconds, const FGuLiCarrierSourceResolver& CarrierResolver);
	/** Exact group-level 1 Hz lease watchdog. Early calls are ignored. No entity loop is performed. */
	bool RunLeaseMaintenance(double NowSeconds);
	void DrainDeferredCandidateResults(TArray<FGuLiWingmanSubmissionResult>& OutResults);
	void DrainDeferredAtomicBatchResults(TArray<FGuLiWingmanAtomicBatchAcceptance>& OutResults);

	bool MarkWingmanDead(const FGuLiWingmanHandle& Wingman);
	bool SetWingmanHealthPermille(const FGuLiWingmanHandle& Wingman, uint16 CurrentHealthPermille);
	bool ReplenishWingman(uint8 FlightIndex, uint8 MemberIndex, uint32 NewEntityGeneration);

    FGuLiWingmanAttackAuthorityState AttackState;
    TFunction<void(const FGuLiWingmanCandidateBatch&, double)> OnValidatedAttackBatch;
	TFunction<void(const FGuLiWingmanHandle&)> OnEmergencyRebaseAccepted;

	const FGuLiWingmanLeaseState& GetLeaseState() const { return LeaseState; }
	uint32 GetMatchEpoch() const { return MatchEpoch; }
	const FGuLiGroupAbilityConfigSnapshot& GetAbilityConfig() const { return AbilityConfig; }
	const FGuLiWingmanUploadRateGrant& GetUploadRateGrant() const { return UploadRateGrant; }
	const FGuLiWingmanRelayValidationRevisions& GetValidationRevisions() const { return ValidationRevisions; }
	uint32 GetConnectionGeneration() const { return ConnectionGeneration; }
	uint32 GetRosterRevision() const { return RosterRevision; }
	uint32 GetAcceptedSequenceForFlight(uint8 FlightIndex) const;
	uint32 GetLastAcceptedFrameForFlight(uint8 FlightIndex) const;
	double GetLastValidCandidateTimeForFlight(uint8 FlightIndex) const
	{
		return FlightIndex < GULI_WINGMAN_FLIGHT_COUNT
			? LastValidCandidateTimeByFlight[FlightIndex] : 0.0;
	}
	const TArray<FGuLiWingmanRosterEntry>& GetRoster() const { return Roster; }
	const TArray<FGuLiWingmanHealthEntry>& GetHealth() const { return Health; }
	const TArray<FGuLiWingmanAcceptedBatch>& GetAcceptedHistory() const { return AcceptedHistory; }
	const FGuLiWingmanAcceptedBatch* FindAcceptedBatch(const FGuLiAcceptedStateRef& StateRef) const;
	/** O(1) current Accepted Store lookup used by event-driven combat adapters. */
	bool TryGetLatestAcceptedSample(
		const FGuLiWingmanHandle& Wingman,
		FGuLiWingmanCandidateSample& OutSample,
		double* OutAcceptedTimeSeconds = nullptr) const;
	uint32 GetLastAcceptedCandidateSequence() const { return LastAcceptedCandidateSequence; }
	uint64 GetServerWingmanMovementWriteCount() const { return ServerWingmanMovementWriteCount; }
	uint64 GetEmergencyRebaseAcceptedCount() const { return EmergencyRebaseAcceptedCount; }
	bool IsTransferInProgress() const { return bTransferInProgress; }
	bool IsAbilityConfigAcknowledged() const { return bAbilityConfigAcknowledged; }
	bool IsBootstrapAcknowledged() const { return bBootstrapAcknowledged; }
	/** Read-only Non-Shipping smoke observability; never advances the transaction. */
	bool HasOutstandingBootstrap() const { return OutstandingBootstrap.IsWellFormed(); }
	bool IsAtomicCandidateBatchRequired() const { return bRequiresAtomicCandidateBatch; }
	bool IsAtomicCandidateBatchCommitted() const { return bAtomicCandidateBatchCommitted; }
	EGuLiWingmanAtomicBatchKind GetRequiredAtomicBatchKind() const { return RequiredAtomicBatchKind; }
	const FGuLiWingmanPendingLeaseOffer& GetPendingLeaseOffer() const { return PendingLeaseOffer; }
	const FGuLiWingmanActiveLeaseTransaction& GetActiveLeaseTransaction() const
	{
		return ActiveLeaseTransaction;
	}
	const TArray<FGuLiWingmanLeaseEvent>& GetLeaseEvents() const { return LeaseEvents; }
	double GetInitialCandidateDeadlineSeconds() const { return InitialCandidateDeadlineTimeSeconds; }
	double GetLastLeaseMaintenanceTimeSeconds() const { return LastLeaseMaintenanceTimeSeconds; }
	double GetNextLeaseMaintenanceTimeSeconds() const { return NextLeaseMaintenanceTimeSeconds; }
	double GetLastHeartbeatTimeSeconds() const { return LastHeartbeatTimeSeconds; }
	uint64 GetLeaseMaintenanceExecutionCount() const { return LeaseMaintenanceExecutionCount; }
	bool IsActiveLeaseRevoked() const { return bActiveLeaseRevoked; }
	bool IsActiveRosterCutPending() const { return bActiveRosterCutPending; }

private:
	struct FPendingCandidate
	{
		FGuid SenderPlayerGuid;
		FGuLiWingmanCandidateBatch Batch;
		FGuLiCandidateWorldValidator WorldValidator;
		double ReceivedTimeSeconds = 0.0;
		double DeadlineSeconds = 0.0;
	};

	struct FLastAcceptedSample
	{
		FGuLiWingmanCandidateSample Sample;
		uint32 ClientSimTick = 0u;
		double ServerAcceptedTimeSeconds = 0.0;
		double LowSpeedStartTimeSeconds = -1.0;
	};

	struct FValidatedCandidate
	{
		FGuLiWingmanAcceptedBatch Accepted;
		TArray<FGuLiWingmanCandidateWorldSegment> WorldSegments;
	};

	bool IsCurrentOwner(const FGuid& SenderPlayerGuid) const;
	EGuLiWingmanRejectReason ValidateCommonRequest(const FGuid& SenderPlayerGuid,
		const FGuLiWingmanGroupHandle& Group, uint32 RequestMatchEpoch, uint32 RequestLeaseEpoch) const;
	EGuLiWingmanRejectReason ValidateCandidateBeforeCarrier(const FGuLiWingmanCandidateBatch& Candidate,
		double NowSeconds, const FPendingCandidate* PendingSelf = nullptr,
		bool bAllowFrozenUnacknowledgedAbilityConfig = false) const;
	EGuLiWingmanRejectReason ValidateCandidateSpatialEnvelope(const FGuLiWingmanCandidateBatch& Candidate,
		double NowSeconds, const FGuLiRelayCarrierState& CarrierState) const;
	EGuLiWingmanRejectReason ValidateCandidateWorldSegments(const FGuLiWingmanCandidateBatch& Candidate,
		const FGuLiRelayCarrierState& CarrierState,
		const FGuLiCandidateWorldValidator& WorldValidator,
		TArray<FGuLiWingmanCandidateWorldSegment>& OutSegments) const;
	EGuLiWingmanRejectReason BuildValidatedCandidate(const FGuLiWingmanCandidateBatch& Candidate,
		double NowSeconds, const FGuLiRelayCarrierState& CarrierState,
		const FGuLiCandidateWorldValidator& WorldValidator, FValidatedCandidate& OutValidated) const;
	void CommitValidatedCandidate(const FGuLiWingmanCandidateBatch& Candidate,
		const FValidatedCandidate& Validated, double NowSeconds);
	FGuLiWingmanSubmissionResult AcceptResolvedCandidate(const FGuLiWingmanCandidateBatch& Candidate,
		double NowSeconds, const FGuLiRelayCarrierState& CarrierState,
		const FGuLiCandidateWorldValidator& WorldValidator);
	void TryActivate(double NowSeconds);
	void SetLifecycle(EGuLiWingmanGroupLifecycle NewLifecycle, double NowSeconds);
	void ResetRequestBuckets(double NowSeconds);
	void InvalidateOutstandingBootstrap();
	void HandleBaselineRevision(double NowSeconds);
	void HandleActiveRosterRevision(const FGuLiWingmanHandle* IntroducedMember,
		double NowSeconds);
	void FreezeActiveTransactionFromOutstanding(double NowSeconds);
	void CommitLeaseOffer(double NowSeconds);
	void RecordLeaseEvent(EGuLiWingmanLeaseEventType Type, double DeadlineSeconds,
		double DetectedTimeSeconds);
	double GetConnectionSilenceAge(double NowSeconds) const;
	bool IsTransactionEntryBeforeDeadline(double NowSeconds, double DeadlineSeconds) const;
	bool IsAtomicEntryAllowed(EGuLiWingmanAtomicBatchKind Kind, double NowSeconds) const;
	void RebuildRequiredMemberMasks();
	uint64 ComputeFireHighWaterHash() const;
	bool CommitMatchesOutstanding(const FGuLiWingmanBootstrapCommit& AppliedCommit) const;
	bool BaselineMatchesOutstanding(const FGuLiWingmanTransferBaseline* AppliedBaseline) const;
	bool IsRosterMemberAlive(const FGuLiWingmanHandle& Wingman) const;
	bool IsCandidateSequenceNewer(uint32 Sequence) const;
	uint8 GetRequiredMemberMask(uint8 FlightIndex) const;
	EGuLiWingmanUploadRateClass GetAllowedUploadRateClass(uint32 ClientSimTick, double NowSeconds) const;
	bool ConsumeFlightUploadRate(const FGuLiWingmanCandidateBatch& Candidate, double NowSeconds);
	FGuLiWingmanSubmissionResult MakeCandidatePending(const FGuLiWingmanCandidateBatch& Candidate,
		double NowSeconds) const;
	FGuLiWingmanSubmissionResult MakeCandidateRejected(const FGuLiWingmanCandidateBatch& Candidate,
		EGuLiWingmanRejectReason Reason, double NowSeconds) const;
	void PopulateAcceptanceGrant(FGuLiWingmanSimulationAcceptance& Acceptance, uint32 ClientSimTick,
		double NowSeconds) const;
	uint64 ComputeOutstandingAtomicBaselineHash() const;
	void ResetFlightTransactionState();
	bool IsFireSequenceNewer(const FGuLiWingmanHandle& Wingman, uint32 Sequence) const;
	void RejectAllPending(EGuLiWingmanRejectReason Reason, double NowSeconds);

	FGuLiWingmanRelayTuning Tuning;
	bool bPhased = false;
	bool bExternalActionsLocked = false;
	bool bExternalBaselineAwaitingAck = false;
	uint32 ExternalDisplacementRevision = 0;
	TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> ExternalAcceptedSequenceFloor{};
	uint32 MatchEpoch = 0u;
	FGuLiWingmanLeaseState LeaseState;
	FGuLiGroupAbilityConfigSnapshot AbilityConfig;
	TArray<FGuLiWingmanRosterEntry> Roster;
	TArray<FGuLiWingmanAuthorityEntry> AuthorityMap;
	TArray<FGuLiWingmanHealthEntry> Health;
	TArray<FGuLiWingmanAcceptedBatch> AcceptedHistory;
	TMap<FGuLiWingmanHandle, FLastAcceptedSample> LastAcceptedSamples;
	TMap<FGuLiWingmanHandle, uint32> LastFireSequences;
	TMap<FGuLiWingmanHandle, uint32> LastEmergencyRebaseRequestSequences;
	TMap<FGuLiWingmanHandle, double> LastEmergencyRebaseAcceptedTimes;
	TArray<FPendingCandidate> PendingCandidates;
	TArray<FGuLiWingmanSubmissionResult> DeferredCandidateResults;
	TArray<FGuLiWingmanAtomicBatchAcceptance> DeferredAtomicBatchResults;
	FGuLiWingmanAtomicCandidateAssembler AtomicCandidateAssembler;
	FGuLiWingmanBootstrapBundle OutstandingBootstrap;
	FGuLiWingmanTokenBucket CandidateBucket;
	FGuLiWingmanTokenBucket FireBucket;
	uint64 NextCutId = 0u;
	uint32 RosterRevision = 0u;
	uint32 AuthorityMapRevision = 0u;
	uint32 HealthRevision = 0u;
	uint32 DeadRevision = 0u;
	uint32 AcceptedSnapshotRevision = 0u;
	uint32 ConnectionGeneration = 0u;
	FGuLiWingmanRelayValidationRevisions ValidationRevisions;
	FGuLiWingmanUploadRateGrant UploadRateGrant;
	TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> AcceptedSequenceByFlight{};
	TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> LastAcceptedFrameByFlight{};
	TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> LastAcceptedTickByFlight{};
	TStaticArray<double, GULI_WINGMAN_FLIGHT_COUNT> LastCandidateReceiveTimeByFlight{};
	TStaticArray<double, GULI_WINGMAN_FLIGHT_COUNT> AttackFlightUploadCredit{};
	TStaticArray<double, GULI_WINGMAN_FLIGHT_COUNT> LastValidCandidateTimeByFlight{};
	/**
	 * Authority-only lease grace for a Flight that transitions from no living members to
	 * required again. It never represents an Accepted Candidate and is cleared by the first
	 * valid Candidate for that Flight.
	 */
	TStaticArray<double, GULI_WINGMAN_FLIGHT_COUNT> RequiredFlightGraceStartTimeByFlight{};
	TStaticArray<uint8, GULI_WINGMAN_FLIGHT_COUNT> RequiredMemberMaskByFlight{};
	uint32 LastAcceptedCandidateSequence = 0u;
	uint32 ClientSimulationClockAnchorTick = 0u;
	uint32 LastAcceptedClientSimTick = 0u;
	double ClientSimulationClockAnchorServerTimeSeconds = 0.0;
	double LastAcceptedTimeSeconds = 0.0;
	double InitialCandidateDeadlineTimeSeconds = 0.0;
	double LastLeaseMaintenanceTimeSeconds = -DBL_MAX;
	double NextLeaseMaintenanceTimeSeconds = 0.0;
	double LastHeartbeatTimeSeconds = 0.0;
	double LastObservedAuthorityTimeSeconds = 0.0;
	uint64 LeaseMaintenanceExecutionCount = 0u;
	uint32 NextLeaseOfferRevision = 0u;
	uint64 ServerWingmanMovementWriteCount = 0u;
	uint64 EmergencyRebaseAcceptedCount = 0u;
	FGuLiWingmanPendingLeaseOffer PendingLeaseOffer;
	FGuLiWingmanActiveLeaseTransaction ActiveLeaseTransaction;
	TArray<FGuLiWingmanLeaseEvent> LeaseEvents;
	TSet<FGuLiWingmanHandle> UnacknowledgedRosterMembers;
	bool bHasClientSimulationClockAnchor = false;
	bool bAbilityConfigAcknowledged = false;
	bool bBootstrapAcknowledged = false;
	bool bTransferInProgress = false;
	bool bRequiresAtomicCandidateBatch = false;
	bool bAtomicCandidateBatchCommitted = false;
	bool bInitialActivationComplete = false;
	bool bActiveLeaseRevoked = false;
	bool bActiveRosterCutPending = false;
	EGuLiWingmanAtomicBatchKind RequiredAtomicBatchKind = EGuLiWingmanAtomicBatchKind::Bootstrap;
};
