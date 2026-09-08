// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTypes.h"
#include "GameplayTagContainer.h"
#include "GuLiWingmanProtocolTypes.generated.h"

inline constexpr uint8 GULI_WINGMAN_FLIGHT_COUNT = 5u;
inline constexpr uint8 GULI_WINGMAN_MEMBERS_PER_FLIGHT = 5u;
inline constexpr uint8 GULI_WINGMAN_GROUP_SIZE = GULI_WINGMAN_FLIGHT_COUNT * GULI_WINGMAN_MEMBERS_PER_FLIGHT;
inline constexpr double GULI_CARRIER_SOURCE_PENDING_TIMEOUT_SECONDS = 0.25;
inline constexpr uint8 GULI_WINGMAN_MAX_TRAIL_SAMPLES = 8u;
inline constexpr uint8 GULI_WINGMAN_ATOMIC_BATCH_MAX_FRAGMENTS = 8u;
inline constexpr uint32 GULI_WINGMAN_ATOMIC_BATCH_MAX_BYTES = 64u * 1024u;
inline constexpr double GULI_WINGMAN_ATOMIC_BATCH_ASSEMBLY_TIMEOUT_SECONDS = 0.5;
/** Shared hard envelope used by the owner guard and the server validator. */
inline constexpr double GULI_WINGMAN_MAXIMUM_CARRIER_DISTANCE_CENTIMETERS = 250000.0;
/** Keeps quantization and carrier motion away from the authoritative hard edge. */
inline constexpr double GULI_WINGMAN_OWNER_CARRIER_DISTANCE_RESERVE_CENTIMETERS = 10000.0;

/** Server-owned upload rate. RequestedRateClass in a Candidate is never an authority grant. */
UENUM(BlueprintType)
enum class EGuLiWingmanUploadRateClass : uint8
{
	Cruise5Hz = 0,
	HighRate10Hz
};

/** Reliable reason code accompanying a server upload-rate grant. */
UENUM(BlueprintType)
enum class EGuLiWingmanUploadRateGrantReason : uint8
{
	CruiseDefault = 0,
	ServerObservedCombat,
	ServerObservedHazard,
	Expired,
	LeaseChanged
};

/** Non-Active Candidate transaction kind. Values introduced in v7 remain frozen in v8. */
UENUM(BlueprintType)
enum class EGuLiWingmanAtomicBatchKind : uint8
{
	Bootstrap = 0,
	Resume,
	Takeover
};

/** Stable wire values for Candidate flight state. FlightMode remains a uint8 in protocol v8. */
UENUM(BlueprintType)
enum class EGuLiWingmanFlightMode : uint8
{
	Orbit = 0,
	Follow,
	CatchUp,
	Recover,
	Stale
};

/** Stable identity for one Ship-owned wingman group. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanGroupHandle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Identity")
	FGuid ShipInstanceId;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Identity")
	uint32 ShipGeneration = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Identity")
	uint32 GroupGeneration = 0u;

	bool IsValid() const;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	friend bool operator==(const FGuLiWingmanGroupHandle& Lhs, const FGuLiWingmanGroupHandle& Rhs)
	{
		return Lhs.ShipInstanceId == Rhs.ShipInstanceId && Lhs.ShipGeneration == Rhs.ShipGeneration
			&& Lhs.GroupGeneration == Rhs.GroupGeneration;
	}
	friend bool operator!=(const FGuLiWingmanGroupHandle& Lhs, const FGuLiWingmanGroupHandle& Rhs) { return !(Lhs == Rhs); }
	friend uint32 GetTypeHash(const FGuLiWingmanGroupHandle& Handle)
	{
		return HashCombine(GetTypeHash(Handle.ShipInstanceId), HashCombine(Handle.ShipGeneration, Handle.GroupGeneration));
	}
};

template<>
struct TStructOpsTypeTraits<FGuLiWingmanGroupHandle> : TStructOpsTypeTraitsBase2<FGuLiWingmanGroupHandle>
{
	enum { WithNetSerializer = true, WithIdenticalViaEquality = true };
};

/** One of the five deterministic Flights inside a group. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanFlightHandle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Identity")
	FGuLiWingmanGroupHandle Group;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Identity")
	uint8 FlightIndex = MAX_uint8;

	bool IsValid() const;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	friend bool operator==(const FGuLiWingmanFlightHandle& Lhs, const FGuLiWingmanFlightHandle& Rhs)
	{
		return Lhs.Group == Rhs.Group && Lhs.FlightIndex == Rhs.FlightIndex;
	}
	friend bool operator!=(const FGuLiWingmanFlightHandle& Lhs, const FGuLiWingmanFlightHandle& Rhs) { return !(Lhs == Rhs); }
	friend uint32 GetTypeHash(const FGuLiWingmanFlightHandle& Handle)
	{
		return HashCombine(GetTypeHash(Handle.Group), Handle.FlightIndex);
	}
};

template<>
struct TStructOpsTypeTraits<FGuLiWingmanFlightHandle> : TStructOpsTypeTraitsBase2<FGuLiWingmanFlightHandle>
{
	enum { WithNetSerializer = true, WithIdenticalViaEquality = true };
};

/** Stable Emitter identity. EntityGeneration advances when a dead slot is replenished. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanHandle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Identity")
	FGuLiWingmanFlightHandle Flight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Identity")
	uint8 MemberIndex = MAX_uint8;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Identity")
	uint32 EntityGeneration = 0u;

	bool IsValid() const;
	uint8 GetGroupMemberIndex() const;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	friend bool operator==(const FGuLiWingmanHandle& Lhs, const FGuLiWingmanHandle& Rhs)
	{
		return Lhs.Flight == Rhs.Flight && Lhs.MemberIndex == Rhs.MemberIndex
			&& Lhs.EntityGeneration == Rhs.EntityGeneration;
	}
	friend bool operator!=(const FGuLiWingmanHandle& Lhs, const FGuLiWingmanHandle& Rhs) { return !(Lhs == Rhs); }
	friend uint32 GetTypeHash(const FGuLiWingmanHandle& Handle)
	{
		return HashCombine(GetTypeHash(Handle.Flight), HashCombine(Handle.MemberIndex, Handle.EntityGeneration));
	}
};

template<>
struct TStructOpsTypeTraits<FGuLiWingmanHandle> : TStructOpsTypeTraitsBase2<FGuLiWingmanHandle>
{
	enum { WithNetSerializer = true, WithIdenticalViaEquality = true };
};

UENUM(BlueprintType)
enum class EGuLiTargetKind : uint8
{
	None = 0,
	Ship,
	CommanderSoldier,
	Wingman
};

/** Stable, adapter-resolved target identity. It never serializes an Actor/UObject pointer. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiTargetHandle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Target")
	EGuLiTargetKind Kind = EGuLiTargetKind::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Target")
	FGuid AuthorityId;

	UPROPERTY(VisibleAnywhere, Category = "Combat|Target")
	uint32 Generation = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Combat|Target")
	uint32 LocalId = 0u;

	bool IsValid() const;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	friend bool operator==(const FGuLiTargetHandle& Lhs, const FGuLiTargetHandle& Rhs)
	{
		return Lhs.Kind == Rhs.Kind && Lhs.AuthorityId == Rhs.AuthorityId
			&& Lhs.Generation == Rhs.Generation && Lhs.LocalId == Rhs.LocalId;
	}
	friend bool operator!=(const FGuLiTargetHandle& Lhs, const FGuLiTargetHandle& Rhs) { return !(Lhs == Rhs); }
	friend uint32 GetTypeHash(const FGuLiTargetHandle& Handle)
	{
		return HashCombine(static_cast<uint8>(Handle.Kind), HashCombine(GetTypeHash(Handle.AuthorityId),
			HashCombine(Handle.Generation, Handle.LocalId)));
	}
};

template<>
struct TStructOpsTypeTraits<FGuLiTargetHandle> : TStructOpsTypeTraitsBase2<FGuLiTargetHandle>
{
	enum { WithNetSerializer = true, WithIdenticalViaEquality = true };
};

/** Exact accepted-pose source used for spatial fire validation. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiAcceptedStateRef
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 MatchEpoch = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 GroupGeneration = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 AcceptedSequence = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 ClientSimTick = 0u;

	bool IsValid() const;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FGuLiAcceptedStateRef> : TStructOpsTypeTraitsBase2<FGuLiAcceptedStateRef>
{
	enum { WithNetSerializer = true };
};

/** References an exact post-replay state from UGuLiShipMovementComponent. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiCarrierSourceRef
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 CanonicalEpoch = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 MoveRevision = 0u;

	bool IsValid() const { return CanonicalEpoch != 0u && MoveRevision != 0u; }
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FGuLiCarrierSourceRef> : TStructOpsTypeTraitsBase2<FGuLiCarrierSourceRef>
{
	enum { WithNetSerializer = true };
};

/** Quantized owner-produced state; server may normalize and store it but never integrates it. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanCandidateSample
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Network")
	FGuLiWingmanHandle Wingman;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Network")
	FIntVector PositionCentimeters = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Network")
	FIntVector VelocityCentimetersPerSecond = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Network")
	FIntVector RotationCentiDegrees = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Network")
	uint8 FlightMode = 0u;

	bool IsWellFormed(const FGuLiWingmanGroupHandle& ExpectedGroup) const;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FGuLiWingmanCandidateSample> : TStructOpsTypeTraitsBase2<FGuLiWingmanCandidateSample>
{
	enum { WithNetSerializer = true };
};

/**
 * An explicit client-produced waypoint between the last Accepted endpoint and the Candidate endpoint.
 * Authority validates every adjacent segment; it never synthesizes a missing trajectory.
 */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanCandidateTrailSample
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 ClientSimTick = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	double CaptureEstimatedServerTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Network")
	FGuLiCarrierSourceRef CarrierSource;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Network")
	TArray<FGuLiWingmanCandidateSample> Samples;

	bool IsWellFormed(const FGuLiWingmanGroupHandle& ExpectedGroup, uint8 ExpectedFlightIndex,
		uint8 ExpectedMemberMask) const;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FGuLiWingmanCandidateTrailSample>
	: TStructOpsTypeTraitsBase2<FGuLiWingmanCandidateTrailSample>
{
	enum { WithNetSerializer = true };
};

/** Reliable authority-selected target. Ground points are traced by authority, never supplied by the firing client. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanAttackTarget
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Wingman|Target") FGuLiTargetHandle Target;
	UPROPERTY(BlueprintReadOnly, Category="Wingman|Target") FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Wingman|Target") float Radius = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category="Wingman|Target") bool bGround = false;
	UPROPERTY(BlueprintReadOnly, Category="Wingman|Target") bool bSpecified = false;
	UPROPERTY() uint32 Revision = 0;
	UPROPERTY() double ServerTime = 0.0;
	bool IsValid() const { return Target.IsValid() && Revision != 0 && !Location.ContainsNaN(); }
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<> struct TStructOpsTypeTraits<FGuLiWingmanAttackTarget> : TStructOpsTypeTraitsBase2<FGuLiWingmanAttackTarget>
{ enum { WithNetSerializer = true }; };

/** Per-emitter authority checkpoint, including cooldown across owner handoff. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanAttackCheckpoint
{
	GENERATED_BODY()
	UPROPERTY() FGuLiWingmanHandle Emitter;
	UPROPERTY() FName SlotId;
	UPROPERTY() FName SkillId;
	UPROPERTY() uint64 DefinitionChecksum = 0;
	UPROPERTY() FGuLiTargetHandle FrozenTargetHandle;
	UPROPERTY() uint32 ProfileRevision = 0;
	UPROPERTY() uint32 RunId = 0;
	UPROPERTY() uint32 LeaseEpoch = 0;
	UPROPERTY() int32 LastShotIndex = -1;
	UPROPERTY() double StartTime = 0;
	UPROPERTY() double NextFireTime = 0;
	UPROPERTY() FVector FrozenTarget = FVector::ZeroVector;
	UPROPERTY() FVector ApproachDirection = FVector::ForwardVector;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<> struct TStructOpsTypeTraits<FGuLiWingmanAttackCheckpoint> : TStructOpsTypeTraitsBase2<FGuLiWingmanAttackCheckpoint>
{ enum { WithNetSerializer = true }; };

USTRUCT()
struct GULISTRIKE_API FGuLiWingmanAttackAuthorityState
{
	GENERATED_BODY()
	UPROPERTY() FGuLiWingmanAttackTarget Target;
	UPROPERTY() TArray<FGuLiWingmanAttackCheckpoint> Checkpoints;
	UPROPERTY() uint32 Revision = 0;
	uint64 ComputeStableHash() const;
};

/** Bounded fire record referencing an explicit validated pose tick in this same Flight batch. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanAttackFireRecord
{
	GENERATED_BODY()
	UPROPERTY() uint8 MemberIndex = MAX_uint8;
	UPROPERTY() uint32 ClientSimTick = 0;
	UPROPERTY() FName SlotId;
	UPROPERTY() uint32 ProfileRevision = 0;
	UPROPERTY() uint32 LoadoutRevision = 0;
	UPROPERTY() uint32 RunId = 0;
	UPROPERTY() uint16 ShotIndex = 0;
	UPROPERTY() FGuLiWingmanAttackTarget Target;
	/** Frozen approach selected before entry; authority checks it against the captured pose. */
	UPROPERTY() FVector ApproachDirection = FVector::ForwardVector;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<> struct TStructOpsTypeTraits<FGuLiWingmanAttackFireRecord> : TStructOpsTypeTraitsBase2<FGuLiWingmanAttackFireRecord>
{ enum { WithNetSerializer = true }; };

/** Atomic owner candidate batch. Old ability versions are rejected before sequence reservation. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanCandidateBatch
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 ProtocolVersion = GULI_WINGMAN_PROTOCOL_VERSION;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 MatchEpoch = 0u;

	/** Current public connection handshake generation. Zero is accepted only by the legacy test adapter. */
	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 ConnectionGeneration = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Network")
	FGuLiWingmanGroupHandle Group;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 LeaseEpoch = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 RosterRevision = 0u;

	/** One of five Flights. MAX_uint8 identifies the pre-flight-contract compatibility adapter only. */
	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint8 FlightIndex = MAX_uint8;

	/** Bit i is set exactly when live MemberIndex i is required in this Flight. */
	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint8 RequiredMemberMask = 0u;

	/** Request only. Authority independently computes the allowed class from its reliable grant. */
	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	EGuLiWingmanUploadRateClass RequestedRateClass = EGuLiWingmanUploadRateClass::Cruise5Hz;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 ObservedGrantRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 CandidateSequence = 0u;

	/** Monotonic within this Flight/Lease producer. CandidateSequence remains the RPC correlation id. */
	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 FrameSequence = 0u;

	/** Last server Accepted snapshot sequence on which this endpoint was simulated. */
	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 BaseAcceptedSequence = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 ClientSimTick = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	double CaptureEstimatedServerTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 NavSchemaRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint64 NavDataChecksum = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 TuningRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 ObstacleRevision = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Network")
	FGuLiCarrierSourceRef CarrierSource;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 AbilitySetRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 FormationCommandRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint64 FormationDefinitionChecksum = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Network")
	TArray<FGuLiWingmanCandidateSample> Samples;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Network")
	TArray<FGuLiWingmanCandidateTrailSample> TrailSamples;

	UPROPERTY()
	TArray<FGuLiWingmanAttackFireRecord> AttackFireRecords;

	bool IsWellFormed() const;
	bool UsesStrictFlightContract() const { return FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; }
	uint64 ComputeStablePayloadHash() const;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FGuLiWingmanCandidateBatch> : TStructOpsTypeTraitsBase2<FGuLiWingmanCandidateBatch>
{
	enum { WithNetSerializer = true };
};

/** Reliable server-owned upload authorization. A client Candidate can only observe this value. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanUploadRateGrant
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 ProtocolVersion = GULI_WINGMAN_PROTOCOL_VERSION;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Network")
	FGuLiWingmanGroupHandle Group;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 ConnectionGeneration = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 LeaseEpoch = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	EGuLiWingmanUploadRateClass RateClass = EGuLiWingmanUploadRateClass::Cruise5Hz;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 GrantRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	uint32 EffectiveClientSimTick = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	double ExpiryServerTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Network")
	EGuLiWingmanUploadRateGrantReason Reason = EGuLiWingmanUploadRateGrantReason::CruiseDefault;

	bool IsWellFormed() const;
	bool IsHighRateActive(uint32 ClientSimTick, double EstimatedServerTimeSeconds) const;
};

/** Frozen header shared by Bootstrap, Resume and Takeover Candidate transactions. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanAtomicCandidateBatchHeader
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint32 ProtocolVersion = GULI_WINGMAN_PROTOCOL_VERSION;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint64 BatchId = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	EGuLiWingmanAtomicBatchKind BatchKind = EGuLiWingmanAtomicBatchKind::Bootstrap;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|AtomicBatch")
	FGuLiWingmanGroupHandle Group;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint32 ConnectionGeneration = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint32 LeaseEpoch = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint32 FrozenRosterRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint8 FrozenRequiredFlightMask = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint64 FrozenRequiredMemberMaskHash = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint32 BaselineRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint64 BaselineHash = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint8 IncludedFlightMask = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint32 ClientBatchStartTick = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint32 BatchPayloadBytes = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint8 FragmentCount = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint64 BatchPayloadHash = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	bool bAtomicCommit = true;

	bool IsWellFormed() const;
};

/** One UE-RPC fragment. Flights are not visible to Accepted Store until every fragment is assembled. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanAtomicCandidateBatchFragment
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|AtomicBatch")
	FGuLiWingmanAtomicCandidateBatchHeader Header;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|AtomicBatch")
	uint8 FragmentIndex = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|AtomicBatch")
	TArray<FGuLiWingmanCandidateBatch> Flights;

	bool IsWellFormed() const;
	uint32 EstimatePayloadBytes() const;
	uint64 ComputePayloadHash() const;
};

/** Client fire intent. Damage, cooldown commit and projectile truth are never client-provided. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanFireIntent
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Combat")
	uint32 ProtocolVersion = GULI_WINGMAN_PROTOCOL_VERSION;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Combat")
	uint32 MatchEpoch = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Combat")
	FGuLiWingmanGroupHandle Group;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Combat")
	uint32 LeaseEpoch = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Combat")
	uint32 DomainFireSequence = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Combat")
	FGuLiWingmanHandle Emitter;

	/** Stable equipment identity; member identity remains Emitter. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Combat")
	FGuLiWeaponBindingKey Binding;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Combat")
	FGuLiAcceptedStateRef SourceAcceptedState;

	/** Client fixed-step at which the intent was authored; a correlation hint, never combat truth. */
	UPROPERTY(VisibleAnywhere, Category = "Wingman|Combat")
	uint32 ClientFireTick = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Combat")
	FGuLiTargetHandle Target;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Combat")
	FGameplayTag WeaponAbilityId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Combat")
	FName SkillId;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Combat")
	uint32 LoadoutRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Combat")
	uint32 ProfileRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Combat")
	uint32 WeaponDefinitionRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Combat")
	uint32 AbilitySetRevision = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Combat")
	FIntVector AimDirectionMilli = FIntVector(1000, 0, 0);

	/** Client LOS prediction for presentation/diagnostics. Authority always traces again. */
	UPROPERTY(VisibleAnywhere, Category = "Wingman|Combat")
	bool bClientPredictedLineOfSight = false;

	bool IsWellFormed() const;
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FGuLiWingmanFireIntent> : TStructOpsTypeTraitsBase2<FGuLiWingmanFireIntent>
{
	enum { WithNetSerializer = true };
};

UENUM(BlueprintType)
enum class EGuLiWingmanBootstrapScope : uint8
{
	Roster = 0,
	AuthorityMap,
	Health,
	Dead,
	AcceptedSnapshot,
	GroupAbilityConfig,
	Count UMETA(Hidden)
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanBootstrapScopeState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Bootstrap")
	EGuLiWingmanBootstrapScope Scope = EGuLiWingmanBootstrapScope::Roster;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Bootstrap")
	uint32 Revision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Bootstrap")
	uint64 Hash = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Bootstrap")
	uint16 ChunkCount = 0u;

	bool IsWellFormed() const;
};

/** Atomic six-scope bootstrap commit. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanBootstrapCommit
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Bootstrap")
	uint32 ProtocolVersion = GULI_WINGMAN_PROTOCOL_VERSION;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Bootstrap")
	uint64 CutId = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Bootstrap")
	FGuLiWingmanGroupHandle Group;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Bootstrap")
	TArray<FGuLiWingmanBootstrapScopeState> Scopes;

	bool IsWellFormed() const;
	const FGuLiWingmanBootstrapScopeState* FindScope(EGuLiWingmanBootstrapScope Scope) const;
};

/** Frozen handoff baseline; a takeover cannot silently bind a newer ability configuration. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanTransferBaseline
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Transfer")
	uint32 ProtocolVersion = GULI_WINGMAN_PROTOCOL_VERSION;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Transfer")
	uint64 CutId = 0u;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wingman|Transfer")
	FGuLiWingmanGroupHandle Group;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Transfer")
	uint32 LeaseEpoch = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Transfer")
	uint32 AbilityConfigRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Transfer")
	uint64 AbilityConfigHash = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Transfer")
	uint32 AcceptedSnapshotRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Wingman|Transfer")
	uint64 AcceptedSnapshotHash = 0u;

	bool IsWellFormed() const;
};

UENUM(BlueprintType)
enum class EGuLiWingmanRejectReason : uint8
{
	None = 0,
	ProtocolMismatch,
	InvalidIdentity,
	WrongGeneration,
	WrongLease,
	InactiveGroup,
	MissingAbilityConfig,
	StaleAbilitySetRevision,
	StaleLoadoutRevision,
	StaleProfileRevision,
	StaleFormationCommand,
	FormationChecksumMismatch,
	UnknownWeaponAbility,
	UnknownWeaponChannel,
	WeaponSkillMismatch,
	WeaponDefinitionMismatch,
	EmitterDead,
	StaleSourceState,
	CarrierMovePendingTimeout,
	CarrierMoveExpired,
	InvalidTarget,
	FriendlyTarget,
	OutOfRange,
	NoLineOfSight,
	CooldownActive,
	Duplicate,
	RateLimited,
	WrongConnectionGeneration,
	StaleRosterRevision,
	WrongFlightCoverage,
	StaleFrameSequence,
	StaleAcceptedBaseline,
	CaptureTimeInvalid,
	NavigationRevisionMismatch,
	UploadGrantMismatch,
	TrailInvalid,
	AtomicBatchConflict,
	AtomicBatchTooLarge,
	AtomicBatchFragmentInvalid,
	AtomicBatchHashMismatch,
	AtomicBatchIncomplete,
	AtomicBatchExpired
};

/** Pure pre-reservation gates shared by the RPC handler and contract tests. */
namespace GuLiWingmanProtocol
{
	GULISTRIKE_API EGuLiWingmanRejectReason ValidateCandidateAbilityConfig(
		const FGuLiWingmanCandidateBatch& Candidate,
		const FGuLiGroupAbilityConfigSnapshot* ConfirmedConfig);

	GULISTRIKE_API EGuLiWingmanRejectReason ValidateFireIntentAbilityConfig(
		const FGuLiWingmanFireIntent& Intent,
		const FGuLiGroupAbilityConfigSnapshot* ConfirmedConfig);
}
