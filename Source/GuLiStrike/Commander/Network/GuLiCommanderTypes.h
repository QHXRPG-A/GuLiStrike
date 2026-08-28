// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GuLiCommanderTypes.generated.h"

/** Wire contract version for the dynamic Soldier/ControlCohort prototype. */
inline constexpr uint16 GULI_COMMANDER_PROTOCOL_VERSION = 2u;

/** A player control cohort targets 25 soldiers, but can be understrength. */
inline constexpr uint32 GULI_CONTROL_COHORT_TARGET_SIZE = 25u;

/** Protocol ceiling for an owner-only selection, not the current spawn count. */
inline constexpr uint32 GULI_MAX_CONTROL_COHORTS = 400u;

/** Unreliable pose packets stay below the project MTU budget at this bound. */
inline constexpr uint32 GULI_MAX_POSE_SAMPLES_PER_CHUNK = 32u;

/** The authority captures one pose frame every three ticks of its 30 Hz simulation. */
inline constexpr uint32 GULI_POSE_CAPTURE_RATE_HZ = 10u;

/** Supports a future 10,000-soldier frame while keeping chunk indices bounded. */
inline constexpr uint32 GULI_MAX_POSE_CHUNKS_PER_FRAME = 512u;

/** Position and velocity components use signed ten-centimeter units. */
inline constexpr float GULI_POSE_QUANTIZATION_CENTIMETERS = 10.0f;

/** Presentational discontinuity marker; gameplay facts still come from reliable state. */
inline constexpr uint8 GULI_SOLDIER_POSE_FLAG_TELEPORT = 1u << 0u;
inline constexpr uint8 GULI_VALID_SOLDIER_POSE_FLAGS = GULI_SOLDIER_POSE_FLAG_TELEPORT;

UENUM(BlueprintType)
enum class EGuLiTeam : uint8
{
	Unassigned = 0,
	Red,
	Blue
};

UENUM(BlueprintType)
enum class EGuLiCommanderRole : uint8
{
	Unassigned = 0,
	Commander,
	Ground,
	Air,
	Observer
};

UENUM(BlueprintType)
enum class EGuLiSelectionRadiusPreset : uint8
{
	Small = 0,
	Medium,
	Large
};

UENUM(BlueprintType)
enum class EGuLiSelectionModifier : uint8
{
	Replace = 0,
	Toggle,
	Clear
};

UENUM(BlueprintType)
enum class EGuLiCommandAckResult : uint8
{
	Accepted = 0,
	PartiallyAccepted,
	Duplicate,
	InvalidRequest,
	StaleSelectionRevision,
	Unauthorized,
	RateLimited,
	NoSelection,
	InvalidTarget,
	PathFailed
};

/** Distinguishes independent client request-id spaces and prevents selection ACKs consuming move feedback. */
UENUM(BlueprintType)
enum class EGuLiCommandKind : uint8
{
	None = 0,
	Selection,
	Move
};

UENUM(BlueprintType)
enum class EGuLiSoldierLifeState : uint8
{
	Alive = 0,
	Destroyed
};

UENUM(BlueprintType)
enum class EGuLiSoldierPoseState : uint8
{
	Idle = 0,
	Moving,
	Destroyed
};

UENUM(BlueprintType)
enum class EGuLiOrderType : uint8
{
	None = 0,
	Move
};

/** Stable, match-local soldier identity. Zero is invalid and values are never reused in a match. */
USTRUCT()
struct GULISTRIKE_API FGuLiSoldierId
{
	GENERATED_BODY()

	FGuLiSoldierId() = default;
	explicit FGuLiSoldierId(const uint32 InValue)
		: Value(InValue)
	{
	}

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	uint32 Value = 0u;

	bool IsValid() const { return Value != 0u; }
	void Reset() { Value = 0u; }
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	friend bool operator==(const FGuLiSoldierId& Lhs, const FGuLiSoldierId& Rhs) { return Lhs.Value == Rhs.Value; }
	friend bool operator!=(const FGuLiSoldierId& Lhs, const FGuLiSoldierId& Rhs) { return !(Lhs == Rhs); }
	friend bool operator<(const FGuLiSoldierId& Lhs, const FGuLiSoldierId& Rhs) { return Lhs.Value < Rhs.Value; }
	friend uint32 GetTypeHash(const FGuLiSoldierId& SoldierId) { return ::GetTypeHash(SoldierId.Value); }
};

template <>
struct TStructOpsTypeTraits<FGuLiSoldierId> : public TStructOpsTypeTraitsBase2<FGuLiSoldierId>
{
	enum { WithNetSerializer = true, WithIdenticalViaEquality = true };
};

/** Ephemeral server-issued identity for one frozen selection cohort. */
USTRUCT()
struct GULISTRIKE_API FGuLiControlCohortId
{
	GENERATED_BODY()

	FGuLiControlCohortId() = default;
	explicit FGuLiControlCohortId(const uint32 InValue)
		: Value(InValue)
	{
	}

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	uint32 Value = 0u;

	bool IsValid() const { return Value != 0u; }
	void Reset() { Value = 0u; }
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	friend bool operator==(const FGuLiControlCohortId& Lhs, const FGuLiControlCohortId& Rhs) { return Lhs.Value == Rhs.Value; }
	friend bool operator!=(const FGuLiControlCohortId& Lhs, const FGuLiControlCohortId& Rhs) { return !(Lhs == Rhs); }
	friend bool operator<(const FGuLiControlCohortId& Lhs, const FGuLiControlCohortId& Rhs) { return Lhs.Value < Rhs.Value; }
	friend uint32 GetTypeHash(const FGuLiControlCohortId& CohortId) { return ::GetTypeHash(CohortId.Value); }
};

template <>
struct TStructOpsTypeTraits<FGuLiControlCohortId> : public TStructOpsTypeTraitsBase2<FGuLiControlCohortId>
{
	enum { WithNetSerializer = true, WithIdenticalViaEquality = true };
};

namespace GuLiCommanderProtocol
{
	GULISTRIKE_API bool IsPlayableTeam(EGuLiTeam Team);
	GULISTRIKE_API float GetSelectionRadiusCentimeters(EGuLiSelectionRadiusPreset Preset);
	GULISTRIKE_API uint16 QuantizeYawDegrees(float YawDegrees);
	GULISTRIKE_API float DequantizeYawDegrees(uint16 QuantizedYaw);
	GULISTRIKE_API int16 QuantizeCentimetersToDecimeters(float Centimeters);
	GULISTRIKE_API float DequantizeDecimetersToCentimeters(int16 Decimeters);
}

/** Client intent. It intentionally contains no cohort or soldier identifiers. */
USTRUCT()
struct GULISTRIKE_API FGuLiSelectionRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	FVector_NetQuantize Center = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	EGuLiSelectionRadiusPreset RadiusPreset = EGuLiSelectionRadiusPreset::Small;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	EGuLiSelectionModifier Modifier = EGuLiSelectionModifier::Replace;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	uint32 ClientRequestId = 0u;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	uint32 KnownSelectionRevision = 0u;

	bool IsWellFormed() const;
};

/** Client move intent. The server resolves the selection revision to frozen cohort membership. */
USTRUCT()
struct GULISTRIKE_API FGuLiMoveRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	FVector_NetQuantize Target = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	uint32 SelectionRevision = 0u;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	uint32 ClientCommandId = 0u;

	bool IsWellFormed() const;
};

/** One temporary, owner-only selection cohort. Membership is frozen for its selection revision. */
USTRUCT()
struct GULISTRIKE_API FGuLiControlCohortDescriptor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	FGuLiControlCohortId CohortId;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	TArray<FGuLiSoldierId> MemberIds;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint8 AliveCount = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 ActiveOrderId = 0u;

	bool IsValid() const;
	bool Contains(FGuLiSoldierId SoldierId) const;
	void Sanitize();
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template <>
struct TStructOpsTypeTraits<FGuLiControlCohortDescriptor>
	: public TStructOpsTypeTraitsBase2<FGuLiControlCohortDescriptor>
{
	enum { WithNetSerializer = true };
};

/** Owner-only confirmed selection. Cohorts may be understrength but never exceed 25 members. */
USTRUCT()
struct GULISTRIKE_API FGuLiCommanderSelectionState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	TArray<FGuLiControlCohortDescriptor> Cohorts;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 SelectionRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 AcceptedClientRequestId = 0u;

	void Sanitize();
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template <>
struct TStructOpsTypeTraits<FGuLiCommanderSelectionState>
	: public TStructOpsTypeTraitsBase2<FGuLiCommanderSelectionState>
{
	enum { WithNetSerializer = true };
};

USTRUCT()
struct GULISTRIKE_API FGuLiCohortCommandAck
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	FGuLiControlCohortId CohortId;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	EGuLiCommandAckResult Result = EGuLiCommandAckResult::InvalidRequest;
};

/** Server result for one client command; per-cohort results preserve partial acceptance. */
USTRUCT()
struct GULISTRIKE_API FGuLiCommandAck
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	EGuLiCommandKind CommandKind = EGuLiCommandKind::None;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 ClientCommandId = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 BatchOrderId = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	EGuLiCommandAckResult Result = EGuLiCommandAckResult::InvalidRequest;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 ServerSelectionRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	TArray<FGuLiCohortCommandAck> CohortResults;

	bool IsAccepted() const;
	void Sanitize();
};

/** Reliable per-soldier gameplay fact carried by a FastArray replicator. */
USTRUCT()
struct GULISTRIKE_API FGuLiSoldierStateItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	FGuLiSoldierId SoldierId;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	EGuLiTeam Team = EGuLiTeam::Unassigned;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	EGuLiSoldierLifeState LifeState = EGuLiSoldierLifeState::Alive;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint8 Health = 100u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 StateRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 ActiveOrderId = 0u;

	bool IsAlive() const;
	void Sanitize();
};

/** Generic FastArray wire container; the owning actor controls mutation and replication scope. */
USTRUCT()
struct GULISTRIKE_API FGuLiSoldierStateFastArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FGuLiSoldierStateItem> Items;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<
			FGuLiSoldierStateItem,
			FGuLiSoldierStateFastArray>(Items, DeltaParams, *this);
	}

	void Sanitize();
	const FGuLiSoldierStateItem* Find(FGuLiSoldierId SoldierId) const;
	FGuLiSoldierStateItem* FindMutable(FGuLiSoldierId SoldierId);
};

template <>
struct TStructOpsTypeTraits<FGuLiSoldierStateFastArray>
	: public TStructOpsTypeTraitsBase2<FGuLiSoldierStateFastArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** One stable-identity pose sample relative to its containing chunk anchor. */
USTRUCT()
struct GULISTRIKE_API FGuLiCompressedSoldierPose
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	FGuLiSoldierId SoldierId;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	int16 RelativeXDecimeters = 0;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	int16 RelativeYDecimeters = 0;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	int16 RelativeZDecimeters = 0;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	int16 VelocityXDecimetersPerSecond = 0;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	int16 VelocityYDecimetersPerSecond = 0;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	int16 VelocityZDecimetersPerSecond = 0;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint16 FacingYaw = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 ActiveOrderId = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	EGuLiSoldierPoseState State = EGuLiSoldierPoseState::Idle;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint8 Flags = 0u;

	void SetRelativeLocationCentimeters(const FVector& RelativeLocation);
	FVector GetRelativeLocationCentimeters() const;
	void SetVelocityCentimetersPerSecond(const FVector& Velocity);
	FVector GetVelocityCentimetersPerSecond() const;
	bool IsTeleport() const;
	void Sanitize();
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template <>
struct TStructOpsTypeTraits<FGuLiCompressedSoldierPose>
	: public TStructOpsTypeTraitsBase2<FGuLiCompressedSoldierPose>
{
	enum { WithNetSerializer = true };
};

/** One independently consumable fragment of a 10 Hz authoritative soldier pose frame. */
USTRUCT()
struct GULISTRIKE_API FGuLiSoldierPoseChunk
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint16 ProtocolVersion = GULI_COMMANDER_PROTOCOL_VERSION;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 AuthorityEpoch = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 FrameSequence = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 ServerSimTick = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	float ServerTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint16 ChunkIndex = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint16 ChunkCount = 1u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	FVector_NetQuantize Anchor = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	TArray<FGuLiCompressedSoldierPose> Samples;

	void Sanitize();
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template <>
struct TStructOpsTypeTraits<FGuLiSoldierPoseChunk>
	: public TStructOpsTypeTraitsBase2<FGuLiSoldierPoseChunk>
{
	enum { WithNetSerializer = true };
};
