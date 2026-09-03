// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Relay/GuLiWingmanRelayServer.h"

namespace
{
	static_assert(static_cast<uint8>(EGuLiWingmanFlightMode::Orbit) == 0u
		&& static_cast<uint8>(EGuLiWingmanFlightMode::Follow) == 1u
		&& static_cast<uint8>(EGuLiWingmanFlightMode::CatchUp) == 2u
		&& static_cast<uint8>(EGuLiWingmanFlightMode::Recover) == 3u
		&& static_cast<uint8>(EGuLiWingmanFlightMode::Stale) == 4u,
		"Protocol v7 flight-mode transition validation requires the frozen contiguous wire values.");

	bool IsStrictlyNewerSequence(const uint32 Candidate, const uint32 Previous)
	{
		return Candidate != 0u && (Previous == 0u || static_cast<int32>(Candidate - Previous) > 0);
	}

	bool IsAllowedFlightModeTransition(const uint8 PreviousValue, const uint8 CurrentValue)
	{
		constexpr uint8 MaximumLiveMode = static_cast<uint8>(EGuLiWingmanFlightMode::Recover);
		if (PreviousValue > MaximumLiveMode || CurrentValue > MaximumLiveMode)
		{
			return false;
		}
		// The client evaluates distance bands at 30 Hz. Adjacent bands and a one-band skip between
		// 5 Hz packets are legal; Orbit <-> Recover is never a physically continuous transition.
		return FMath::Abs(static_cast<int32>(CurrentValue) - static_cast<int32>(PreviousValue)) <= 2;
	}

	bool ScopeStatesEqual(const FGuLiWingmanBootstrapCommit& Lhs, const FGuLiWingmanBootstrapCommit& Rhs)
	{
		if (Lhs.ProtocolVersion != Rhs.ProtocolVersion || Lhs.CutId != Rhs.CutId || Lhs.Group != Rhs.Group
			|| Lhs.Scopes.Num() != Rhs.Scopes.Num())
		{
			return false;
		}
		for (const FGuLiWingmanBootstrapScopeState& LhsState : Lhs.Scopes)
		{
			const FGuLiWingmanBootstrapScopeState* RhsState = Rhs.FindScope(LhsState.Scope);
			if (!RhsState || RhsState->Revision != LhsState.Revision || RhsState->Hash != LhsState.Hash
				|| RhsState->ChunkCount != LhsState.ChunkCount)
			{
				return false;
			}
		}
		return true;
	}

	bool BaselinesEqual(const FGuLiWingmanTransferBaseline& Lhs, const FGuLiWingmanTransferBaseline& Rhs)
	{
		return Lhs.ProtocolVersion == Rhs.ProtocolVersion && Lhs.CutId == Rhs.CutId && Lhs.Group == Rhs.Group
			&& Lhs.LeaseEpoch == Rhs.LeaseEpoch && Lhs.AbilityConfigRevision == Rhs.AbilityConfigRevision
			&& Lhs.AbilityConfigHash == Rhs.AbilityConfigHash
			&& Lhs.AcceptedSnapshotRevision == Rhs.AcceptedSnapshotRevision
			&& Lhs.AcceptedSnapshotHash == Rhs.AcceptedSnapshotHash;
	}

	FGuLiWingmanHandle MakeWingmanHandle(const FGuLiWingmanGroupHandle& Group,
		const uint8 FlightIndex, const uint8 MemberIndex, const uint32 EntityGeneration)
	{
		FGuLiWingmanHandle Handle;
		Handle.Flight.Group = Group;
		Handle.Flight.FlightIndex = FlightIndex;
		Handle.MemberIndex = MemberIndex;
		Handle.EntityGeneration = EntityGeneration;
		return Handle;
	}

	void AddScope(FGuLiWingmanBootstrapCommit& Commit, const EGuLiWingmanBootstrapScope Scope,
		const uint32 Revision, const uint64 Hash)
	{
		FGuLiWingmanBootstrapScopeState& State = Commit.Scopes.AddDefaulted_GetRef();
		State.Scope = Scope;
		State.Revision = Revision;
		State.Hash = Hash;
		State.ChunkCount = 1u;
	}
}

bool FGuLiRelayCarrierState::IsWellFormed() const
{
	return !Transform.ContainsNaN() && !Velocity.ContainsNaN()
		&& FMath::IsFinite(ServerWorldTimeSeconds) && ServerWorldTimeSeconds >= 0.0;
}

bool FGuLiWingmanCandidateWorldValidationContext::IsWellFormed() const
{
	if (!Candidate || !Candidate->IsWellFormed() || !Carrier || !Carrier->IsWellFormed()
		|| !ConfirmedAbilityConfig || !ConfirmedAbilityConfig->IsUsableByLeaseOwner()
		|| Segments.Num() != Candidate->Samples.Num() * (Candidate->TrailSamples.Num() + 1))
	{
		return false;
	}
	for (const FGuLiWingmanCandidateWorldSegment& Segment : Segments)
	{
		if (!Segment.Wingman.IsValid() || Segment.Wingman.Flight.Group != Candidate->Group
			|| Segment.PreviousPosition.ContainsNaN() || Segment.CurrentPosition.ContainsNaN())
		{
			return false;
		}
	}
	return true;
}

bool FGuLiWingmanRelayTuning::IsWellFormed() const
{
	return FMath::IsFinite(CarrierPendingTimeoutSeconds) && CarrierPendingTimeoutSeconds > 0.0
		&& CarrierPendingTimeoutSeconds <= GULI_CARRIER_SOURCE_PENDING_TIMEOUT_SECONDS
		&& FMath::IsFinite(LeaseMaintenanceIntervalSeconds)
		&& FMath::IsNearlyEqual(LeaseMaintenanceIntervalSeconds, 1.0, UE_DOUBLE_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(InitialCandidateDeadlineSeconds, 3.0, UE_DOUBLE_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(OfferReadyDeadlineSeconds, 3.0, UE_DOUBLE_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(TakeoverAcknowledgementDeadlineSeconds, 3.0, UE_DOUBLE_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(TakeoverCandidateDeadlineSeconds, 3.0, UE_DOUBLE_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(TakeoverOverallDeadlineSeconds, 10.0, UE_DOUBLE_SMALL_NUMBER)
		&& FMath::IsFinite(ActiveToStaleSeconds) && ActiveToStaleSeconds > 0.0
		&& FMath::IsFinite(StaleToUnavailableSeconds) && StaleToUnavailableSeconds > 0.0
		&& FMath::IsFinite(UnavailableToRevokeSeconds) && UnavailableToRevokeSeconds > 0.0
		&& ActiveToStaleSeconds < StaleToUnavailableSeconds
		&& StaleToUnavailableSeconds < UnavailableToRevokeSeconds
		&& FMath::IsNearlyEqual(ActiveToStaleSeconds, 1.0, UE_DOUBLE_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(StaleToUnavailableSeconds, 2.0, UE_DOUBLE_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(UnavailableToRevokeSeconds, 3.0, UE_DOUBLE_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(HeartbeatIntervalSeconds, 1.0, UE_DOUBLE_SMALL_NUMBER)
		&& FMath::IsFinite(CandidateBucketCapacity) && CandidateBucketCapacity >= 1.0
		&& FMath::IsFinite(CandidateTokensPerSecond) && CandidateTokensPerSecond > 0.0
		&& FMath::IsFinite(FireBucketCapacity) && FireBucketCapacity >= 1.0
		&& FMath::IsFinite(FireTokensPerSecond) && FireTokensPerSecond > 0.0
		&& FMath::IsFinite(MaximumFireSourceAgeSeconds) && MaximumFireSourceAgeSeconds > 0.0
		&& FMath::IsFinite(MaximumCarrierDistanceCentimeters) && MaximumCarrierDistanceCentimeters > 0.0
		&& FMath::IsFinite(MaximumWingmanSpeedCentimetersPerSecond) && MaximumWingmanSpeedCentimetersPerSecond > 0.0
		&& FMath::IsFinite(SpeedEnvelopeSlackCentimetersPerSecond)
		&& SpeedEnvelopeSlackCentimetersPerSecond >= 0.0
		&& FMath::IsFinite(TurnEnvelopeSlackDegrees) && TurnEnvelopeSlackDegrees >= 0.0
		&& FMath::IsFinite(PositionEnvelopeSlackCentimeters) && PositionEnvelopeSlackCentimeters >= 0.0
		&& FMath::IsFinite(MaximumClientSimulationLeadSeconds)
		&& MaximumClientSimulationLeadSeconds >= 0.0
		&& FMath::IsFinite(MaximumCaptureFutureSkewSeconds) && MaximumCaptureFutureSkewSeconds >= 0.0
		&& FMath::IsFinite(MaximumCaptureHistorySeconds) && MaximumCaptureHistorySeconds > 0.0
		&& FMath::IsFinite(UploadIntervalToleranceSeconds) && UploadIntervalToleranceSeconds >= 0.0
		&& UploadIntervalToleranceSeconds < 0.1
		&& FMath::IsFinite(HighRateGrantDurationSeconds) && HighRateGrantDurationSeconds > 0.0
		&& FMath::IsFinite(ClientSimulationHz) && ClientSimulationHz > 0.0
		&& MaximumAcceptedHistoryBatches > 0;
}

bool FGuLiWingmanPendingLeaseOffer::IsWellFormed() const
{
	return IsPending() && OfferRevision != 0u && ProposedOwnerPlayerGuid.IsValid()
		&& FMath::IsFinite(IssuedTimeSeconds) && IssuedTimeSeconds >= 0.0
		&& FMath::IsFinite(ReadyDeadlineSeconds) && ReadyDeadlineSeconds > IssuedTimeSeconds
		&& FMath::IsFinite(OverallDeadlineSeconds) && OverallDeadlineSeconds >= ReadyDeadlineSeconds
		&& PreviewRosterRevision != 0u && PreviewAcceptedSnapshotHash != 0u
		&& PreviewAbilityConfigRevision != 0u && PreviewAbilityConfigHash != 0u;
}

bool FGuLiWingmanRelayServer::InitializeGroup(
	const uint32 InMatchEpoch,
	const FGuLiWingmanGroupHandle& Group,
	const FGuid& InitialOwnerPlayerGuid,
	const FGuid& BackupPlayerGuid,
	const FGuLiGroupAbilityConfigSnapshot& InitialAbilityConfig,
	const double NowSeconds,
	const FGuLiWingmanRelayTuning& InTuning)
{
	if (InMatchEpoch == 0u || !Group.IsValid() || !InitialOwnerPlayerGuid.IsValid()
		|| !InitialAbilityConfig.IsUsableByLeaseOwner() || !InTuning.IsWellFormed()
		|| !FMath::IsFinite(NowSeconds) || NowSeconds < 0.0
		|| InitialAbilityConfig.ShipInstanceId != Group.ShipInstanceId
		|| InitialAbilityConfig.ShipGeneration != Group.ShipGeneration
		|| InitialAbilityConfig.GroupGeneration != Group.GroupGeneration)
	{
		return false;
	}

	Tuning = InTuning;
	MatchEpoch = InMatchEpoch;
	LeaseState = FGuLiWingmanLeaseState{};
	LeaseState.Group = Group;
	LeaseState.OwnerPlayerGuid = InitialOwnerPlayerGuid;
	LeaseState.BackupPlayerGuid = BackupPlayerGuid;
	LeaseState.LeaseEpoch = 1u;
	AbilityConfig = InitialAbilityConfig;
	Roster.Reset(GULI_WINGMAN_GROUP_SIZE);
	AuthorityMap.Reset(GULI_WINGMAN_GROUP_SIZE);
	Health.Reset(GULI_WINGMAN_GROUP_SIZE);
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		for (uint8 MemberIndex = 0u; MemberIndex < GULI_WINGMAN_MEMBERS_PER_FLIGHT; ++MemberIndex)
		{
			const FGuLiWingmanHandle Handle = MakeWingmanHandle(Group, FlightIndex, MemberIndex, 1u);
			FGuLiWingmanRosterEntry& RosterEntry = Roster.AddDefaulted_GetRef();
			RosterEntry.Wingman = Handle;
			FGuLiWingmanAuthorityEntry& AuthorityEntry = AuthorityMap.AddDefaulted_GetRef();
			AuthorityEntry.Wingman = Handle;
			AuthorityEntry.LeaseOwnerPlayerGuid = InitialOwnerPlayerGuid;
			AuthorityEntry.LeaseEpoch = LeaseState.LeaseEpoch;
			FGuLiWingmanHealthEntry& HealthEntry = Health.AddDefaulted_GetRef();
			HealthEntry.Wingman = Handle;
		}
	}

	AcceptedHistory.Reset();
	LastAcceptedSamples.Reset();
	LastFireSequences.Reset();
	PendingCandidates.Reset();
	DeferredCandidateResults.Reset();
	DeferredAtomicBatchResults.Reset();
	OutstandingBootstrap = FGuLiWingmanBootstrapBundle{};
	NextCutId = 0u;
	RosterRevision = 1u;
	AuthorityMapRevision = 1u;
	HealthRevision = 1u;
	DeadRevision = 1u;
	AcceptedSnapshotRevision = 1u;
	ConnectionGeneration = 0u;
	ValidationRevisions = FGuLiWingmanRelayValidationRevisions{};
	UploadRateGrant = FGuLiWingmanUploadRateGrant{};
	ResetFlightTransactionState();
	RebuildRequiredMemberMasks();
	LastAcceptedCandidateSequence = 0u;
	ClientSimulationClockAnchorTick = 0u;
	LastAcceptedClientSimTick = 0u;
	ClientSimulationClockAnchorServerTimeSeconds = 0.0;
	LastAcceptedTimeSeconds = NowSeconds;
	InitialCandidateDeadlineTimeSeconds = NowSeconds + Tuning.InitialCandidateDeadlineSeconds;
	LastLeaseMaintenanceTimeSeconds = -DBL_MAX;
	NextLeaseMaintenanceTimeSeconds = NowSeconds + Tuning.LeaseMaintenanceIntervalSeconds;
	LastHeartbeatTimeSeconds = NowSeconds;
	LastObservedAuthorityTimeSeconds = NowSeconds;
	LeaseMaintenanceExecutionCount = 0u;
	ServerWingmanMovementWriteCount = 0u;
	PendingLeaseOffer = FGuLiWingmanPendingLeaseOffer{};
	ActiveLeaseTransaction = FGuLiWingmanActiveLeaseTransaction{};
	LeaseEvents.Reset();
	UnacknowledgedRosterMembers.Reset();
	bHasClientSimulationClockAnchor = false;
	bAbilityConfigAcknowledged = false;
	bBootstrapAcknowledged = false;
	bTransferInProgress = false;
	bRequiresAtomicCandidateBatch = false;
	bAtomicCandidateBatchCommitted = false;
	bInitialActivationComplete = false;
	bActiveLeaseRevoked = false;
	bActiveRosterCutPending = false;
	RequiredAtomicBatchKind = EGuLiWingmanAtomicBatchKind::Bootstrap;
	ResetRequestBuckets(NowSeconds);
	SetLifecycle(EGuLiWingmanGroupLifecycle::Initializing, NowSeconds);
	return LeaseState.IsWellFormed();
}

bool FGuLiWingmanRelayServer::ConfigureStrictFlightContract(
	const uint32 InConnectionGeneration,
	const FGuLiWingmanRelayValidationRevisions& Revisions,
	const double NowSeconds)
{
	if (InConnectionGeneration == 0u || !Revisions.IsWellFormed()
		|| !FMath::IsFinite(NowSeconds) || NowSeconds < 0.0
		|| !LeaseState.IsWellFormed() || LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Revoked
		|| (!AcceptedHistory.IsEmpty() && !bTransferInProgress))
	{
		return false;
	}
	ConnectionGeneration = InConnectionGeneration;
	ValidationRevisions = Revisions;
	bRequiresAtomicCandidateBatch = true;
	bAtomicCandidateBatchCommitted = false;
	UploadRateGrant = FGuLiWingmanUploadRateGrant{};
	UploadRateGrant.Group = LeaseState.Group;
	UploadRateGrant.ConnectionGeneration = ConnectionGeneration;
	UploadRateGrant.LeaseEpoch = LeaseState.LeaseEpoch;
	UploadRateGrant.RateClass = EGuLiWingmanUploadRateClass::Cruise5Hz;
	UploadRateGrant.GrantRevision = 1u;
	UploadRateGrant.EffectiveClientSimTick = 1u;
	UploadRateGrant.ExpiryServerTimeSeconds = 0.0;
	UploadRateGrant.Reason = EGuLiWingmanUploadRateGrantReason::CruiseDefault;
	LastObservedAuthorityTimeSeconds = FMath::Max(LastObservedAuthorityTimeSeconds, NowSeconds);
	if (bTransferInProgress)
	{
		HandleBaselineRevision(NowSeconds);
	}
	else
	{
		InvalidateOutstandingBootstrap();
	}
	return UploadRateGrant.IsWellFormed();
}

bool FGuLiWingmanRelayServer::IssueHighRateGrant(
	const uint32 EffectiveClientSimTick,
	const EGuLiWingmanUploadRateGrantReason Reason,
	const double NowSeconds,
	FGuLiWingmanUploadRateGrant& OutGrant)
{
	OutGrant = FGuLiWingmanUploadRateGrant{};
	if (!bRequiresAtomicCandidateBatch || ConnectionGeneration == 0u
		|| LeaseState.Lifecycle != EGuLiWingmanGroupLifecycle::Active
		|| EffectiveClientSimTick == 0u || !FMath::IsFinite(NowSeconds) || NowSeconds < 0.0
		|| (Reason != EGuLiWingmanUploadRateGrantReason::ServerObservedCombat
			&& Reason != EGuLiWingmanUploadRateGrantReason::ServerObservedHazard))
	{
		return false;
	}
	++UploadRateGrant.GrantRevision;
	if (UploadRateGrant.GrantRevision == 0u)
	{
		++UploadRateGrant.GrantRevision;
	}
	UploadRateGrant.Group = LeaseState.Group;
	UploadRateGrant.ConnectionGeneration = ConnectionGeneration;
	UploadRateGrant.LeaseEpoch = LeaseState.LeaseEpoch;
	UploadRateGrant.RateClass = EGuLiWingmanUploadRateClass::HighRate10Hz;
	UploadRateGrant.EffectiveClientSimTick = EffectiveClientSimTick;
	UploadRateGrant.ExpiryServerTimeSeconds = NowSeconds + Tuning.HighRateGrantDurationSeconds;
	UploadRateGrant.Reason = Reason;
	OutGrant = UploadRateGrant;
	return OutGrant.IsWellFormed();
}

bool FGuLiWingmanRelayServer::PublishAbilityConfig(
	const FGuLiGroupAbilityConfigSnapshot& NewConfig, const double NowSeconds)
{
	if (!LeaseState.IsWellFormed() || LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Revoked
		|| bTransferInProgress || !NewConfig.IsUsableByLeaseOwner() || !FMath::IsFinite(NowSeconds)
		|| NewConfig.ShipInstanceId != LeaseState.Group.ShipInstanceId
		|| NewConfig.ShipGeneration != LeaseState.Group.ShipGeneration
		|| NewConfig.GroupGeneration != LeaseState.Group.GroupGeneration
		|| !IsStrictlyNewerSequence(NewConfig.SnapshotRevision, AbilityConfig.SnapshotRevision))
	{
		return false;
	}
	AbilityConfig = NewConfig;
	bAbilityConfigAcknowledged = false;
	bBootstrapAcknowledged = false;
	InvalidateOutstandingBootstrap();
	RejectAllPending(EGuLiWingmanRejectReason::MissingAbilityConfig, NowSeconds);
	SetLifecycle(EGuLiWingmanGroupLifecycle::Initializing, NowSeconds);
	return true;
}

bool FGuLiWingmanRelayServer::BeginLeaseOffer(
	const FGuid& ProposedOwnerPlayerGuid,
	const FGuid& ProposedBackupPlayerGuid,
	const double NowSeconds,
	FGuLiWingmanPendingLeaseOffer& OutOffer)
{
	OutOffer = FGuLiWingmanPendingLeaseOffer{};
	if (!LeaseState.IsWellFormed() || LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Revoked
		|| PendingLeaseOffer.IsPending()
		|| (ActiveLeaseTransaction.State != EGuLiWingmanActiveLeaseTransactionState::None
			&& ActiveLeaseTransaction.State != EGuLiWingmanActiveLeaseTransactionState::NoOwner)
		|| !ProposedOwnerPlayerGuid.IsValid() || ProposedOwnerPlayerGuid == LeaseState.OwnerPlayerGuid
		|| !AbilityConfig.IsUsableByLeaseOwner()
		|| !FMath::IsFinite(NowSeconds) || NowSeconds < 0.0)
	{
		return false;
	}
	LastObservedAuthorityTimeSeconds = FMath::Max(LastObservedAuthorityTimeSeconds, NowSeconds);
	++NextLeaseOfferRevision;
	if (NextLeaseOfferRevision == 0u)
	{
		++NextLeaseOfferRevision;
	}
	PendingLeaseOffer.State = EGuLiWingmanPendingLeaseState::AwaitingOfferReady;
	PendingLeaseOffer.OfferRevision = NextLeaseOfferRevision;
	PendingLeaseOffer.ProposedOwnerPlayerGuid = ProposedOwnerPlayerGuid;
	PendingLeaseOffer.ProposedBackupPlayerGuid = ProposedBackupPlayerGuid;
	PendingLeaseOffer.IssuedTimeSeconds = NowSeconds;
	PendingLeaseOffer.ReadyDeadlineSeconds = NowSeconds + Tuning.OfferReadyDeadlineSeconds;
	PendingLeaseOffer.OverallDeadlineSeconds = NowSeconds + Tuning.TakeoverOverallDeadlineSeconds;
	PendingLeaseOffer.PreviewRosterRevision = RosterRevision;
	PendingLeaseOffer.PreviewAcceptedSnapshotHash = GuLiWingmanRelayHash::AcceptedSnapshot(AcceptedHistory);
	PendingLeaseOffer.PreviewAbilityConfigRevision = AbilityConfig.SnapshotRevision;
	PendingLeaseOffer.PreviewAbilityConfigHash = AbilityConfig.SnapshotHash;
	if (!PendingLeaseOffer.IsWellFormed())
	{
		PendingLeaseOffer = FGuLiWingmanPendingLeaseOffer{};
		return false;
	}
	RecordLeaseEvent(EGuLiWingmanLeaseEventType::OfferStarted,
		PendingLeaseOffer.ReadyDeadlineSeconds, NowSeconds);
	OutOffer = PendingLeaseOffer;
	return true;
}

bool FGuLiWingmanRelayServer::AcknowledgeLeaseOfferReady(
	const FGuid& SenderPlayerGuid, const uint32 OfferRevision, const double NowSeconds)
{
	if (!PendingLeaseOffer.IsWellFormed()
		|| SenderPlayerGuid != PendingLeaseOffer.ProposedOwnerPlayerGuid
		|| OfferRevision == 0u || OfferRevision != PendingLeaseOffer.OfferRevision
		|| !IsTransactionEntryBeforeDeadline(NowSeconds, PendingLeaseOffer.ReadyDeadlineSeconds)
		|| !IsTransactionEntryBeforeDeadline(NowSeconds, PendingLeaseOffer.OverallDeadlineSeconds))
	{
		return false;
	}
	LastObservedAuthorityTimeSeconds = FMath::Max(LastObservedAuthorityTimeSeconds, NowSeconds);
	CommitLeaseOffer(NowSeconds);
	return true;
}

bool FGuLiWingmanRelayServer::BeginTakeover(
	const FGuid& NewOwnerPlayerGuid, const FGuid& NewBackupPlayerGuid, const double NowSeconds)
{
	// Authority compatibility facade used by deterministic fixtures. Production disconnects use
	// BeginLeaseOffer -> client Ready -> commit and never call this shortcut.
	FGuLiWingmanPendingLeaseOffer Offer;
	return BeginLeaseOffer(NewOwnerPlayerGuid, NewBackupPlayerGuid, NowSeconds, Offer)
		&& AcknowledgeLeaseOfferReady(NewOwnerPlayerGuid, Offer.OfferRevision, NowSeconds);
}

bool FGuLiWingmanRelayServer::SuspendForOwnerLoss(
	const FGuid& NoOwnerSentinelGuid, const double NowSeconds)
{
	if (!LeaseState.IsWellFormed() || LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Revoked
		|| !NoOwnerSentinelGuid.IsValid() || NoOwnerSentinelGuid == LeaseState.OwnerPlayerGuid
		|| !AbilityConfig.IsUsableByLeaseOwner() || !FMath::IsFinite(NowSeconds) || NowSeconds < 0.0)
	{
		return false;
	}

	PendingLeaseOffer = FGuLiWingmanPendingLeaseOffer{};
	LeaseState.OwnerPlayerGuid = NoOwnerSentinelGuid;
	LeaseState.BackupPlayerGuid.Invalidate();
	++LeaseState.LeaseEpoch;
	if (LeaseState.LeaseEpoch == 0u)
	{
		++LeaseState.LeaseEpoch;
	}
	for (FGuLiWingmanAuthorityEntry& Entry : AuthorityMap)
	{
		Entry.LeaseOwnerPlayerGuid = NoOwnerSentinelGuid;
		Entry.LeaseEpoch = LeaseState.LeaseEpoch;
	}
	++AuthorityMapRevision;
	if (AuthorityMapRevision == 0u)
	{
		++AuthorityMapRevision;
	}
	bTransferInProgress = false;
	bActiveLeaseRevoked = true;
	bActiveRosterCutPending = false;
	UnacknowledgedRosterMembers.Reset();
	ActiveLeaseTransaction = FGuLiWingmanActiveLeaseTransaction{};
	ActiveLeaseTransaction.State = EGuLiWingmanActiveLeaseTransactionState::NoOwner;
	bAbilityConfigAcknowledged = false;
	bBootstrapAcknowledged = false;
	InvalidateOutstandingBootstrap();
	RejectAllPending(EGuLiWingmanRejectReason::WrongLease, NowSeconds);
	ResetRequestBuckets(NowSeconds);
	SetLifecycle(EGuLiWingmanGroupLifecycle::Unavailable, NowSeconds);
	RecordLeaseEvent(EGuLiWingmanLeaseEventType::NoOwner, NowSeconds, NowSeconds);
	return true;
}

bool FGuLiWingmanRelayServer::EnterNoOwner(const double NowSeconds)
{
	if (!LeaseState.Group.IsValid())
	{
		return false;
	}
	const uint32 A = LeaseState.Group.ShipInstanceId.A ^ 0x4E4F4F57u;
	const uint32 B = LeaseState.Group.ShipInstanceId.B ^ LeaseState.Group.ShipGeneration;
	const uint32 C = LeaseState.Group.ShipInstanceId.C ^ LeaseState.Group.GroupGeneration;
	const FGuid Sentinel(A == 0u ? 1u : A, B, C, 0x4E455221u);
	return SuspendForOwnerLoss(Sentinel, NowSeconds);
}

bool FGuLiWingmanRelayServer::BeginResume(const double NowSeconds)
{
	if (!LeaseState.IsWellFormed() || bActiveLeaseRevoked || bTransferInProgress
		|| (LeaseState.Lifecycle != EGuLiWingmanGroupLifecycle::Stale
			&& LeaseState.Lifecycle != EGuLiWingmanGroupLifecycle::Unavailable)
		|| !FMath::IsFinite(NowSeconds) || NowSeconds < 0.0
		|| GetMaximumRequiredFlightFreshnessAge(NowSeconds) >= Tuning.UnavailableToRevokeSeconds)
	{
		return false;
	}
	LastObservedAuthorityTimeSeconds = FMath::Max(LastObservedAuthorityTimeSeconds, NowSeconds);
	bTransferInProgress = true;
	bActiveRosterCutPending = false;
	UnacknowledgedRosterMembers.Reset();
	bRequiresAtomicCandidateBatch = true;
	bAtomicCandidateBatchCommitted = false;
	RequiredAtomicBatchKind = EGuLiWingmanAtomicBatchKind::Resume;
	ActiveLeaseTransaction = FGuLiWingmanActiveLeaseTransaction{};
	ActiveLeaseTransaction.State = EGuLiWingmanActiveLeaseTransactionState::AwaitingBaselineAck;
	ActiveLeaseTransaction.Kind = EGuLiWingmanAtomicBatchKind::Resume;
	AtomicCandidateAssembler.Reset();
	bAbilityConfigAcknowledged = false;
	bBootstrapAcknowledged = false;
	InvalidateOutstandingBootstrap();
	RecordLeaseEvent(EGuLiWingmanLeaseEventType::ResumeStarted,
		NowSeconds + FMath::Max(0.0,
			Tuning.UnavailableToRevokeSeconds - GetMaximumRequiredFlightFreshnessAge(NowSeconds)),
		NowSeconds);
	return true;
}

bool FGuLiWingmanRelayServer::RefreshActiveRosterCut(
	const double NowSeconds, FGuLiWingmanBootstrapBundle& OutBundle)
{
	OutBundle = FGuLiWingmanBootstrapBundle{};
	if (!FMath::IsFinite(NowSeconds) || NowSeconds < 0.0
		|| LeaseState.Lifecycle != EGuLiWingmanGroupLifecycle::Active
		|| bTransferInProgress || bActiveLeaseRevoked || !bActiveRosterCutPending)
	{
		return false;
	}
	LastObservedAuthorityTimeSeconds = FMath::Max(LastObservedAuthorityTimeSeconds, NowSeconds);
	return BuildBootstrap(OutBundle) && OutBundle.bActiveRosterRefresh;
}

bool FGuLiWingmanRelayServer::RecordLeaseHeartbeat(
	const FGuid& SenderPlayerGuid,
	const uint32 RequestConnectionGeneration,
	const uint32 RequestLeaseEpoch,
	const double NowSeconds)
{
	if (!FMath::IsFinite(NowSeconds) || NowSeconds < 0.0 || bActiveLeaseRevoked
		|| LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Revoked
		|| ActiveLeaseTransaction.State == EGuLiWingmanActiveLeaseTransactionState::NoOwner
		|| !IsCurrentOwner(SenderPlayerGuid) || RequestLeaseEpoch != LeaseState.LeaseEpoch
		|| (ConnectionGeneration != 0u && RequestConnectionGeneration != ConnectionGeneration))
	{
		return false;
	}
	LastObservedAuthorityTimeSeconds = FMath::Max(LastObservedAuthorityTimeSeconds, NowSeconds);
	LastHeartbeatTimeSeconds = NowSeconds;
	RecordLeaseEvent(EGuLiWingmanLeaseEventType::HeartbeatReceived, NowSeconds, NowSeconds);
	return true;
}

void FGuLiWingmanRelayServer::Revoke(const double NowSeconds)
{
	RejectAllPending(EGuLiWingmanRejectReason::InactiveGroup, NowSeconds);
	AcceptedHistory.Reset();
	LastAcceptedSamples.Reset();
	LastFireSequences.Reset();
	ClientSimulationClockAnchorTick = 0u;
	LastAcceptedClientSimTick = 0u;
	ClientSimulationClockAnchorServerTimeSeconds = 0.0;
	bHasClientSimulationClockAnchor = false;
	OutstandingBootstrap = FGuLiWingmanBootstrapBundle{};
	AtomicCandidateAssembler.Reset();
	bAbilityConfigAcknowledged = false;
	bBootstrapAcknowledged = false;
	bTransferInProgress = false;
	bAtomicCandidateBatchCommitted = false;
	bActiveLeaseRevoked = true;
	bActiveRosterCutPending = false;
	UnacknowledgedRosterMembers.Reset();
	PendingLeaseOffer = FGuLiWingmanPendingLeaseOffer{};
	ActiveLeaseTransaction = FGuLiWingmanActiveLeaseTransaction{};
	FGuLiGroupAbilityConfigSnapshot Tombstone;
	Tombstone.ShipInstanceId = LeaseState.Group.ShipInstanceId;
	Tombstone.ShipGeneration = LeaseState.Group.ShipGeneration;
	Tombstone.GroupGeneration = LeaseState.Group.GroupGeneration;
	Tombstone.SnapshotRevision = AbilityConfig.SnapshotRevision + 1u;
	if (Tombstone.SnapshotRevision == 0u)
	{
		Tombstone.SnapshotRevision = 1u;
	}
	Tombstone.RefreshHash();
	AbilityConfig = Tombstone;
	SetLifecycle(EGuLiWingmanGroupLifecycle::Revoked, FMath::Max(0.0, NowSeconds));
}

bool FGuLiWingmanRelayServer::BuildBootstrap(FGuLiWingmanBootstrapBundle& OutBundle)
{
	OutBundle = FGuLiWingmanBootstrapBundle{};
	const bool bInitialOrConfigBootstrap =
		LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Initializing;
	const bool bActiveRosterRefresh = LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Active
		&& bActiveRosterCutPending && !bTransferInProgress && !bActiveLeaseRevoked;
	const bool bResumeOrTakeoverBootstrap = bTransferInProgress
		&& (ActiveLeaseTransaction.State == EGuLiWingmanActiveLeaseTransactionState::AwaitingBaselineAck
			|| ActiveLeaseTransaction.State == EGuLiWingmanActiveLeaseTransactionState::AwaitingTakeoverBatch)
		&& (RequiredAtomicBatchKind == EGuLiWingmanAtomicBatchKind::Resume
			|| RequiredAtomicBatchKind == EGuLiWingmanAtomicBatchKind::Takeover);
	if ((!bInitialOrConfigBootstrap && !bResumeOrTakeoverBootstrap && !bActiveRosterRefresh)
		|| !AbilityConfig.IsUsableByLeaseOwner())
	{
		return false;
	}
	if (OutstandingBootstrap.IsWellFormed())
	{
		OutBundle = OutstandingBootstrap;
		return true;
	}

	FGuLiWingmanBootstrapBundle Bundle;
	Bundle.Roster = Roster;
	Bundle.AuthorityMap = AuthorityMap;
	Bundle.Health = Health;
	for (const FGuLiWingmanRosterEntry& Entry : Roster)
	{
		if (Entry.bDead)
		{
			Bundle.Dead.Add(Entry.Wingman);
		}
	}
	if (!AcceptedHistory.IsEmpty())
	{
		if (ConnectionGeneration == 0u)
		{
			Bundle.AcceptedSnapshot.Add(AcceptedHistory.Last());
		}
		else
		{
			for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
			{
				for (int32 HistoryIndex = AcceptedHistory.Num() - 1; HistoryIndex >= 0; --HistoryIndex)
				{
					if (AcceptedHistory[HistoryIndex].FlightIndex == FlightIndex)
					{
						Bundle.AcceptedSnapshot.Add(AcceptedHistory[HistoryIndex]);
						break;
					}
				}
			}
		}
	}
	Bundle.AbilityConfig = AbilityConfig;
	Bundle.ConnectionGeneration = ConnectionGeneration;
	Bundle.RosterRevision = RosterRevision;
	Bundle.ValidationRevisions = ValidationRevisions;
	Bundle.UploadRateGrant = UploadRateGrant;
	Bundle.bRequiresAtomicCandidateBatch = !bActiveRosterRefresh
		&& bRequiresAtomicCandidateBatch && !bAtomicCandidateBatchCommitted;
	Bundle.bActiveRosterRefresh = bActiveRosterRefresh;
	Bundle.AtomicBatchKind = bActiveRosterRefresh
		? EGuLiWingmanAtomicBatchKind::Bootstrap : RequiredAtomicBatchKind;
	Bundle.RequiredFlightMask = 0u;
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		if (GetRequiredMemberMask(FlightIndex) != 0u)
		{
			Bundle.RequiredFlightMask |= static_cast<uint8>(1u << FlightIndex);
		}
	}
	Bundle.RequiredMemberMaskHash = GuLiWingmanRelayHash::RequiredMemberMasks(Bundle.Roster);
	Bundle.Commit.Group = LeaseState.Group;
	++NextCutId;
	if (NextCutId == 0u)
	{
		++NextCutId;
	}
	Bundle.Commit.CutId = NextCutId;
	AddScope(Bundle.Commit, EGuLiWingmanBootstrapScope::Roster, RosterRevision,
		GuLiWingmanRelayHash::Roster(Bundle.Roster));
	AddScope(Bundle.Commit, EGuLiWingmanBootstrapScope::AuthorityMap, AuthorityMapRevision,
		GuLiWingmanRelayHash::AuthorityMap(Bundle.AuthorityMap));
	AddScope(Bundle.Commit, EGuLiWingmanBootstrapScope::Health, HealthRevision,
		GuLiWingmanRelayHash::Health(Bundle.Health));
	AddScope(Bundle.Commit, EGuLiWingmanBootstrapScope::Dead, DeadRevision,
		GuLiWingmanRelayHash::Dead(Bundle.Dead));
	AddScope(Bundle.Commit, EGuLiWingmanBootstrapScope::AcceptedSnapshot, AcceptedSnapshotRevision,
		GuLiWingmanRelayHash::AcceptedSnapshot(Bundle.AcceptedSnapshot));
	AddScope(Bundle.Commit, EGuLiWingmanBootstrapScope::GroupAbilityConfig,
		AbilityConfig.SnapshotRevision, AbilityConfig.SnapshotHash);
	Bundle.AtomicBaselineRevision = static_cast<uint32>(Bundle.Commit.CutId);
	// Freeze the new six-scope cut directly into the Candidate transaction baseline.
	{
		uint64 BaselineHash = GuLiShipAbilityHash::OffsetBasis;
		GuLiShipAbilityHash::AddUInt64(BaselineHash, Bundle.Commit.CutId);
		for (const FGuLiWingmanBootstrapScopeState& Scope : Bundle.Commit.Scopes)
		{
			GuLiShipAbilityHash::AddUInt32(BaselineHash, static_cast<uint8>(Scope.Scope));
			GuLiShipAbilityHash::AddUInt32(BaselineHash, Scope.Revision);
			GuLiShipAbilityHash::AddUInt64(BaselineHash, Scope.Hash);
		}
		Bundle.AtomicBaselineHash = GuLiShipAbilityHash::Finish(BaselineHash);
	}

	if (bTransferInProgress)
	{
		Bundle.bHasTransferBaseline = true;
		Bundle.TransferBaseline.CutId = Bundle.Commit.CutId;
		Bundle.TransferBaseline.Group = LeaseState.Group;
		Bundle.TransferBaseline.LeaseEpoch = LeaseState.LeaseEpoch;
		Bundle.TransferBaseline.AbilityConfigRevision = AbilityConfig.SnapshotRevision;
		Bundle.TransferBaseline.AbilityConfigHash = AbilityConfig.SnapshotHash;
		Bundle.TransferBaseline.AcceptedSnapshotRevision = AcceptedSnapshotRevision;
		Bundle.TransferBaseline.AcceptedSnapshotHash = GuLiWingmanRelayHash::AcceptedSnapshot(Bundle.AcceptedSnapshot);
	}

	if (!Bundle.IsWellFormed())
	{
		return false;
	}
	OutstandingBootstrap = Bundle;
	FreezeActiveTransactionFromOutstanding(LastObservedAuthorityTimeSeconds);
	bBootstrapAcknowledged = false;
	OutBundle = MoveTemp(Bundle);
	return true;
}

bool FGuLiWingmanRelayServer::AcknowledgeAbilityConfig(
	const FGuid& SenderPlayerGuid, const FGuLiGroupAbilityConfigAck& Ack, const double NowSeconds)
{
	const bool bBootstrapContext = LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Initializing;
	const bool bActiveRosterContext = LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Active
		&& bActiveRosterCutPending && !bTransferInProgress;
	const bool bTransactionContext = bTransferInProgress
		&& (ActiveLeaseTransaction.State == EGuLiWingmanActiveLeaseTransactionState::AwaitingBaselineAck
			|| ActiveLeaseTransaction.State == EGuLiWingmanActiveLeaseTransactionState::AwaitingTakeoverBatch);
	const bool bDeadlineOpen = ActiveLeaseTransaction.IsTransfer()
		? IsTransactionEntryBeforeDeadline(NowSeconds,
			ActiveLeaseTransaction.State == EGuLiWingmanActiveLeaseTransactionState::AwaitingBaselineAck
				? ActiveLeaseTransaction.BaselineAckDeadlineSeconds
				: ActiveLeaseTransaction.CandidateDeadlineSeconds)
			&& IsTransactionEntryBeforeDeadline(NowSeconds, ActiveLeaseTransaction.OverallDeadlineSeconds)
		: (!ActiveLeaseTransaction.IsResume()
			|| (!bActiveLeaseRevoked
				&& GetMaximumRequiredFlightFreshnessAge(NowSeconds) < Tuning.UnavailableToRevokeSeconds));
	if (!FMath::IsFinite(NowSeconds) || !IsCurrentOwner(SenderPlayerGuid)
		|| (!bBootstrapContext && !bTransactionContext && !bActiveRosterContext) || !bDeadlineOpen
		|| !Ack.IsWellFormed() || Ack.Group != LeaseState.Group || Ack.LeaseEpoch != LeaseState.LeaseEpoch
		|| Ack.SnapshotRevision != AbilityConfig.SnapshotRevision || Ack.SnapshotHash != AbilityConfig.SnapshotHash)
	{
		return false;
	}
	if (!bInitialActivationComplete && bBootstrapContext
		&& !IsTransactionEntryBeforeDeadline(NowSeconds, InitialCandidateDeadlineTimeSeconds))
	{
		return false;
	}
	LastObservedAuthorityTimeSeconds = FMath::Max(LastObservedAuthorityTimeSeconds, NowSeconds);
	bAbilityConfigAcknowledged = true;
	TryActivate(NowSeconds);
	return true;
}

bool FGuLiWingmanRelayServer::AcknowledgeBootstrap(
	const FGuid& SenderPlayerGuid,
	const FGuLiWingmanBootstrapCommit& AppliedCommit,
	const FGuLiWingmanTransferBaseline* AppliedTransferBaseline,
	const double NowSeconds)
{
	const bool bBootstrapContext = LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Initializing;
	const bool bActiveRosterContext = LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Active
		&& bActiveRosterCutPending && !bTransferInProgress;
	const bool bTransactionContext = bTransferInProgress
		&& ActiveLeaseTransaction.State == EGuLiWingmanActiveLeaseTransactionState::AwaitingBaselineAck;
	const bool bDeadlineOpen = ActiveLeaseTransaction.IsTransfer()
		? IsTransactionEntryBeforeDeadline(NowSeconds, ActiveLeaseTransaction.BaselineAckDeadlineSeconds)
			&& IsTransactionEntryBeforeDeadline(NowSeconds, ActiveLeaseTransaction.OverallDeadlineSeconds)
		: (!ActiveLeaseTransaction.IsResume()
			|| (!bActiveLeaseRevoked
				&& GetMaximumRequiredFlightFreshnessAge(NowSeconds) < Tuning.UnavailableToRevokeSeconds));
	if (!FMath::IsFinite(NowSeconds) || !IsCurrentOwner(SenderPlayerGuid)
		|| (!bBootstrapContext && !bTransactionContext && !bActiveRosterContext) || !bDeadlineOpen
		|| !AppliedCommit.IsWellFormed() || !CommitMatchesOutstanding(AppliedCommit)
		|| !BaselineMatchesOutstanding(AppliedTransferBaseline))
	{
		return false;
	}
	if (!bInitialActivationComplete && bBootstrapContext
		&& !IsTransactionEntryBeforeDeadline(NowSeconds, InitialCandidateDeadlineTimeSeconds))
	{
		return false;
	}
	LastObservedAuthorityTimeSeconds = FMath::Max(LastObservedAuthorityTimeSeconds, NowSeconds);
	bBootstrapAcknowledged = true;
	if (bActiveRosterContext)
	{
		bActiveRosterCutPending = false;
		UnacknowledgedRosterMembers.Reset();
	}
	if (bTransactionContext)
	{
		ActiveLeaseTransaction.State = EGuLiWingmanActiveLeaseTransactionState::AwaitingTakeoverBatch;
		if (ActiveLeaseTransaction.IsTransfer())
		{
			ActiveLeaseTransaction.CandidateDeadlineSeconds = FMath::Min(
				NowSeconds + Tuning.TakeoverCandidateDeadlineSeconds,
				ActiveLeaseTransaction.OverallDeadlineSeconds);
		}
		RecordLeaseEvent(EGuLiWingmanLeaseEventType::BaselineAcknowledged,
			ActiveLeaseTransaction.IsTransfer()
				? ActiveLeaseTransaction.CandidateDeadlineSeconds
				: NowSeconds + FMath::Max(0.0,
					Tuning.UnavailableToRevokeSeconds - GetMaximumRequiredFlightFreshnessAge(NowSeconds)),
			NowSeconds);
	}
	TryActivate(NowSeconds);
	return true;
}

FGuLiWingmanSubmissionResult FGuLiWingmanRelayServer::SubmitCandidate(
	const FGuid& SenderPlayerGuid,
	const FGuLiWingmanCandidateBatch& Candidate,
	const double NowSeconds,
	const FGuLiCarrierSourceResolver& CarrierResolver,
	const FGuLiCandidateWorldValidator& WorldValidator)
{
	EGuLiWingmanRejectReason RejectReason = ValidateCommonRequest(
		SenderPlayerGuid, Candidate.Group, Candidate.MatchEpoch, Candidate.LeaseEpoch);
	if (RejectReason == EGuLiWingmanRejectReason::None)
	{
		RejectReason = ValidateCandidateBeforeCarrier(Candidate, NowSeconds);
	}
	if (RejectReason != EGuLiWingmanRejectReason::None)
	{
		return MakeCandidateRejected(Candidate, RejectReason, NowSeconds);
	}
	if (!FMath::IsFinite(NowSeconds) || !CandidateBucket.Consume(NowSeconds))
	{
		return MakeCandidateRejected(Candidate, EGuLiWingmanRejectReason::RateLimited, NowSeconds);
	}
	if (Candidate.UsesStrictFlightContract() && !ConsumeFlightUploadRate(Candidate, NowSeconds))
	{
		return MakeCandidateRejected(Candidate, EGuLiWingmanRejectReason::RateLimited, NowSeconds);
	}
	if (Candidate.UsesStrictFlightContract())
	{
		const int32 ExistingIndex = PendingCandidates.IndexOfByPredicate(
			[&Candidate](const FPendingCandidate& Existing)
			{
				return Existing.Batch.UsesStrictFlightContract()
					&& Existing.Batch.FlightIndex == Candidate.FlightIndex;
			});
		if (ExistingIndex != INDEX_NONE)
		{
			const FPendingCandidate& Existing = PendingCandidates[ExistingIndex];
			if (!IsStrictlyNewerSequence(Candidate.FrameSequence, Existing.Batch.FrameSequence))
			{
				return MakeCandidateRejected(Candidate,
					EGuLiWingmanRejectReason::StaleFrameSequence, NowSeconds);
			}
			DeferredCandidateResults.Add(MakeCandidateRejected(Existing.Batch,
				EGuLiWingmanRejectReason::Duplicate, NowSeconds));
			PendingCandidates.RemoveAt(ExistingIndex, 1, EAllowShrinking::No);
		}
	}
	if (!CarrierResolver)
	{
		return MakeCandidateRejected(Candidate, EGuLiWingmanRejectReason::CarrierMoveExpired, NowSeconds);
	}

	bool bTrailCarrierPending = false;
	for (const FGuLiWingmanCandidateTrailSample& Trail : Candidate.TrailSamples)
	{
		FGuLiRelayCarrierState TrailCarrier;
		const EGuLiRelayCarrierLookupResult TrailLookup = CarrierResolver(Trail.CarrierSource, TrailCarrier);
		if (TrailLookup == EGuLiRelayCarrierLookupResult::Expired
			|| TrailLookup == EGuLiRelayCarrierLookupResult::Rejected)
		{
			return MakeCandidateRejected(Candidate, EGuLiWingmanRejectReason::CarrierMoveExpired, NowSeconds);
		}
		bTrailCarrierPending |= TrailLookup == EGuLiRelayCarrierLookupResult::Pending;
	}
	FGuLiRelayCarrierState CarrierState;
	const EGuLiRelayCarrierLookupResult EndpointLookup = CarrierResolver(Candidate.CarrierSource, CarrierState);
	if (EndpointLookup == EGuLiRelayCarrierLookupResult::Expired
		|| EndpointLookup == EGuLiRelayCarrierLookupResult::Rejected)
	{
		return MakeCandidateRejected(Candidate, EGuLiWingmanRejectReason::CarrierMoveExpired, NowSeconds);
	}
	switch (bTrailCarrierPending ? EGuLiRelayCarrierLookupResult::Pending : EndpointLookup)
	{
	case EGuLiRelayCarrierLookupResult::Found:
		return AcceptResolvedCandidate(Candidate, NowSeconds, CarrierState, WorldValidator);
	case EGuLiRelayCarrierLookupResult::Pending:
	{
		FPendingCandidate& Pending = PendingCandidates.AddDefaulted_GetRef();
		Pending.SenderPlayerGuid = SenderPlayerGuid;
		Pending.Batch = Candidate;
		Pending.WorldValidator = WorldValidator;
		Pending.ReceivedTimeSeconds = NowSeconds;
		Pending.DeadlineSeconds = NowSeconds + Tuning.CarrierPendingTimeoutSeconds;
		return MakeCandidatePending(Candidate, NowSeconds);
	}
	case EGuLiRelayCarrierLookupResult::Expired:
		return MakeCandidateRejected(Candidate, EGuLiWingmanRejectReason::CarrierMoveExpired, NowSeconds);
	default:
		return MakeCandidateRejected(Candidate, EGuLiWingmanRejectReason::CarrierMoveExpired, NowSeconds);
	}
}

FGuLiWingmanSubmissionResult FGuLiWingmanRelayServer::SubmitFireIntent(
	const FGuid& SenderPlayerGuid,
	const FGuLiWingmanFireIntent& Intent,
	const double NowSeconds,
	const FGuLiFireIntentServerValidator& AdditionalValidator)
{
	EGuLiWingmanRejectReason RejectReason = ValidateCommonRequest(
		SenderPlayerGuid, Intent.Group, Intent.MatchEpoch, Intent.LeaseEpoch);
	if (RejectReason == EGuLiWingmanRejectReason::None)
	{
		RejectReason = GuLiWingmanProtocol::ValidateFireIntentAbilityConfig(Intent, &AbilityConfig);
	}
	if (RejectReason == EGuLiWingmanRejectReason::None && !IsRosterMemberAlive(Intent.Emitter))
	{
		RejectReason = EGuLiWingmanRejectReason::EmitterDead;
	}
	if (RejectReason == EGuLiWingmanRejectReason::None
		&& UnacknowledgedRosterMembers.Contains(Intent.Emitter))
	{
		RejectReason = EGuLiWingmanRejectReason::StaleRosterRevision;
	}
	if (RejectReason == EGuLiWingmanRejectReason::None && !IsFireSequenceNewer(Intent.Emitter, Intent.DomainFireSequence))
	{
		RejectReason = EGuLiWingmanRejectReason::Duplicate;
	}
	const FGuLiWingmanAcceptedBatch* SourceBatch = RejectReason == EGuLiWingmanRejectReason::None
		? FindAcceptedBatch(Intent.SourceAcceptedState) : nullptr;
	if (RejectReason == EGuLiWingmanRejectReason::None
		&& (!SourceBatch || !SourceBatch->FindSample(Intent.Emitter)
			|| SourceBatch->AbilitySetRevision != Intent.AbilitySetRevision))
	{
		RejectReason = EGuLiWingmanRejectReason::StaleSourceState;
	}
	if (RejectReason == EGuLiWingmanRejectReason::None
		&& (NowSeconds < SourceBatch->ServerAcceptedTimeSeconds
			|| NowSeconds - SourceBatch->ServerAcceptedTimeSeconds > Tuning.MaximumFireSourceAgeSeconds))
	{
		RejectReason = EGuLiWingmanRejectReason::StaleSourceState;
	}
	if (RejectReason != EGuLiWingmanRejectReason::None)
	{
		return FGuLiWingmanSubmissionResult::Rejected(RejectReason, Intent.DomainFireSequence);
	}
	if (!FMath::IsFinite(NowSeconds) || !FireBucket.Consume(NowSeconds))
	{
		return FGuLiWingmanSubmissionResult::Rejected(EGuLiWingmanRejectReason::RateLimited,
			Intent.DomainFireSequence);
	}
	if (AdditionalValidator)
	{
		RejectReason = AdditionalValidator(Intent, *SourceBatch);
		if (RejectReason != EGuLiWingmanRejectReason::None)
		{
			return FGuLiWingmanSubmissionResult::Rejected(RejectReason, Intent.DomainFireSequence);
		}
	}
	LastFireSequences.FindOrAdd(Intent.Emitter) = Intent.DomainFireSequence;
	return FGuLiWingmanSubmissionResult::AcceptedSequence(Intent.DomainFireSequence);
}

FGuLiWingmanAtomicBatchAcceptance FGuLiWingmanRelayServer::SubmitAtomicCandidateFragment(
	const FGuid& SenderPlayerGuid,
	const FGuLiWingmanAtomicCandidateBatchFragment& Fragment,
	const double NowSeconds,
	const FGuLiCarrierSourceResolver& CarrierResolver,
	const FGuLiCandidateWorldValidator& WorldValidator)
{
	FGuLiWingmanAtomicBatchAcceptance Result;
	Result.BatchId = Fragment.Header.BatchId;
	Result.BatchKind = Fragment.Header.BatchKind;
	Result.Group = Fragment.Header.Group;
	Result.LeaseEpoch = Fragment.Header.LeaseEpoch;
	Result.FrozenRosterRevision = Fragment.Header.FrozenRosterRevision;
	Result.BaselineRevision = Fragment.Header.BaselineRevision;
	Result.BaselineHash = Fragment.Header.BaselineHash;
	Result.AvailabilityAfter = LeaseState.Lifecycle;
	auto Reject = [&Result](const EGuLiWingmanRejectReason Reason)
	{
		Result.Disposition = EGuLiWingmanSubmissionDisposition::Rejected;
		Result.RejectReason = Reason;
		Result.CommittedFlightMask = 0u;
		Result.AcceptedSnapshotSequences.Reset();
		Result.BatchValidatedPayloadHash = 0u;
		return Result;
	};

	if (!FMath::IsFinite(NowSeconds) || !bRequiresAtomicCandidateBatch || bAtomicCandidateBatchCommitted
		|| !IsAtomicEntryAllowed(Fragment.Header.BatchKind, NowSeconds)
		|| !IsCurrentOwner(SenderPlayerGuid) || !OutstandingBootstrap.IsWellFormed())
	{
		return Reject(EGuLiWingmanRejectReason::InactiveGroup);
	}
	if (!Fragment.Header.IsWellFormed() || Fragment.Header.Group != LeaseState.Group
		|| Fragment.Header.ConnectionGeneration != ConnectionGeneration
		|| Fragment.Header.LeaseEpoch != LeaseState.LeaseEpoch)
	{
		return Reject(EGuLiWingmanRejectReason::WrongLease);
	}
	if (Fragment.Header.FrozenRosterRevision != RosterRevision)
	{
		return Reject(EGuLiWingmanRejectReason::StaleRosterRevision);
	}
	const uint8 RequiredFlightMask = OutstandingBootstrap.RequiredFlightMask;
	if (Fragment.Header.BatchKind != RequiredAtomicBatchKind
		|| Fragment.Header.FrozenRequiredFlightMask != RequiredFlightMask
		|| Fragment.Header.IncludedFlightMask != RequiredFlightMask
		|| Fragment.Header.FrozenRequiredMemberMaskHash
			!= GuLiWingmanRelayHash::RequiredMemberMasks(Roster))
	{
		return Reject(EGuLiWingmanRejectReason::WrongFlightCoverage);
	}
	if (Fragment.Header.BaselineRevision != OutstandingBootstrap.AtomicBaselineRevision
		|| Fragment.Header.BaselineHash != OutstandingBootstrap.AtomicBaselineHash
		|| Fragment.Header.BaselineHash != ComputeOutstandingAtomicBaselineHash())
	{
		return Reject(EGuLiWingmanRejectReason::StaleAcceptedBaseline);
	}

	TArray<FGuLiWingmanCandidateBatch> Flights;
	EGuLiWingmanRejectReason AssemblyReject = EGuLiWingmanRejectReason::None;
	const EGuLiAtomicCandidateAssemblyDisposition Assembly = AtomicCandidateAssembler.SubmitFragment(
		Fragment, NowSeconds, Flights, AssemblyReject);
	if (Assembly == EGuLiAtomicCandidateAssemblyDisposition::Rejected)
	{
		return Reject(AssemblyReject);
	}
	if (Assembly == EGuLiAtomicCandidateAssemblyDisposition::Pending)
	{
		Result.Disposition = EGuLiWingmanSubmissionDisposition::Pending;
		Result.RejectReason = EGuLiWingmanRejectReason::None;
		return Result;
	}

	TSet<uint32> CandidateSequences;
	TArray<FValidatedCandidate> ValidatedFlights;
	ValidatedFlights.SetNum(Flights.Num());
	for (int32 Index = 0; Index < Flights.Num(); ++Index)
	{
		const FGuLiWingmanCandidateBatch& Candidate = Flights[Index];
		if (CandidateSequences.Contains(Candidate.CandidateSequence))
		{
			return Reject(EGuLiWingmanRejectReason::Duplicate);
		}
		CandidateSequences.Add(Candidate.CandidateSequence);
		// The atomic bootstrap candidates are validated against the exact ability snapshot frozen
		// into OutstandingBootstrap.  The owner ACK is intentionally independent and cannot be
		// required before this all-or-nothing candidate transaction completes.
		EGuLiWingmanRejectReason CandidateReject = ValidateCandidateBeforeCarrier(
			Candidate, NowSeconds, nullptr, true);
		if (CandidateReject != EGuLiWingmanRejectReason::None)
		{
			return Reject(CandidateReject);
		}
		if (!CarrierResolver)
		{
			return Reject(EGuLiWingmanRejectReason::CarrierMoveExpired);
		}
		for (const FGuLiWingmanCandidateTrailSample& Trail : Candidate.TrailSamples)
		{
			FGuLiRelayCarrierState TrailCarrier;
			if (CarrierResolver(Trail.CarrierSource, TrailCarrier) != EGuLiRelayCarrierLookupResult::Found)
			{
				return Reject(EGuLiWingmanRejectReason::AtomicBatchIncomplete);
			}
		}
		FGuLiRelayCarrierState CarrierState;
		if (CarrierResolver(Candidate.CarrierSource, CarrierState) != EGuLiRelayCarrierLookupResult::Found)
		{
			return Reject(EGuLiWingmanRejectReason::AtomicBatchIncomplete);
		}
		CandidateReject = BuildValidatedCandidate(
			Candidate, NowSeconds, CarrierState, WorldValidator, ValidatedFlights[Index]);
		if (CandidateReject != EGuLiWingmanRejectReason::None)
		{
			return Reject(CandidateReject);
		}
	}

	// Sole atomic commit point. No Accepted Store, sequence, relay or freshness field changed above.
	for (int32 Index = 0; Index < Flights.Num(); ++Index)
	{
		CommitValidatedCandidate(Flights[Index], ValidatedFlights[Index], NowSeconds);
	}
	bAtomicCandidateBatchCommitted = true;
	LastObservedAuthorityTimeSeconds = FMath::Max(LastObservedAuthorityTimeSeconds, NowSeconds);
	RecordLeaseEvent(EGuLiWingmanLeaseEventType::AtomicBatchAccepted,
		RequiredAtomicBatchKind == EGuLiWingmanAtomicBatchKind::Takeover
			? ActiveLeaseTransaction.CandidateDeadlineSeconds
			: NowSeconds,
		NowSeconds);
	TryActivate(NowSeconds);
	Result.Disposition = EGuLiWingmanSubmissionDisposition::Accepted;
	Result.RejectReason = EGuLiWingmanRejectReason::None;
	Result.CommittedFlightMask = RequiredFlightMask;
	Result.AcceptedSnapshotSequences.SetNumZeroed(GULI_WINGMAN_FLIGHT_COUNT);
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		Result.AcceptedSnapshotSequences[FlightIndex] = AcceptedSequenceByFlight[FlightIndex];
	}
	Result.BatchValidatedPayloadHash = GuLiWingmanRelayHash::CandidatePayloads(Flights);
	Result.AvailabilityAfter = LeaseState.Lifecycle;
	return Result;
}

void FGuLiWingmanRelayServer::AdvancePacketDeadlines(
	const double NowSeconds, const FGuLiCarrierSourceResolver& CarrierResolver)
{
	if (!FMath::IsFinite(NowSeconds) || NowSeconds < 0.0)
	{
		return;
	}
	FGuLiWingmanAtomicBatchAcceptance ExpiredAtomic;
	if (AtomicCandidateAssembler.Expire(NowSeconds, ExpiredAtomic))
	{
		ExpiredAtomic.AvailabilityAfter = LeaseState.Lifecycle;
		DeferredAtomicBatchResults.Add(ExpiredAtomic);
	}
	int32 Index = 0;
	while (Index < PendingCandidates.Num())
	{
		FPendingCandidate& Pending = PendingCandidates[Index];
		EGuLiWingmanRejectReason RejectReason = ValidateCommonRequest(Pending.SenderPlayerGuid,
			Pending.Batch.Group, Pending.Batch.MatchEpoch, Pending.Batch.LeaseEpoch);
		if (RejectReason == EGuLiWingmanRejectReason::None)
		{
			RejectReason = ValidateCandidateBeforeCarrier(Pending.Batch, NowSeconds, &Pending);
		}
		if (RejectReason != EGuLiWingmanRejectReason::None)
		{
			DeferredCandidateResults.Add(MakeCandidateRejected(Pending.Batch, RejectReason, NowSeconds));
			PendingCandidates.RemoveAt(Index, 1, EAllowShrinking::No);
			continue;
		}
		if (NowSeconds >= Pending.DeadlineSeconds)
		{
			DeferredCandidateResults.Add(MakeCandidateRejected(Pending.Batch,
				EGuLiWingmanRejectReason::CarrierMovePendingTimeout, NowSeconds));
			PendingCandidates.RemoveAt(Index, 1, EAllowShrinking::No);
			continue;
		}

		FGuLiRelayCarrierState CarrierState;
		EGuLiRelayCarrierLookupResult LookupResult = CarrierResolver
			? CarrierResolver(Pending.Batch.CarrierSource, CarrierState)
			: EGuLiRelayCarrierLookupResult::Rejected;
		if (LookupResult == EGuLiRelayCarrierLookupResult::Found)
		{
			for (const FGuLiWingmanCandidateTrailSample& Trail : Pending.Batch.TrailSamples)
			{
				FGuLiRelayCarrierState TrailCarrier;
				const EGuLiRelayCarrierLookupResult TrailLookup = CarrierResolver
					? CarrierResolver(Trail.CarrierSource, TrailCarrier)
					: EGuLiRelayCarrierLookupResult::Rejected;
				if (TrailLookup != EGuLiRelayCarrierLookupResult::Found)
				{
					LookupResult = TrailLookup;
					break;
				}
			}
		}
		if (LookupResult == EGuLiRelayCarrierLookupResult::Found)
		{
			DeferredCandidateResults.Add(AcceptResolvedCandidate(
				Pending.Batch, NowSeconds, CarrierState, Pending.WorldValidator));
			PendingCandidates.RemoveAt(Index, 1, EAllowShrinking::No);
		}
		else if (LookupResult == EGuLiRelayCarrierLookupResult::Expired
			|| LookupResult == EGuLiRelayCarrierLookupResult::Rejected)
		{
			DeferredCandidateResults.Add(MakeCandidateRejected(Pending.Batch,
				EGuLiWingmanRejectReason::CarrierMoveExpired, NowSeconds));
			PendingCandidates.RemoveAt(Index, 1, EAllowShrinking::No);
		}
		else
		{
			++Index;
		}
	}

	LastObservedAuthorityTimeSeconds = FMath::Max(LastObservedAuthorityTimeSeconds, NowSeconds);
}

void FGuLiWingmanRelayServer::AdvanceTime(
	const double NowSeconds, const FGuLiCarrierSourceResolver& CarrierResolver)
{
	AdvancePacketDeadlines(NowSeconds, CarrierResolver);
	RunLeaseMaintenance(NowSeconds);
}

bool FGuLiWingmanRelayServer::RunLeaseMaintenance(const double NowSeconds)
{
	if (!FMath::IsFinite(NowSeconds) || NowSeconds < 0.0
		|| NowSeconds + UE_DOUBLE_SMALL_NUMBER < NextLeaseMaintenanceTimeSeconds)
	{
		return false;
	}
	const double ScheduledMaintenanceTimeSeconds = NextLeaseMaintenanceTimeSeconds;
	LastObservedAuthorityTimeSeconds = FMath::Max(LastObservedAuthorityTimeSeconds, NowSeconds);
	LastLeaseMaintenanceTimeSeconds = NowSeconds;
	++LeaseMaintenanceExecutionCount;
	while (NextLeaseMaintenanceTimeSeconds <= NowSeconds + UE_DOUBLE_SMALL_NUMBER)
	{
		NextLeaseMaintenanceTimeSeconds += Tuning.LeaseMaintenanceIntervalSeconds;
	}
	RecordLeaseEvent(EGuLiWingmanLeaseEventType::Maintenance,
		ScheduledMaintenanceTimeSeconds, NowSeconds);

	if (LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Revoked)
	{
		return true;
	}

	// Pending offer preview is independent from ActiveLease freshness. Expiry never refreshes it.
	if (PendingLeaseOffer.IsPending()
		&& (NowSeconds >= PendingLeaseOffer.ReadyDeadlineSeconds
			|| NowSeconds >= PendingLeaseOffer.OverallDeadlineSeconds))
	{
		const double Deadline = FMath::Min(
			PendingLeaseOffer.ReadyDeadlineSeconds, PendingLeaseOffer.OverallDeadlineSeconds);
		RecordLeaseEvent(EGuLiWingmanLeaseEventType::OfferReadyExpired, Deadline, NowSeconds);
		PendingLeaseOffer = FGuLiWingmanPendingLeaseOffer{};
	}

	if (!bInitialActivationComplete && LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Initializing
		&& NowSeconds >= InitialCandidateDeadlineTimeSeconds)
	{
		RecordLeaseEvent(EGuLiWingmanLeaseEventType::InitialCandidateExpired,
			InitialCandidateDeadlineTimeSeconds, NowSeconds);
		Revoke(NowSeconds);
		return true;
	}
	if (!bInitialActivationComplete && LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Initializing)
	{
		return true;
	}

	if (ActiveLeaseTransaction.IsTransfer()
		&& ActiveLeaseTransaction.State != EGuLiWingmanActiveLeaseTransactionState::None
		&& ActiveLeaseTransaction.State != EGuLiWingmanActiveLeaseTransactionState::NoOwner)
	{
		double Deadline = ActiveLeaseTransaction.OverallDeadlineSeconds;
		if (ActiveLeaseTransaction.State == EGuLiWingmanActiveLeaseTransactionState::AwaitingBaselineAck)
		{
			Deadline = FMath::Min(Deadline, ActiveLeaseTransaction.BaselineAckDeadlineSeconds);
		}
		else if (ActiveLeaseTransaction.State == EGuLiWingmanActiveLeaseTransactionState::AwaitingTakeoverBatch)
		{
			Deadline = FMath::Min(Deadline, ActiveLeaseTransaction.CandidateDeadlineSeconds);
		}
		if (NowSeconds >= Deadline)
		{
			RecordLeaseEvent(EGuLiWingmanLeaseEventType::TransactionExpired, Deadline, NowSeconds);
			RejectAllPending(EGuLiWingmanRejectReason::InactiveGroup, NowSeconds);
			AtomicCandidateAssembler.Reset();
			ActiveLeaseTransaction.State = EGuLiWingmanActiveLeaseTransactionState::NoOwner;
			bTransferInProgress = false;
			bActiveLeaseRevoked = true;
			SetLifecycle(EGuLiWingmanGroupLifecycle::Unavailable, NowSeconds);
		}
		return true;
	}

	if (ActiveLeaseTransaction.State == EGuLiWingmanActiveLeaseTransactionState::NoOwner)
	{
		SetLifecycle(EGuLiWingmanGroupLifecycle::Unavailable, NowSeconds);
		return true;
	}

	const double FreshnessAge = GetMaximumRequiredFlightFreshnessAge(NowSeconds);
	if (FreshnessAge >= Tuning.UnavailableToRevokeSeconds)
	{
		if (!bActiveLeaseRevoked)
		{
			const double Deadline = NowSeconds - FreshnessAge + Tuning.UnavailableToRevokeSeconds;
			RecordLeaseEvent(EGuLiWingmanLeaseEventType::ActiveLeaseRevoked, Deadline, NowSeconds);
			RejectAllPending(EGuLiWingmanRejectReason::InactiveGroup, NowSeconds);
			AtomicCandidateAssembler.Reset();
			bActiveLeaseRevoked = true;
			bTransferInProgress = false;
			if (!PendingLeaseOffer.IsPending())
			{
				ActiveLeaseTransaction.State = EGuLiWingmanActiveLeaseTransactionState::NoOwner;
				RecordLeaseEvent(EGuLiWingmanLeaseEventType::NoOwner, Deadline, NowSeconds);
			}
			else
			{
				ActiveLeaseTransaction = FGuLiWingmanActiveLeaseTransaction{};
			}
		}
		SetLifecycle(EGuLiWingmanGroupLifecycle::Unavailable, NowSeconds);
	}
	else if (FreshnessAge >= Tuning.StaleToUnavailableSeconds)
	{
		if (LeaseState.Lifecycle != EGuLiWingmanGroupLifecycle::Unavailable)
		{
			RecordLeaseEvent(EGuLiWingmanLeaseEventType::BecameUnavailable,
				NowSeconds - FreshnessAge + Tuning.StaleToUnavailableSeconds, NowSeconds);
		}
		SetLifecycle(EGuLiWingmanGroupLifecycle::Unavailable, NowSeconds);
	}
	else if (FreshnessAge >= Tuning.ActiveToStaleSeconds)
	{
		if (LeaseState.Lifecycle != EGuLiWingmanGroupLifecycle::Stale)
		{
			RecordLeaseEvent(EGuLiWingmanLeaseEventType::BecameStale,
				NowSeconds - FreshnessAge + Tuning.ActiveToStaleSeconds, NowSeconds);
		}
		SetLifecycle(EGuLiWingmanGroupLifecycle::Stale, NowSeconds);
	}
	else if (!bActiveLeaseRevoked && !bTransferInProgress && bInitialActivationComplete)
	{
		SetLifecycle(EGuLiWingmanGroupLifecycle::Active, NowSeconds);
	}
	return true;
}

void FGuLiWingmanRelayServer::DrainDeferredCandidateResults(
	TArray<FGuLiWingmanSubmissionResult>& OutResults)
{
	OutResults.Append(MoveTemp(DeferredCandidateResults));
	DeferredCandidateResults.Reset();
}

void FGuLiWingmanRelayServer::DrainDeferredAtomicBatchResults(
	TArray<FGuLiWingmanAtomicBatchAcceptance>& OutResults)
{
	OutResults.Append(MoveTemp(DeferredAtomicBatchResults));
	DeferredAtomicBatchResults.Reset();
}

bool FGuLiWingmanRelayServer::MarkWingmanDead(const FGuLiWingmanHandle& Wingman)
{
	FGuLiWingmanRosterEntry* RosterEntry = Roster.FindByPredicate([&Wingman](const FGuLiWingmanRosterEntry& Entry)
	{
		return Entry.Wingman == Wingman;
	});
	FGuLiWingmanHealthEntry* HealthEntry = Health.FindByPredicate([&Wingman](const FGuLiWingmanHealthEntry& Entry)
	{
		return Entry.Wingman == Wingman;
	});
	if (!RosterEntry || !HealthEntry || RosterEntry->bDead)
	{
		return false;
	}
	RosterEntry->bDead = true;
	HealthEntry->CurrentHealthPermille = 0u;
	++RosterRevision;
	++HealthRevision;
	++DeadRevision;
	LastFireSequences.Remove(Wingman);
	RebuildRequiredMemberMasks();
	HandleActiveRosterRevision(nullptr, LastObservedAuthorityTimeSeconds);
	return true;
}

bool FGuLiWingmanRelayServer::SetWingmanHealthPermille(
	const FGuLiWingmanHandle& Wingman, const uint16 CurrentHealthPermille)
{
	FGuLiWingmanHealthEntry* Entry = Health.FindByPredicate([&Wingman](const FGuLiWingmanHealthEntry& Candidate)
	{
		return Candidate.Wingman == Wingman;
	});
	if (!Entry || CurrentHealthPermille > Entry->MaximumHealthPermille)
	{
		return false;
	}
	if (Entry->CurrentHealthPermille == CurrentHealthPermille)
	{
		return true;
	}
	Entry->CurrentHealthPermille = CurrentHealthPermille;
	++HealthRevision;
	RebuildRequiredMemberMasks();
	HandleBaselineRevision(LastObservedAuthorityTimeSeconds);
	return true;
}

bool FGuLiWingmanRelayServer::ReplenishWingman(
	const uint8 FlightIndex, const uint8 MemberIndex, const uint32 NewEntityGeneration)
{
	if (FlightIndex >= GULI_WINGMAN_FLIGHT_COUNT || MemberIndex >= GULI_WINGMAN_MEMBERS_PER_FLIGHT
		|| NewEntityGeneration == 0u)
	{
		return false;
	}
	const uint8 GroupIndex = static_cast<uint8>(FlightIndex * GULI_WINGMAN_MEMBERS_PER_FLIGHT + MemberIndex);
	if (!Roster.IsValidIndex(GroupIndex) || !Health.IsValidIndex(GroupIndex) || !AuthorityMap.IsValidIndex(GroupIndex)
		|| !Roster[GroupIndex].bDead || NewEntityGeneration <= Roster[GroupIndex].Wingman.EntityGeneration)
	{
		return false;
	}
	const FGuLiWingmanHandle OldHandle = Roster[GroupIndex].Wingman;
	const FGuLiWingmanHandle NewHandle = MakeWingmanHandle(
		LeaseState.Group, FlightIndex, MemberIndex, NewEntityGeneration);
	Roster[GroupIndex].Wingman = NewHandle;
	Roster[GroupIndex].bDead = false;
	AuthorityMap[GroupIndex].Wingman = NewHandle;
	Health[GroupIndex].Wingman = NewHandle;
	Health[GroupIndex].CurrentHealthPermille = Health[GroupIndex].MaximumHealthPermille;
	LastAcceptedSamples.Remove(OldHandle);
	LastFireSequences.Remove(OldHandle);
	++RosterRevision;
	++AuthorityMapRevision;
	++HealthRevision;
	++DeadRevision;
	RebuildRequiredMemberMasks();
	HandleActiveRosterRevision(&NewHandle, LastObservedAuthorityTimeSeconds);
	return true;
}

const FGuLiWingmanAcceptedBatch* FGuLiWingmanRelayServer::FindAcceptedBatch(
	const FGuLiAcceptedStateRef& StateRef) const
{
	return AcceptedHistory.FindByPredicate([&StateRef](const FGuLiWingmanAcceptedBatch& Batch)
	{
		return Batch.StateRef.MatchEpoch == StateRef.MatchEpoch
			&& Batch.StateRef.GroupGeneration == StateRef.GroupGeneration
			&& Batch.StateRef.AcceptedSequence == StateRef.AcceptedSequence
			&& Batch.StateRef.ClientSimTick == StateRef.ClientSimTick;
	});
}

bool FGuLiWingmanRelayServer::TryGetLatestAcceptedSample(
	const FGuLiWingmanHandle& Wingman,
	FGuLiWingmanCandidateSample& OutSample,
	double* OutAcceptedTimeSeconds) const
{
	OutSample = FGuLiWingmanCandidateSample{};
	if (OutAcceptedTimeSeconds)
	{
		*OutAcceptedTimeSeconds = 0.0;
	}
	const FLastAcceptedSample* Latest = LastAcceptedSamples.Find(Wingman);
	if (!Latest || Latest->Sample.Wingman != Wingman
		|| !Latest->Sample.IsWellFormed(Wingman.Flight.Group)
		|| !FMath::IsFinite(Latest->ServerAcceptedTimeSeconds))
	{
		return false;
	}
	OutSample = Latest->Sample;
	if (OutAcceptedTimeSeconds)
	{
		*OutAcceptedTimeSeconds = Latest->ServerAcceptedTimeSeconds;
	}
	return true;
}

uint32 FGuLiWingmanRelayServer::GetAcceptedSequenceForFlight(const uint8 FlightIndex) const
{
	return FlightIndex < GULI_WINGMAN_FLIGHT_COUNT ? AcceptedSequenceByFlight[FlightIndex] : 0u;
}

uint32 FGuLiWingmanRelayServer::GetLastAcceptedFrameForFlight(const uint8 FlightIndex) const
{
	return FlightIndex < GULI_WINGMAN_FLIGHT_COUNT ? LastAcceptedFrameByFlight[FlightIndex] : 0u;
}

uint8 FGuLiWingmanRelayServer::GetRequiredMemberMask(const uint8 FlightIndex) const
{
	return FlightIndex < GULI_WINGMAN_FLIGHT_COUNT
		? RequiredMemberMaskByFlight[FlightIndex] : 0u;
}

EGuLiWingmanUploadRateClass FGuLiWingmanRelayServer::GetAllowedUploadRateClass(
	const uint32 ClientSimTick, const double NowSeconds) const
{
	return UploadRateGrant.IsHighRateActive(ClientSimTick, NowSeconds)
		? EGuLiWingmanUploadRateClass::HighRate10Hz
		: EGuLiWingmanUploadRateClass::Cruise5Hz;
}

bool FGuLiWingmanRelayServer::ConsumeFlightUploadRate(
	const FGuLiWingmanCandidateBatch& Candidate, const double NowSeconds)
{
	if (!Candidate.UsesStrictFlightContract() || Candidate.FlightIndex >= GULI_WINGMAN_FLIGHT_COUNT
		|| !FMath::IsFinite(NowSeconds))
	{
		return false;
	}
	const EGuLiWingmanUploadRateClass Allowed = GetAllowedUploadRateClass(
		Candidate.ClientSimTick, NowSeconds);
	const EGuLiWingmanUploadRateClass Effective =
		Candidate.RequestedRateClass == EGuLiWingmanUploadRateClass::HighRate10Hz
		&& Allowed == EGuLiWingmanUploadRateClass::HighRate10Hz
		? EGuLiWingmanUploadRateClass::HighRate10Hz
		: EGuLiWingmanUploadRateClass::Cruise5Hz;
	const double MinimumInterval = Effective == EGuLiWingmanUploadRateClass::HighRate10Hz ? 0.1 : 0.2;
	double& LastReceive = LastCandidateReceiveTimeByFlight[Candidate.FlightIndex];
	if (LastReceive > -DBL_MAX / 2.0
		&& NowSeconds - LastReceive + Tuning.UploadIntervalToleranceSeconds < MinimumInterval)
	{
		return false;
	}
	LastReceive = NowSeconds;
	return true;
}

void FGuLiWingmanRelayServer::PopulateAcceptanceGrant(
	FGuLiWingmanSimulationAcceptance& Acceptance,
	const uint32 ClientSimTick,
	const double NowSeconds) const
{
	Acceptance.LeaseEpoch = LeaseState.LeaseEpoch;
	Acceptance.AvailabilityAfter = LeaseState.Lifecycle;
	Acceptance.AllowedUploadRateClass = GetAllowedUploadRateClass(ClientSimTick, NowSeconds);
	Acceptance.GrantRevision = UploadRateGrant.GrantRevision;
	Acceptance.GrantExpiryServerTimeSeconds = UploadRateGrant.ExpiryServerTimeSeconds;
}

FGuLiWingmanSubmissionResult FGuLiWingmanRelayServer::MakeCandidatePending(
	const FGuLiWingmanCandidateBatch& Candidate,
	const double NowSeconds) const
{
	FGuLiWingmanSubmissionResult Result = FGuLiWingmanSubmissionResult::Pending(
		Candidate.CandidateSequence);
	Result.Acceptance.Group = Candidate.Group.IsValid() ? Candidate.Group : LeaseState.Group;
	Result.Acceptance.FlightIndex = Candidate.FlightIndex;
	Result.Acceptance.RosterRevision = RosterRevision;
	Result.Acceptance.FrameSequence = Candidate.FrameSequence;
	Result.Acceptance.Disposition = EGuLiWingmanSubmissionDisposition::Pending;
	Result.Acceptance.RejectReason = EGuLiWingmanRejectReason::None;
	Result.Acceptance.AcceptedServerTimeSeconds = FMath::IsFinite(NowSeconds)
		? FMath::Max(0.0, NowSeconds) : 0.0;
	if (Candidate.FlightIndex < GULI_WINGMAN_FLIGHT_COUNT)
	{
		Result.Acceptance.AcceptedSnapshotSequence = AcceptedSequenceByFlight[Candidate.FlightIndex];
		for (int32 HistoryIndex = AcceptedHistory.Num() - 1; HistoryIndex >= 0; --HistoryIndex)
		{
			if (AcceptedHistory[HistoryIndex].FlightIndex == Candidate.FlightIndex)
			{
				Result.Acceptance.RebaseBaseline = AcceptedHistory[HistoryIndex].StateRef;
				break;
			}
		}
	}
	PopulateAcceptanceGrant(Result.Acceptance, Candidate.ClientSimTick, NowSeconds);
	return Result;
}

FGuLiWingmanSubmissionResult FGuLiWingmanRelayServer::MakeCandidateRejected(
	const FGuLiWingmanCandidateBatch& Candidate,
	const EGuLiWingmanRejectReason Reason,
	const double NowSeconds) const
{
	FGuLiWingmanSubmissionResult Result = FGuLiWingmanSubmissionResult::Rejected(
		Reason, Candidate.CandidateSequence);
	Result.Acceptance.Group = Candidate.Group.IsValid() ? Candidate.Group : LeaseState.Group;
	Result.Acceptance.FlightIndex = Candidate.FlightIndex;
	Result.Acceptance.RosterRevision = RosterRevision;
	Result.Acceptance.FrameSequence = Candidate.FrameSequence;
	Result.Acceptance.Disposition = EGuLiWingmanSubmissionDisposition::Rejected;
	Result.Acceptance.RejectReason = Reason;
	Result.Acceptance.AcceptedServerTimeSeconds = FMath::IsFinite(NowSeconds)
		? FMath::Max(0.0, NowSeconds) : 0.0;
	if (Candidate.FlightIndex < GULI_WINGMAN_FLIGHT_COUNT)
	{
		Result.Acceptance.AcceptedSnapshotSequence = AcceptedSequenceByFlight[Candidate.FlightIndex];
		for (int32 HistoryIndex = AcceptedHistory.Num() - 1; HistoryIndex >= 0; --HistoryIndex)
		{
			if (AcceptedHistory[HistoryIndex].FlightIndex == Candidate.FlightIndex)
			{
				Result.Acceptance.RebaseBaseline = AcceptedHistory[HistoryIndex].StateRef;
				break;
			}
		}
	}
	PopulateAcceptanceGrant(Result.Acceptance, Candidate.ClientSimTick, NowSeconds);
	return Result;
}

uint64 FGuLiWingmanRelayServer::ComputeOutstandingAtomicBaselineHash() const
{
	if (!OutstandingBootstrap.Commit.IsWellFormed())
	{
		return 0u;
	}
	uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
	GuLiShipAbilityHash::AddUInt64(Hash, OutstandingBootstrap.Commit.CutId);
	for (const FGuLiWingmanBootstrapScopeState& Scope : OutstandingBootstrap.Commit.Scopes)
	{
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint8>(Scope.Scope));
		GuLiShipAbilityHash::AddUInt32(Hash, Scope.Revision);
		GuLiShipAbilityHash::AddUInt64(Hash, Scope.Hash);
	}
	return GuLiShipAbilityHash::Finish(Hash);
}

void FGuLiWingmanRelayServer::ResetFlightTransactionState()
{
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		AcceptedSequenceByFlight[FlightIndex] = 0u;
		LastAcceptedFrameByFlight[FlightIndex] = 0u;
		LastAcceptedTickByFlight[FlightIndex] = 0u;
		LastCandidateReceiveTimeByFlight[FlightIndex] = -DBL_MAX;
		LastValidCandidateTimeByFlight[FlightIndex] = 0.0;
		RequiredFlightGraceStartTimeByFlight[FlightIndex] = 0.0;
	}
}

bool FGuLiWingmanRelayServer::IsCurrentOwner(const FGuid& SenderPlayerGuid) const
{
	return SenderPlayerGuid.IsValid() && SenderPlayerGuid == LeaseState.OwnerPlayerGuid;
}

EGuLiWingmanRejectReason FGuLiWingmanRelayServer::ValidateCommonRequest(
	const FGuid& SenderPlayerGuid,
	const FGuLiWingmanGroupHandle& Group,
	const uint32 RequestMatchEpoch,
	const uint32 RequestLeaseEpoch) const
{
	if (!Group.IsValid() || RequestMatchEpoch == 0u)
	{
		return EGuLiWingmanRejectReason::InvalidIdentity;
	}
	if (Group != LeaseState.Group)
	{
		return EGuLiWingmanRejectReason::WrongGeneration;
	}
	if (RequestMatchEpoch != MatchEpoch || !IsCurrentOwner(SenderPlayerGuid)
		|| RequestLeaseEpoch != LeaseState.LeaseEpoch)
	{
		return EGuLiWingmanRejectReason::WrongLease;
	}
	return LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Active
		? EGuLiWingmanRejectReason::None
		: EGuLiWingmanRejectReason::InactiveGroup;
}

EGuLiWingmanRejectReason FGuLiWingmanRelayServer::ValidateCandidateBeforeCarrier(
	const FGuLiWingmanCandidateBatch& Candidate,
	const double NowSeconds,
	const FPendingCandidate* PendingSelf,
	const bool bAllowFrozenUnacknowledgedAbilityConfig) const
{
	if (bActiveRosterCutPending && !bTransferInProgress)
	{
		return EGuLiWingmanRejectReason::StaleRosterRevision;
	}
	EGuLiWingmanRejectReason RejectReason = GuLiWingmanProtocol::ValidateCandidateAbilityConfig(
		Candidate, (bAbilityConfigAcknowledged || bAllowFrozenUnacknowledgedAbilityConfig)
			? &AbilityConfig : nullptr);
	if (RejectReason != EGuLiWingmanRejectReason::None)
	{
		return RejectReason;
	}
	if (Candidate.UsesStrictFlightContract())
	{
		if (ConnectionGeneration == 0u || Candidate.ConnectionGeneration != ConnectionGeneration)
		{
			return EGuLiWingmanRejectReason::WrongConnectionGeneration;
		}
		if (Candidate.RosterRevision != RosterRevision)
		{
			return EGuLiWingmanRejectReason::StaleRosterRevision;
		}
		if (Candidate.RequiredMemberMask != GetRequiredMemberMask(Candidate.FlightIndex))
		{
			return EGuLiWingmanRejectReason::WrongFlightCoverage;
		}
		if (Candidate.NavSchemaRevision != ValidationRevisions.NavSchemaRevision
			|| Candidate.NavDataChecksum != ValidationRevisions.NavDataChecksum
			|| Candidate.TuningRevision != ValidationRevisions.TuningRevision
			|| Candidate.ObstacleRevision != ValidationRevisions.ObstacleRevision)
		{
			return EGuLiWingmanRejectReason::NavigationRevisionMismatch;
		}
		if (!FMath::IsFinite(NowSeconds)
			|| Candidate.CaptureEstimatedServerTimeSeconds > NowSeconds + Tuning.MaximumCaptureFutureSkewSeconds
			|| Candidate.CaptureEstimatedServerTimeSeconds < NowSeconds - Tuning.MaximumCaptureHistorySeconds)
		{
			return EGuLiWingmanRejectReason::CaptureTimeInvalid;
		}
		if (!IsStrictlyNewerSequence(Candidate.FrameSequence,
			LastAcceptedFrameByFlight[Candidate.FlightIndex]))
		{
			return EGuLiWingmanRejectReason::StaleFrameSequence;
		}
		if (Candidate.BaseAcceptedSequence != AcceptedSequenceByFlight[Candidate.FlightIndex])
		{
			return EGuLiWingmanRejectReason::StaleAcceptedBaseline;
		}
		const EGuLiWingmanUploadRateClass AllowedRate = GetAllowedUploadRateClass(
			Candidate.ClientSimTick, NowSeconds);
		if (Candidate.ObservedGrantRevision != UploadRateGrant.GrantRevision
			|| (Candidate.RequestedRateClass == EGuLiWingmanUploadRateClass::HighRate10Hz
				&& AllowedRate != EGuLiWingmanUploadRateClass::HighRate10Hz))
		{
			return EGuLiWingmanRejectReason::UploadGrantMismatch;
		}
		if (!Candidate.TrailSamples.IsEmpty())
		{
			uint32 PreviousTick = LastAcceptedTickByFlight[Candidate.FlightIndex];
			for (const FGuLiWingmanCandidateTrailSample& Trail : Candidate.TrailSamples)
			{
				if (Trail.ClientSimTick <= PreviousTick
					|| Trail.CaptureEstimatedServerTimeSeconds
						> NowSeconds + Tuning.MaximumCaptureFutureSkewSeconds
					|| Trail.CaptureEstimatedServerTimeSeconds
						< NowSeconds - Tuning.MaximumCaptureHistorySeconds)
				{
					return EGuLiWingmanRejectReason::TrailInvalid;
				}
				PreviousTick = Trail.ClientSimTick;
			}
		}
	}
	if ((!Candidate.UsesStrictFlightContract() && !IsCandidateSequenceNewer(Candidate.CandidateSequence))
		|| PendingCandidates.ContainsByPredicate([&Candidate, PendingSelf](const FPendingCandidate& Pending)
		{
			return &Pending != PendingSelf && Pending.Batch.CandidateSequence == Candidate.CandidateSequence;
		}))
	{
		return EGuLiWingmanRejectReason::Duplicate;
	}
	for (const FGuLiWingmanCandidateSample& Sample : Candidate.Samples)
	{
		if (!IsRosterMemberAlive(Sample.Wingman))
		{
			return EGuLiWingmanRejectReason::EmitterDead;
		}
		if (Sample.FlightMode == static_cast<uint8>(EGuLiWingmanFlightMode::Stale))
		{
			return EGuLiWingmanRejectReason::InvalidIdentity;
		}
		if (const FLastAcceptedSample* Previous = LastAcceptedSamples.Find(Sample.Wingman);
			Previous && Candidate.TrailSamples.IsEmpty()
			&& !IsAllowedFlightModeTransition(Previous->Sample.FlightMode, Sample.FlightMode))
		{
			return EGuLiWingmanRejectReason::InvalidIdentity;
		}
	}
	return EGuLiWingmanRejectReason::None;
}

EGuLiWingmanRejectReason FGuLiWingmanRelayServer::ValidateCandidateSpatialEnvelope(
	const FGuLiWingmanCandidateBatch& Candidate,
	const double NowSeconds,
	const FGuLiRelayCarrierState& CarrierState) const
{
	if (!CarrierState.IsWellFormed() || !FMath::IsFinite(NowSeconds) || NowSeconds < 0.0)
	{
		return EGuLiWingmanRejectReason::CarrierMoveExpired;
	}

	const FGuLiWingmanFormationRuntimeConfig& Formation = AbilityConfig.FormationRuntime;
	const double AuthorizedMaximumSpeed = FMath::Min(
		Tuning.MaximumWingmanSpeedCentimetersPerSecond,
		static_cast<double>(Formation.CatchUpSpeedCentimetersPerSecond));
	const double MaximumAcceleration = static_cast<double>(
		Formation.MaximumAccelerationCentimetersPerSecondSquared);
	const double MaximumDeceleration = static_cast<double>(
		Formation.MaximumDecelerationCentimetersPerSecondSquared);
	const double MaximumTurnRateDegrees = static_cast<double>(
		Formation.MaximumTurnRateDegreesPerSecond);
	const double MaximumTurnRateRadians = FMath::DegreesToRadians(MaximumTurnRateDegrees);
	if (!Candidate.UsesStrictFlightContract() && bHasClientSimulationClockAnchor)
	{
		if (!IsStrictlyNewerSequence(Candidate.ClientSimTick, LastAcceptedClientSimTick))
		{
			return EGuLiWingmanRejectReason::StaleSourceState;
		}
		const uint32 TickDeltaFromAnchor = Candidate.ClientSimTick - ClientSimulationClockAnchorTick;
		const double ClientElapsedSeconds = static_cast<double>(TickDeltaFromAnchor) / Tuning.ClientSimulationHz;
		const double ServerElapsedSeconds = NowSeconds - ClientSimulationClockAnchorServerTimeSeconds;
		if (!FMath::IsFinite(ClientElapsedSeconds) || !FMath::IsFinite(ServerElapsedSeconds)
			|| ServerElapsedSeconds < 0.0
			|| ClientElapsedSeconds > ServerElapsedSeconds + Tuning.MaximumClientSimulationLeadSeconds)
		{
			return EGuLiWingmanRejectReason::StaleSourceState;
		}
	}
	struct FTemporaryPrevious
	{
		FGuLiWingmanCandidateSample Sample;
		uint32 ClientSimTick = 0u;
		double ServerTimeSeconds = 0.0;
	};
	TMap<FGuLiWingmanHandle, FTemporaryPrevious> PreviousByMember;
	for (const TPair<FGuLiWingmanHandle, FLastAcceptedSample>& Pair : LastAcceptedSamples)
	{
		FTemporaryPrevious& Previous = PreviousByMember.Add(Pair.Key);
		Previous.Sample = Pair.Value.Sample;
		Previous.ClientSimTick = Pair.Value.ClientSimTick;
		Previous.ServerTimeSeconds = Pair.Value.ServerAcceptedTimeSeconds;
	}
	const FVector CarrierLocation = CarrierState.Transform.GetLocation();
	auto ValidateFrame = [&](const TArray<FGuLiWingmanCandidateSample>& FrameSamples,
		const uint32 FrameTick, const double FrameServerTime,
		const bool bEndpoint) -> EGuLiWingmanRejectReason
	{
		for (const FGuLiWingmanCandidateSample& Sample : FrameSamples)
		{
			const FVector Position(Sample.PositionCentimeters);
			const FVector Velocity(Sample.VelocityCentimetersPerSecond);
			const double Speed = Velocity.Size();
			if (FVector::DistSquared(Position, CarrierLocation)
				> FMath::Square(Tuning.MaximumCarrierDistanceCentimeters))
			{
				return EGuLiWingmanRejectReason::InvalidIdentity;
			}
			if (FTemporaryPrevious* Previous = PreviousByMember.Find(Sample.Wingman))
			{
				if (!IsStrictlyNewerSequence(FrameTick, Previous->ClientSimTick))
				{
					return bEndpoint ? EGuLiWingmanRejectReason::StaleSourceState
						: EGuLiWingmanRejectReason::TrailInvalid;
				}
				const double ClientDeltaSeconds = static_cast<double>(FrameTick - Previous->ClientSimTick)
					/ Tuning.ClientSimulationHz;
				const double ServerDeltaSeconds = FrameServerTime - Previous->ServerTimeSeconds;
				if (!FMath::IsFinite(ClientDeltaSeconds) || !FMath::IsFinite(ServerDeltaSeconds)
					|| ServerDeltaSeconds < 0.0
					|| ClientDeltaSeconds > ServerDeltaSeconds + Tuning.MaximumClientSimulationLeadSeconds)
				{
					return bEndpoint ? EGuLiWingmanRejectReason::StaleSourceState
						: EGuLiWingmanRejectReason::TrailInvalid;
				}
				if (!IsAllowedFlightModeTransition(Previous->Sample.FlightMode, Sample.FlightMode))
				{
					return bEndpoint ? EGuLiWingmanRejectReason::InvalidIdentity
						: EGuLiWingmanRejectReason::TrailInvalid;
				}
				const FVector PreviousPosition(Previous->Sample.PositionCentimeters);
				const FVector PreviousVelocity(Previous->Sample.VelocityCentimetersPerSecond);
				const double PreviousSpeed = PreviousVelocity.Size();
				const double MaximumEndpointSpeed = FMath::Min(
					Tuning.MaximumWingmanSpeedCentimetersPerSecond,
					FMath::Max(AuthorizedMaximumSpeed, PreviousSpeed));
				if (Speed > MaximumEndpointSpeed + Tuning.SpeedEnvelopeSlackCentimetersPerSecond)
				{
					return bEndpoint ? EGuLiWingmanRejectReason::InvalidIdentity
						: EGuLiWingmanRejectReason::TrailInvalid;
				}
				const double SpeedDelta = Speed - PreviousSpeed;
				const double PermittedSpeedDelta = (SpeedDelta >= 0.0
					? MaximumAcceleration : MaximumDeceleration) * ClientDeltaSeconds
					+ Tuning.SpeedEnvelopeSlackCentimetersPerSecond;
				if (FMath::Abs(SpeedDelta) > PermittedSpeedDelta)
				{
					return bEndpoint ? EGuLiWingmanRejectReason::InvalidIdentity
						: EGuLiWingmanRejectReason::TrailInvalid;
				}
				if (Speed > UE_DOUBLE_SMALL_NUMBER && PreviousSpeed > UE_DOUBLE_SMALL_NUMBER)
				{
					const double DirectionDot = FMath::Clamp(FVector::DotProduct(
						Velocity / Speed, PreviousVelocity / PreviousSpeed), -1.0, 1.0);
					const double TurnDegrees = FMath::RadiansToDegrees(FMath::Acos(DirectionDot));
					if (TurnDegrees > MaximumTurnRateDegrees * ClientDeltaSeconds
						+ Tuning.TurnEnvelopeSlackDegrees)
					{
						return bEndpoint ? EGuLiWingmanRejectReason::InvalidIdentity
							: EGuLiWingmanRejectReason::TrailInvalid;
					}
				}
				const double ReachabilitySpeed = FMath::Max3(PreviousSpeed, Speed, AuthorizedMaximumSpeed);
				const double MaximumVectorAcceleration = FMath::Max(MaximumAcceleration, MaximumDeceleration)
					+ ReachabilitySpeed * MaximumTurnRateRadians;
				const FVector ConstantVelocityPrediction = PreviousPosition
					+ PreviousVelocity * ClientDeltaSeconds;
				const double PermittedIntegrationError = Tuning.PositionEnvelopeSlackCentimeters
					+ 0.5 * MaximumVectorAcceleration * FMath::Square(ClientDeltaSeconds);
				if (FVector::DistSquared(Position, ConstantVelocityPrediction)
					> FMath::Square(PermittedIntegrationError))
				{
					return bEndpoint ? EGuLiWingmanRejectReason::InvalidIdentity
						: EGuLiWingmanRejectReason::TrailInvalid;
				}
			}
			else if (Speed > AuthorizedMaximumSpeed + Tuning.SpeedEnvelopeSlackCentimetersPerSecond)
			{
				return bEndpoint ? EGuLiWingmanRejectReason::InvalidIdentity
					: EGuLiWingmanRejectReason::TrailInvalid;
			}
			FTemporaryPrevious& NewPrevious = PreviousByMember.FindOrAdd(Sample.Wingman);
			NewPrevious.Sample = Sample;
			NewPrevious.ClientSimTick = FrameTick;
			NewPrevious.ServerTimeSeconds = FrameServerTime;
		}
		return EGuLiWingmanRejectReason::None;
	};

	for (const FGuLiWingmanCandidateTrailSample& Trail : Candidate.TrailSamples)
	{
		const EGuLiWingmanRejectReason TrailResult = ValidateFrame(
			Trail.Samples, Trail.ClientSimTick, Trail.CaptureEstimatedServerTimeSeconds, false);
		if (TrailResult != EGuLiWingmanRejectReason::None)
		{
			return TrailResult;
		}
	}
	const double EndpointServerTime = Candidate.UsesStrictFlightContract()
		? Candidate.CaptureEstimatedServerTimeSeconds : NowSeconds;
	const EGuLiWingmanRejectReason EndpointResult = ValidateFrame(
		Candidate.Samples, Candidate.ClientSimTick, EndpointServerTime, true);
	if (EndpointResult != EGuLiWingmanRejectReason::None)
	{
		return EndpointResult;
	}
	return EGuLiWingmanRejectReason::None;
}

EGuLiWingmanRejectReason FGuLiWingmanRelayServer::ValidateCandidateWorldSegments(
	const FGuLiWingmanCandidateBatch& Candidate,
	const FGuLiRelayCarrierState& CarrierState,
	const FGuLiCandidateWorldValidator& WorldValidator,
	TArray<FGuLiWingmanCandidateWorldSegment>& OutSegments) const
{
	OutSegments.Reset();
	if (!WorldValidator)
	{
		return EGuLiWingmanRejectReason::InvalidIdentity;
	}
	TMap<FGuLiWingmanHandle, FVector> PreviousPositions;
	for (const TPair<FGuLiWingmanHandle, FLastAcceptedSample>& Pair : LastAcceptedSamples)
	{
		PreviousPositions.Add(Pair.Key, FVector(Pair.Value.Sample.PositionCentimeters));
	}
	auto AppendFrame = [&OutSegments, &PreviousPositions](
		const TArray<FGuLiWingmanCandidateSample>& FrameSamples)
	{
		for (const FGuLiWingmanCandidateSample& Sample : FrameSamples)
		{
			FGuLiWingmanCandidateWorldSegment& Segment = OutSegments.AddDefaulted_GetRef();
			Segment.Wingman = Sample.Wingman;
			Segment.CurrentPosition = FVector(Sample.PositionCentimeters);
			if (const FVector* Previous = PreviousPositions.Find(Sample.Wingman))
			{
				Segment.PreviousPosition = *Previous;
				Segment.bHasPreviousAcceptedSample = true;
			}
			else
			{
				Segment.PreviousPosition = Segment.CurrentPosition;
			}
			PreviousPositions.FindOrAdd(Sample.Wingman) = Segment.CurrentPosition;
		}
	};
	for (const FGuLiWingmanCandidateTrailSample& Trail : Candidate.TrailSamples)
	{
		AppendFrame(Trail.Samples);
	}
	AppendFrame(Candidate.Samples);

	FGuLiWingmanCandidateWorldValidationContext WorldContext;
	WorldContext.Candidate = &Candidate;
	WorldContext.Carrier = &CarrierState;
	WorldContext.ConfirmedAbilityConfig = &AbilityConfig;
	WorldContext.Segments = OutSegments;
	return WorldContext.IsWellFormed()
		? WorldValidator(WorldContext) : EGuLiWingmanRejectReason::InvalidIdentity;
}

EGuLiWingmanRejectReason FGuLiWingmanRelayServer::BuildValidatedCandidate(
	const FGuLiWingmanCandidateBatch& Candidate,
	const double NowSeconds,
	const FGuLiRelayCarrierState& CarrierState,
	const FGuLiCandidateWorldValidator& WorldValidator,
	FValidatedCandidate& OutValidated) const
{
	OutValidated = FValidatedCandidate{};
	EGuLiWingmanRejectReason RejectReason = ValidateCandidateSpatialEnvelope(
		Candidate, NowSeconds, CarrierState);
	if (RejectReason != EGuLiWingmanRejectReason::None)
	{
		return RejectReason;
	}
	RejectReason = ValidateCandidateWorldSegments(
		Candidate, CarrierState, WorldValidator, OutValidated.WorldSegments);
	if (RejectReason != EGuLiWingmanRejectReason::None)
	{
		return RejectReason;
	}

	FGuLiWingmanAcceptedBatch& Accepted = OutValidated.Accepted;
	Accepted.Group = Candidate.Group;
	Accepted.StateRef.MatchEpoch = Candidate.MatchEpoch;
	Accepted.StateRef.GroupGeneration = Candidate.Group.GroupGeneration;
	if (Candidate.UsesStrictFlightContract())
	{
		Accepted.StateRef.AcceptedSequence = AcceptedSequenceByFlight[Candidate.FlightIndex] + 1u;
		if (Accepted.StateRef.AcceptedSequence == 0u) Accepted.StateRef.AcceptedSequence = 1u;
	}
	else
	{
		Accepted.StateRef.AcceptedSequence = Candidate.CandidateSequence;
	}
	Accepted.StateRef.ClientSimTick = Candidate.ClientSimTick;
	Accepted.CarrierSource = Candidate.CarrierSource;
	Accepted.ConnectionGeneration = Candidate.ConnectionGeneration;
	Accepted.RosterRevision = Candidate.RosterRevision;
	Accepted.FlightIndex = Candidate.FlightIndex;
	Accepted.FrameSequence = Candidate.FrameSequence;
	Accepted.BaseAcceptedSequence = Candidate.BaseAcceptedSequence;
	Accepted.CaptureEstimatedServerTimeSeconds = Candidate.CaptureEstimatedServerTimeSeconds;
	Accepted.ValidationRevisions.NavSchemaRevision = Candidate.NavSchemaRevision;
	Accepted.ValidationRevisions.NavDataChecksum = Candidate.NavDataChecksum;
	Accepted.ValidationRevisions.TuningRevision = Candidate.TuningRevision;
	Accepted.ValidationRevisions.ObstacleRevision = Candidate.ObstacleRevision;
	Accepted.Samples = Candidate.Samples;
	Accepted.AbilitySetRevision = Candidate.AbilitySetRevision;
	Accepted.FormationCommandRevision = Candidate.FormationCommandRevision;
	Accepted.FormationDefinitionChecksum = Candidate.FormationDefinitionChecksum;
	Accepted.ServerAcceptedTimeSeconds = NowSeconds;
	Accepted.RefreshHash();
	return Accepted.IsWellFormed()
		? EGuLiWingmanRejectReason::None : EGuLiWingmanRejectReason::InvalidIdentity;
}

void FGuLiWingmanRelayServer::CommitValidatedCandidate(
	const FGuLiWingmanCandidateBatch& Candidate,
	const FValidatedCandidate& Validated,
	const double NowSeconds)
{
	for (const FGuLiWingmanCandidateSample& Sample : Candidate.Samples)
	{
		FLastAcceptedSample& Last = LastAcceptedSamples.FindOrAdd(Sample.Wingman);
		Last.Sample = Sample;
		Last.ClientSimTick = Candidate.ClientSimTick;
		// Strict clients carry an estimated server capture timestamp.  Comparing the next trail
		// against receipt time would reject valid packets whenever network latency decreases.
		Last.ServerAcceptedTimeSeconds = Candidate.UsesStrictFlightContract()
			? Candidate.CaptureEstimatedServerTimeSeconds : NowSeconds;
	}
	if (Candidate.UsesStrictFlightContract())
	{
		AcceptedSequenceByFlight[Candidate.FlightIndex] = Validated.Accepted.StateRef.AcceptedSequence;
		LastAcceptedFrameByFlight[Candidate.FlightIndex] = Candidate.FrameSequence;
		LastAcceptedTickByFlight[Candidate.FlightIndex] = Candidate.ClientSimTick;
		LastValidCandidateTimeByFlight[Candidate.FlightIndex] = NowSeconds;
		RequiredFlightGraceStartTimeByFlight[Candidate.FlightIndex] = 0.0;
	}
	else
	{
		if (!bHasClientSimulationClockAnchor)
		{
			ClientSimulationClockAnchorTick = Candidate.ClientSimTick;
			ClientSimulationClockAnchorServerTimeSeconds = NowSeconds;
			bHasClientSimulationClockAnchor = true;
		}
		LastAcceptedClientSimTick = Candidate.ClientSimTick;
	}
	if (IsStrictlyNewerSequence(Candidate.CandidateSequence, LastAcceptedCandidateSequence))
	{
		LastAcceptedCandidateSequence = Candidate.CandidateSequence;
	}
	LastAcceptedTimeSeconds = NowSeconds;
	AcceptedHistory.Add(Validated.Accepted);
	if (AcceptedHistory.Num() > Tuning.MaximumAcceptedHistoryBatches)
	{
		AcceptedHistory.RemoveAt(0, AcceptedHistory.Num() - Tuning.MaximumAcceptedHistoryBatches,
			EAllowShrinking::No);
	}
	++AcceptedSnapshotRevision;
	if (AcceptedSnapshotRevision == 0u) ++AcceptedSnapshotRevision;
}

FGuLiWingmanSubmissionResult FGuLiWingmanRelayServer::AcceptResolvedCandidate(
	const FGuLiWingmanCandidateBatch& Candidate,
	const double NowSeconds,
	const FGuLiRelayCarrierState& CarrierState,
	const FGuLiCandidateWorldValidator& WorldValidator)
{
	FValidatedCandidate Validated;
	const EGuLiWingmanRejectReason RejectReason = BuildValidatedCandidate(
		Candidate, NowSeconds, CarrierState, WorldValidator, Validated);
	if (RejectReason != EGuLiWingmanRejectReason::None)
	{
		return MakeCandidateRejected(Candidate, RejectReason, NowSeconds);
	}
	CommitValidatedCandidate(Candidate, Validated, NowSeconds);
	FGuLiWingmanSubmissionResult Result = FGuLiWingmanSubmissionResult::Accepted(Validated.Accepted);
	Result.Sequence = Candidate.CandidateSequence;
	Result.Acceptance.LeaseEpoch = Candidate.LeaseEpoch;
	Result.Acceptance.AvailabilityAfter = LeaseState.Lifecycle;
	PopulateAcceptanceGrant(Result.Acceptance, Candidate.ClientSimTick, NowSeconds);
	return Result;
}

void FGuLiWingmanRelayServer::TryActivate(const double NowSeconds)
{
	const bool bInitialOrConfigBootstrap =
		LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Initializing;
	const bool bTransferBootstrap = bTransferInProgress
		&& ActiveLeaseTransaction.State == EGuLiWingmanActiveLeaseTransactionState::AwaitingTakeoverBatch;
	if ((bInitialOrConfigBootstrap || bTransferBootstrap)
		&& bAbilityConfigAcknowledged && bBootstrapAcknowledged
		&& (!bRequiresAtomicCandidateBatch || bAtomicCandidateBatchCommitted)
		&& OutstandingBootstrap.IsWellFormed())
	{
		bInitialActivationComplete = true;
		bActiveLeaseRevoked = false;
		bTransferInProgress = false;
		LastAcceptedTimeSeconds = NowSeconds;
		if (!bRequiresAtomicCandidateBatch)
		{
			for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
			{
				if (RequiredMemberMaskByFlight[FlightIndex] != 0u)
				{
					LastValidCandidateTimeByFlight[FlightIndex] = NowSeconds;
					RequiredFlightGraceStartTimeByFlight[FlightIndex] = 0.0;
				}
			}
		}
		ActiveLeaseTransaction = FGuLiWingmanActiveLeaseTransaction{};
		SetLifecycle(EGuLiWingmanGroupLifecycle::Active, NowSeconds);
	}
}

void FGuLiWingmanRelayServer::SetLifecycle(
	const EGuLiWingmanGroupLifecycle NewLifecycle, const double NowSeconds)
{
	if (LeaseState.Lifecycle != NewLifecycle)
	{
		LeaseState.Lifecycle = NewLifecycle;
		LeaseState.LifecycleChangedTimeSeconds = NowSeconds;
	}
}

void FGuLiWingmanRelayServer::ResetRequestBuckets(const double NowSeconds)
{
	CandidateBucket.Reset(NowSeconds, Tuning.CandidateBucketCapacity, Tuning.CandidateTokensPerSecond);
	FireBucket.Reset(NowSeconds, Tuning.FireBucketCapacity, Tuning.FireTokensPerSecond);
}

void FGuLiWingmanRelayServer::InvalidateOutstandingBootstrap()
{
	OutstandingBootstrap = FGuLiWingmanBootstrapBundle{};
	bBootstrapAcknowledged = false;
}

void FGuLiWingmanRelayServer::HandleBaselineRevision(const double NowSeconds)
{
	const bool bNeedsFrozenBaseline = LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Initializing
		|| bTransferInProgress;
	if (!bNeedsFrozenBaseline)
	{
		return;
	}
	InvalidateOutstandingBootstrap();
	AtomicCandidateAssembler.Reset();
	bAtomicCandidateBatchCommitted = false;
	bAbilityConfigAcknowledged = false;
	bBootstrapAcknowledged = false;
	if (bTransferInProgress)
	{
		ActiveLeaseTransaction.State = EGuLiWingmanActiveLeaseTransactionState::AwaitingBaselineAck;
		ActiveLeaseTransaction.FrozenRosterRevision = 0u;
		ActiveLeaseTransaction.FrozenBaselineRevision = 0u;
		ActiveLeaseTransaction.FrozenBaselineHash = 0u;
		ActiveLeaseTransaction.CandidateDeadlineSeconds = 0.0;
		if (ActiveLeaseTransaction.IsTransfer())
		{
			ActiveLeaseTransaction.BaselineAckDeadlineSeconds = FMath::Min(
				NowSeconds + Tuning.TakeoverAcknowledgementDeadlineSeconds,
				ActiveLeaseTransaction.OverallDeadlineSeconds);
		}
	}
	RecordLeaseEvent(EGuLiWingmanLeaseEventType::BaselineRevised,
		ActiveLeaseTransaction.IsTransfer()
			? ActiveLeaseTransaction.BaselineAckDeadlineSeconds
			: InitialCandidateDeadlineTimeSeconds,
		NowSeconds);
}

void FGuLiWingmanRelayServer::HandleActiveRosterRevision(
	const FGuLiWingmanHandle* IntroducedMember, const double NowSeconds)
{
	if (LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Active
		&& !bTransferInProgress && !bActiveLeaseRevoked)
	{
		if (IntroducedMember && IntroducedMember->IsValid())
		{
			UnacknowledgedRosterMembers.Add(*IntroducedMember);
		}
		bActiveRosterCutPending = true;
		RejectAllPending(EGuLiWingmanRejectReason::StaleRosterRevision, NowSeconds);
		AtomicCandidateAssembler.Reset();
		InvalidateOutstandingBootstrap();
		return;
	}
	HandleBaselineRevision(NowSeconds);
}

void FGuLiWingmanRelayServer::FreezeActiveTransactionFromOutstanding(const double NowSeconds)
{
	if (!bTransferInProgress || !OutstandingBootstrap.IsWellFormed())
	{
		return;
	}
	ActiveLeaseTransaction.FrozenRosterRevision = RosterRevision;
	ActiveLeaseTransaction.FrozenBaselineRevision = OutstandingBootstrap.AtomicBaselineRevision;
	ActiveLeaseTransaction.FrozenBaselineHash = OutstandingBootstrap.AtomicBaselineHash;
	ActiveLeaseTransaction.FrozenAcceptedSnapshotHash =
		GuLiWingmanRelayHash::AcceptedSnapshot(OutstandingBootstrap.AcceptedSnapshot);
	ActiveLeaseTransaction.FrozenFireHighWaterHash = ComputeFireHighWaterHash();
	ActiveLeaseTransaction.FrozenAbilityConfigRevision = AbilityConfig.SnapshotRevision;
	ActiveLeaseTransaction.FrozenAbilityConfigHash = AbilityConfig.SnapshotHash;
	LastObservedAuthorityTimeSeconds = FMath::Max(LastObservedAuthorityTimeSeconds, NowSeconds);
}

void FGuLiWingmanRelayServer::CommitLeaseOffer(const double NowSeconds)
{
	const FGuLiWingmanPendingLeaseOffer CommittedOffer = PendingLeaseOffer;
	PendingLeaseOffer = FGuLiWingmanPendingLeaseOffer{};
	LeaseState.OwnerPlayerGuid = CommittedOffer.ProposedOwnerPlayerGuid;
	LeaseState.BackupPlayerGuid = CommittedOffer.ProposedBackupPlayerGuid;
	++LeaseState.LeaseEpoch;
	if (LeaseState.LeaseEpoch == 0u)
	{
		++LeaseState.LeaseEpoch;
	}
	for (FGuLiWingmanAuthorityEntry& Entry : AuthorityMap)
	{
		Entry.LeaseOwnerPlayerGuid = LeaseState.OwnerPlayerGuid;
		Entry.LeaseEpoch = LeaseState.LeaseEpoch;
	}
	++AuthorityMapRevision;
	if (AuthorityMapRevision == 0u)
	{
		++AuthorityMapRevision;
	}
	bTransferInProgress = true;
	bActiveLeaseRevoked = false;
	bActiveRosterCutPending = false;
	UnacknowledgedRosterMembers.Reset();
	bRequiresAtomicCandidateBatch = true;
	bAtomicCandidateBatchCommitted = false;
	RequiredAtomicBatchKind = EGuLiWingmanAtomicBatchKind::Takeover;
	ActiveLeaseTransaction = FGuLiWingmanActiveLeaseTransaction{};
	ActiveLeaseTransaction.State = EGuLiWingmanActiveLeaseTransactionState::AwaitingBaselineAck;
	ActiveLeaseTransaction.Kind = EGuLiWingmanAtomicBatchKind::Takeover;
	ActiveLeaseTransaction.OverallDeadlineSeconds = CommittedOffer.OverallDeadlineSeconds;
	ActiveLeaseTransaction.BaselineAckDeadlineSeconds = FMath::Min(
		NowSeconds + Tuning.TakeoverAcknowledgementDeadlineSeconds,
		ActiveLeaseTransaction.OverallDeadlineSeconds);
	AtomicCandidateAssembler.Reset();
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		LastAcceptedFrameByFlight[FlightIndex] = 0u;
		LastAcceptedTickByFlight[FlightIndex] = 0u;
		LastCandidateReceiveTimeByFlight[FlightIndex] = -DBL_MAX;
	}
	if (ConnectionGeneration != 0u)
	{
		++UploadRateGrant.GrantRevision;
		if (UploadRateGrant.GrantRevision == 0u)
		{
			++UploadRateGrant.GrantRevision;
		}
		UploadRateGrant.LeaseEpoch = LeaseState.LeaseEpoch;
		UploadRateGrant.RateClass = EGuLiWingmanUploadRateClass::Cruise5Hz;
		UploadRateGrant.EffectiveClientSimTick = 1u;
		UploadRateGrant.ExpiryServerTimeSeconds = 0.0;
		UploadRateGrant.Reason = EGuLiWingmanUploadRateGrantReason::LeaseChanged;
	}
	bAbilityConfigAcknowledged = false;
	bBootstrapAcknowledged = false;
	InvalidateOutstandingBootstrap();
	RejectAllPending(EGuLiWingmanRejectReason::WrongLease, NowSeconds);
	ResetRequestBuckets(NowSeconds);
	SetLifecycle(EGuLiWingmanGroupLifecycle::Unavailable, NowSeconds);
	FGuLiWingmanBootstrapBundle FrozenAtCommit;
	BuildBootstrap(FrozenAtCommit);
	RecordLeaseEvent(EGuLiWingmanLeaseEventType::OfferCommitted,
		ActiveLeaseTransaction.BaselineAckDeadlineSeconds, NowSeconds);
}

void FGuLiWingmanRelayServer::RecordLeaseEvent(
	const EGuLiWingmanLeaseEventType Type,
	const double DeadlineSeconds,
	const double DetectedTimeSeconds)
{
	// This audit trail is intentionally bounded. Heartbeats and the fixed-rate
	// maintenance watchdog must not turn a long-running match into an
	// ever-growing server allocation.
	constexpr int32 MaxLeaseEventHistory = 256;
	if (LeaseEvents.Num() >= MaxLeaseEventHistory)
	{
		LeaseEvents.RemoveAt(0, LeaseEvents.Num() - MaxLeaseEventHistory + 1,
			EAllowShrinking::No);
	}
	FGuLiWingmanLeaseEvent& Event = LeaseEvents.AddDefaulted_GetRef();
	Event.Type = Type;
	Event.Group = LeaseState.Group;
	Event.LeaseEpoch = LeaseState.LeaseEpoch;
	Event.TheoreticalDeadlineSeconds = FMath::IsFinite(DeadlineSeconds)
		? FMath::Max(0.0, DeadlineSeconds) : 0.0;
	Event.DetectedTimeSeconds = FMath::IsFinite(DetectedTimeSeconds)
		? FMath::Max(0.0, DetectedTimeSeconds) : 0.0;
	Event.DetectionLagSeconds = FMath::Max(
		0.0, Event.DetectedTimeSeconds - Event.TheoreticalDeadlineSeconds);
}

double FGuLiWingmanRelayServer::GetMaximumRequiredFlightFreshnessAge(
	const double NowSeconds) const
{
	if (!bRequiresAtomicCandidateBatch || ConnectionGeneration == 0u)
	{
		return FMath::Max(0.0, NowSeconds - LastAcceptedTimeSeconds);
	}
	double MaximumAge = 0.0;
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		if (RequiredMemberMaskByFlight[FlightIndex] != 0u)
		{
			const double FreshnessReferenceSeconds = FMath::Max(
				LastValidCandidateTimeByFlight[FlightIndex],
				RequiredFlightGraceStartTimeByFlight[FlightIndex]);
			MaximumAge = FMath::Max(
				MaximumAge, NowSeconds - FreshnessReferenceSeconds);
		}
	}
	return FMath::Max(0.0, MaximumAge);
}

bool FGuLiWingmanRelayServer::IsTransactionEntryBeforeDeadline(
	const double NowSeconds, const double DeadlineSeconds) const
{
	return FMath::IsFinite(NowSeconds) && NowSeconds >= 0.0
		&& FMath::IsFinite(DeadlineSeconds) && DeadlineSeconds > 0.0
		&& NowSeconds < DeadlineSeconds;
}

bool FGuLiWingmanRelayServer::IsAtomicEntryAllowed(
	const EGuLiWingmanAtomicBatchKind Kind, const double NowSeconds) const
{
	if (Kind != RequiredAtomicBatchKind)
	{
		return false;
	}
	if (Kind == EGuLiWingmanAtomicBatchKind::Bootstrap)
	{
		return LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Initializing
			&& (bInitialActivationComplete
				|| IsTransactionEntryBeforeDeadline(NowSeconds, InitialCandidateDeadlineTimeSeconds));
	}
	if (!bTransferInProgress
		|| ActiveLeaseTransaction.State != EGuLiWingmanActiveLeaseTransactionState::AwaitingTakeoverBatch)
	{
		return false;
	}
	if (Kind == EGuLiWingmanAtomicBatchKind::Resume)
	{
		return !bActiveLeaseRevoked
			&& (LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Stale
				|| LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Unavailable)
			&& GetMaximumRequiredFlightFreshnessAge(NowSeconds) < Tuning.UnavailableToRevokeSeconds;
	}
	return LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Unavailable
		&& IsTransactionEntryBeforeDeadline(NowSeconds, ActiveLeaseTransaction.CandidateDeadlineSeconds)
		&& IsTransactionEntryBeforeDeadline(NowSeconds, ActiveLeaseTransaction.OverallDeadlineSeconds);
}

void FGuLiWingmanRelayServer::RebuildRequiredMemberMasks()
{
	const TStaticArray<uint8, GULI_WINGMAN_FLIGHT_COUNT> PreviousMasks =
		RequiredMemberMaskByFlight;
	for (uint8& Mask : RequiredMemberMaskByFlight)
	{
		Mask = 0u;
	}
	for (int32 Index = 0; Index < Roster.Num(); ++Index)
	{
		const FGuLiWingmanRosterEntry& RosterEntry = Roster[Index];
		const FGuLiWingmanHealthEntry* HealthEntry = Health.IsValidIndex(Index) ? &Health[Index] : nullptr;
		if (!RosterEntry.bDead && HealthEntry && HealthEntry->Wingman == RosterEntry.Wingman
			&& HealthEntry->CurrentHealthPermille > 0u
			&& RosterEntry.Wingman.Flight.FlightIndex < GULI_WINGMAN_FLIGHT_COUNT)
		{
			RequiredMemberMaskByFlight[RosterEntry.Wingman.Flight.FlightIndex]
				|= static_cast<uint8>(1u << RosterEntry.Wingman.MemberIndex);
		}
	}
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		if (RequiredMemberMaskByFlight[FlightIndex] == 0u)
		{
			RequiredFlightGraceStartTimeByFlight[FlightIndex] = 0.0;
		}
		else if (PreviousMasks[FlightIndex] == 0u
			&& LeaseState.Lifecycle == EGuLiWingmanGroupLifecycle::Active
			&& !bTransferInProgress && !bActiveLeaseRevoked)
		{
			// The server-authorized replacement has no Accepted pose yet. Start the normal
			// three-second lease window now so the reliable roster Cut can cross the network;
			// combat and movement still require a real Accepted Candidate.
			RequiredFlightGraceStartTimeByFlight[FlightIndex] =
				FMath::Max(0.0, LastObservedAuthorityTimeSeconds);
		}
	}
}

uint64 FGuLiWingmanRelayServer::ComputeFireHighWaterHash() const
{
	uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
	for (const FGuLiWingmanRosterEntry& Entry : Roster)
	{
		GuLiShipAbilityHash::AddUInt32(Hash, Entry.Wingman.GetGroupMemberIndex());
		GuLiShipAbilityHash::AddUInt32(Hash, LastFireSequences.FindRef(Entry.Wingman));
	}
	return GuLiShipAbilityHash::Finish(Hash);
}

bool FGuLiWingmanRelayServer::CommitMatchesOutstanding(
	const FGuLiWingmanBootstrapCommit& AppliedCommit) const
{
	return OutstandingBootstrap.IsWellFormed()
		&& ScopeStatesEqual(OutstandingBootstrap.Commit, AppliedCommit);
}

bool FGuLiWingmanRelayServer::BaselineMatchesOutstanding(
	const FGuLiWingmanTransferBaseline* AppliedBaseline) const
{
	if (!OutstandingBootstrap.bHasTransferBaseline)
	{
		return AppliedBaseline == nullptr || !AppliedBaseline->IsWellFormed();
	}
	return AppliedBaseline && AppliedBaseline->IsWellFormed()
		&& BaselinesEqual(OutstandingBootstrap.TransferBaseline, *AppliedBaseline);
}

bool FGuLiWingmanRelayServer::IsRosterMemberAlive(const FGuLiWingmanHandle& Wingman) const
{
	const FGuLiWingmanRosterEntry* Entry = Roster.FindByPredicate([&Wingman](const FGuLiWingmanRosterEntry& Candidate)
	{
		return Candidate.Wingman == Wingman;
	});
	const FGuLiWingmanHealthEntry* HealthEntry = Health.FindByPredicate(
		[&Wingman](const FGuLiWingmanHealthEntry& Candidate)
		{
			return Candidate.Wingman == Wingman;
		});
	return Entry && !Entry->bDead && HealthEntry && HealthEntry->CurrentHealthPermille > 0u;
}

bool FGuLiWingmanRelayServer::IsCandidateSequenceNewer(const uint32 Sequence) const
{
	return IsStrictlyNewerSequence(Sequence, LastAcceptedCandidateSequence);
}

bool FGuLiWingmanRelayServer::IsFireSequenceNewer(
	const FGuLiWingmanHandle& Wingman, const uint32 Sequence) const
{
	const uint32* LastSequence = LastFireSequences.Find(Wingman);
	return IsStrictlyNewerSequence(Sequence, LastSequence ? *LastSequence : 0u);
}

void FGuLiWingmanRelayServer::RejectAllPending(
	const EGuLiWingmanRejectReason Reason,
	const double NowSeconds)
{
	for (const FPendingCandidate& Pending : PendingCandidates)
	{
		DeferredCandidateResults.Add(MakeCandidateRejected(Pending.Batch, Reason, NowSeconds));
	}
	PendingCandidates.Reset();
}
