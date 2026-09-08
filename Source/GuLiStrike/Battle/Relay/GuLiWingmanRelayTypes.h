// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "GuLiWingmanRelayTypes.generated.h"

UENUM(BlueprintType)
enum class EGuLiWingmanGroupLifecycle : uint8
{
	Unavailable = 0,
	Initializing,
	Active,
	Stale,
	Revoked
};

UENUM(BlueprintType)
enum class EGuLiWingmanSubmissionDisposition : uint8
{
	Accepted = 0,
	Pending,
	Rejected
};

/** Reliable server projection used by every Flight Candidate revision gate. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanRelayValidationRevisions
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Relay")
	uint32 NavSchemaRevision = 1u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Relay")
	uint64 NavDataChecksum = 1u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Relay")
	uint32 TuningRevision = 1u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Relay")
	uint32 ObstacleRevision = 1u;

	bool IsWellFormed() const
	{
		return NavSchemaRevision != 0u && NavDataChecksum != 0u
			&& TuningRevision != 0u && ObstacleRevision != 0u;
	}
};

/** Result of resolving a cross-channel CarrierSourceRef against authoritative CMC history. */
enum class EGuLiRelayCarrierLookupResult : uint8
{
	Found = 0,
	Pending,
	Expired,
	Rejected
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanRosterEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Roster")
	FGuLiWingmanHandle Wingman;

	/** Explicit type mapping; stable-slot/member indices are not type identities. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Roster")
	FName WingmanTypeId = TEXT("DefaultWingman");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Roster")
	bool bDead = false;

	bool IsWellFormed(const FGuLiWingmanGroupHandle& ExpectedGroup) const;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanAuthorityEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Authority")
	FGuLiWingmanHandle Wingman;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Authority")
	FGuid LeaseOwnerPlayerGuid;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Authority")
	uint32 LeaseEpoch = 0u;

	bool IsWellFormed(const FGuLiWingmanGroupHandle& ExpectedGroup) const;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanHealthEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Health")
	FGuLiWingmanHandle Wingman;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Health")
	uint16 CurrentHealthPermille = 1000u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Health")
	uint16 MaximumHealthPermille = 1000u;

	bool IsWellFormed(const FGuLiWingmanGroupHandle& ExpectedGroup) const;
};

/** An accepted batch is a verbatim, server-validated client sample set. It is never extrapolated on authority. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanAcceptedBatch
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Accepted")
	FGuLiWingmanGroupHandle Group;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Accepted")
	FGuLiAcceptedStateRef StateRef;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Accepted")
	FGuLiCarrierSourceRef CarrierSource;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Accepted")
	uint32 ConnectionGeneration = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Accepted")
	uint32 RosterRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Accepted")
	uint8 FlightIndex = MAX_uint8;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Accepted")
	uint32 FrameSequence = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Accepted")
	uint32 BaseAcceptedSequence = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Accepted")
	double CaptureEstimatedServerTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Accepted")
	FGuLiWingmanRelayValidationRevisions ValidationRevisions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Accepted")
	TArray<FGuLiWingmanCandidateSample> Samples;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Accepted")
	uint32 AbilitySetRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Accepted")
	uint32 FormationCommandRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Accepted")
	uint64 FormationDefinitionChecksum = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Accepted")
	double ServerAcceptedTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Accepted")
	uint64 StableHash = 0u;

	uint64 ComputeStableHash() const;
	void RefreshHash();
	bool IsWellFormed() const;
	const FGuLiWingmanCandidateSample* FindSample(const FGuLiWingmanHandle& Wingman) const;
	bool UsesStrictFlightContract() const { return FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; }
};

/** Typed Candidate result. Rejections preserve the exact last Accepted baseline and current Grant. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanSimulationAcceptance
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Acceptance")
	uint32 ProtocolVersion = GULI_WINGMAN_PROTOCOL_VERSION;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Acceptance")
	FGuLiWingmanGroupHandle Group;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Acceptance")
	uint8 FlightIndex = MAX_uint8;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Acceptance")
	uint32 LeaseEpoch = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Acceptance")
	uint32 RosterRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Acceptance")
	uint32 FrameSequence = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Acceptance")
	EGuLiWingmanSubmissionDisposition Disposition = EGuLiWingmanSubmissionDisposition::Rejected;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Acceptance")
	EGuLiWingmanRejectReason RejectReason = EGuLiWingmanRejectReason::InvalidIdentity;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Acceptance")
	uint32 AcceptedSnapshotSequence = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Acceptance")
	double AcceptedServerTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Acceptance")
	uint64 ValidatedPayloadHash = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Acceptance")
	uint32 NormalizationFlags = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Acceptance")
	EGuLiWingmanGroupLifecycle AvailabilityAfter = EGuLiWingmanGroupLifecycle::Unavailable;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Acceptance")
	EGuLiWingmanUploadRateClass AllowedUploadRateClass = EGuLiWingmanUploadRateClass::Cruise5Hz;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Acceptance")
	uint32 GrantRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Acceptance")
	double GrantExpiryServerTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Acceptance")
	FGuLiAcceptedStateRef RebaseBaseline;

	bool IsWellFormed() const;
};

/** Typed all-or-nothing result for a Bootstrap/Resume/Takeover Candidate transaction. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanAtomicBatchAcceptance
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint64 BatchId = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	EGuLiWingmanAtomicBatchKind BatchKind = EGuLiWingmanAtomicBatchKind::Bootstrap;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|AtomicBatch")
	FGuLiWingmanGroupHandle Group;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint32 LeaseEpoch = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint32 FrozenRosterRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint32 BaselineRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint64 BaselineHash = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	EGuLiWingmanSubmissionDisposition Disposition = EGuLiWingmanSubmissionDisposition::Rejected;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	EGuLiWingmanRejectReason RejectReason = EGuLiWingmanRejectReason::InvalidIdentity;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint8 CommittedFlightMask = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	TArray<uint32> AcceptedSnapshotSequences;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint64 BatchValidatedPayloadHash = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	EGuLiWingmanGroupLifecycle AvailabilityAfter = EGuLiWingmanGroupLifecycle::Unavailable;

	bool IsWellFormed() const;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanLeaseState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Lease")
	FGuLiWingmanGroupHandle Group;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Lease")
	FGuid OwnerPlayerGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Lease")
	FGuid BackupPlayerGuid;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Lease")
	uint32 LeaseEpoch = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Lease")
	EGuLiWingmanGroupLifecycle Lifecycle = EGuLiWingmanGroupLifecycle::Unavailable;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Lease")
	double LifecycleChangedTimeSeconds = 0.0;

	bool IsWellFormed() const;
};

/** Exact acknowledgement of the reliable, ASC-independent ability projection. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiGroupAbilityConfigAck
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Abilities")
	FGuLiWingmanGroupHandle Group;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Abilities")
	uint32 LeaseEpoch = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Abilities")
	uint32 SnapshotRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Abilities")
	uint64 SnapshotHash = 0u;

	bool IsWellFormed() const;
};

/** All six payloads captured from one server cut and committed atomically by the owner. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanBootstrapBundle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Bootstrap")
	FGuLiWingmanBootstrapCommit Commit;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Bootstrap")
	TArray<FGuLiWingmanRosterEntry> Roster;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Bootstrap")
	TArray<FGuLiWingmanAuthorityEntry> AuthorityMap;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Bootstrap")
	TArray<FGuLiWingmanHealthEntry> Health;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Bootstrap")
	TArray<FGuLiWingmanHandle> Dead;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Bootstrap")
	TArray<FGuLiWingmanAcceptedBatch> AcceptedSnapshot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Bootstrap")
	FGuLiGroupAbilityConfigSnapshot AbilityConfig;

	UPROPERTY()
	FGuLiWingmanAttackAuthorityState AttackState;
	UPROPERTY() uint64 AttackStateHash = 0;

	/** Non-zero selects the strict per-Flight Candidate contract. */
	UPROPERTY(VisibleAnywhere, Category = "Wingman|Bootstrap")
	uint32 ConnectionGeneration = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Bootstrap")
	uint32 RosterRevision = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Bootstrap")
	FGuLiWingmanRelayValidationRevisions ValidationRevisions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Bootstrap")
	FGuLiWingmanUploadRateGrant UploadRateGrant;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Bootstrap")
	bool bRequiresAtomicCandidateBatch = false;

	/**
	 * Reliable six-scope cut emitted after an Active roster mutation. This is an
	 * acknowledgement barrier only: it never resets pose/sequence/freshness and
	 * never asks the owner for an all-Flight recovery batch.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Wingman|Bootstrap")
	bool bActiveRosterRefresh = false;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Bootstrap")
	EGuLiWingmanAtomicBatchKind AtomicBatchKind = EGuLiWingmanAtomicBatchKind::Bootstrap;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Bootstrap")
	uint32 AtomicBaselineRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Bootstrap")
	uint64 AtomicBaselineHash = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Bootstrap")
	uint8 RequiredFlightMask = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Bootstrap")
	uint64 RequiredMemberMaskHash = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Bootstrap")
	bool bHasTransferBaseline = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Bootstrap")
	FGuLiWingmanTransferBaseline TransferBaseline;

	bool IsWellFormed() const;
};

struct GULISTRIKE_API FGuLiWingmanSubmissionResult
{
	EGuLiWingmanSubmissionDisposition Disposition = EGuLiWingmanSubmissionDisposition::Rejected;
	EGuLiWingmanRejectReason RejectReason = EGuLiWingmanRejectReason::InvalidIdentity;
	uint32 Sequence = 0u;
	FGuLiWingmanAcceptedBatch AcceptedBatch;
	FGuLiWingmanSimulationAcceptance Acceptance;

	static FGuLiWingmanSubmissionResult Accepted(const FGuLiWingmanAcceptedBatch& Batch);
	static FGuLiWingmanSubmissionResult AcceptedSequence(uint32 Sequence);
	static FGuLiWingmanSubmissionResult Pending(uint32 CandidateSequence);
	static FGuLiWingmanSubmissionResult Rejected(EGuLiWingmanRejectReason Reason, uint32 Sequence = 0u);
};

/**
 * Single Candidate-result wire DTO.  Both a remote Client RPC and the listen-host fast path must
 * pass through its NetSerialize contract before any Accepted snapshot is consumed locally.
 */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanCandidateResultWire
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Acceptance")
	uint32 CandidateSequence = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Acceptance")
	FGuLiWingmanSimulationAcceptance Acceptance;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Acceptance")
	FGuLiWingmanAcceptedBatch AcceptedBatch;

	bool IsWellFormed() const;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FGuLiWingmanCandidateResultWire>
	: TStructOpsTypeTraitsBase2<FGuLiWingmanCandidateResultWire>
{
	enum { WithNetSerializer = true };
};

namespace GuLiWingmanRelayWire
{
	/** Exact in-memory wire copy used by listen hosts and by the common Client RPC consume gate. */
	GULISTRIKE_API bool MakeValidatedCandidateResultCopy(
		const FGuLiWingmanCandidateResultWire& Source,
		FGuLiWingmanCandidateResultWire& OutCopy);
}

enum class EGuLiAtomicCandidateAssemblyDisposition : uint8
{
	Pending = 0,
	Complete,
	Rejected
};

/** UObject-free bounded fragment assembler. It owns no Accepted state and performs no validation movement. */
class GULISTRIKE_API FGuLiWingmanAtomicCandidateAssembler
{
public:
	EGuLiAtomicCandidateAssemblyDisposition SubmitFragment(
		const FGuLiWingmanAtomicCandidateBatchFragment& Fragment,
		double NowSeconds,
		TArray<FGuLiWingmanCandidateBatch>& OutFlights,
		EGuLiWingmanRejectReason& OutRejectReason);
	bool Expire(double NowSeconds, FGuLiWingmanAtomicBatchAcceptance& OutExpired);
	void Reset();
	bool IsPending() const { return Header.BatchId != 0u; }
	uint64 GetPendingBatchId() const { return Header.BatchId; }

private:
	FGuLiWingmanAtomicCandidateBatchHeader Header;
	TArray<FGuLiWingmanAtomicCandidateBatchFragment> Fragments;
	TBitArray<> ReceivedFragments;
	double DeadlineSeconds = 0.0;
	uint32 ReceivedPayloadBytes = 0u;
};

/** Monotonic token bucket. Backward/non-finite clocks fail closed without changing its budget. */
class GULISTRIKE_API FGuLiWingmanTokenBucket
{
public:
	void Reset(double NowSeconds, double InCapacity, double InTokensPerSecond);
	bool Consume(double NowSeconds, double TokenCount = 1.0);
	double GetAvailableTokens() const { return AvailableTokens; }

private:
	double Capacity = 0.0;
	double TokensPerSecond = 0.0;
	double AvailableTokens = 0.0;
	double LastUpdateSeconds = 0.0;
	bool bInitialized = false;
};

namespace GuLiWingmanRelayHash
{
	GULISTRIKE_API uint64 Roster(const TArray<FGuLiWingmanRosterEntry>& Entries);
	GULISTRIKE_API uint64 AuthorityMap(const TArray<FGuLiWingmanAuthorityEntry>& Entries);
	GULISTRIKE_API uint64 Health(const TArray<FGuLiWingmanHealthEntry>& Entries);
	GULISTRIKE_API uint64 Dead(const TArray<FGuLiWingmanHandle>& Entries);
	GULISTRIKE_API uint64 AcceptedSnapshot(const TArray<FGuLiWingmanAcceptedBatch>& Entries);
	GULISTRIKE_API uint64 CandidatePayloads(const TArray<FGuLiWingmanCandidateBatch>& Entries);
	GULISTRIKE_API uint64 RequiredMemberMasks(const TArray<FGuLiWingmanRosterEntry>& Roster);
}
