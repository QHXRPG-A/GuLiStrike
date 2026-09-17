// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/GuLiCommanderSimulationTiming.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Engine/NetSerialization.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "Gameplay/Units/GuLiEngineeringCommandTypes.h"
#include "GuLiCommanderTypes.generated.h"

/** 一个临时控制组最多 25 名士兵，允许不足额；不是网络 Actor 数量。 */
inline constexpr uint32 GULI_CONTROL_COHORT_TARGET_SIZE = 25u;

/** 拥有者选择状态最多 400 个控制组；协议上限不等于当前生成数量。 */
inline constexpr uint32 GULI_MAX_CONTROL_COHORTS = 400u;

/** Alt 同兵种扩选本次最多选入 1000 人；不限制 Shift 累计选择。 */
inline constexpr uint32 GULI_MAX_SAME_TYPE_SELECTION = 1000u;
inline constexpr uint32 GULI_MAX_CONTROLLABLE_ACTOR_SELECTION = 64u;
inline constexpr uint16 GULI_DEFAULT_SOLDIER_UNIT_TYPE_ID = 1u;

/** 每块最多 32 个姿态样本，用于控制载荷；实际网络包还包含 UE/传输层开销。 */
inline constexpr uint32 GULI_MAX_POSE_SAMPLES_PER_CHUNK = 32u;

/** 10 Hz 权威模拟每步捕获一次姿态；远处/静止单位仍按连接的降频配置发送。 */
inline constexpr uint32 GULI_POSE_CAPTURE_RATE_HZ = 10u;

/** 将同一捕获帧分三相发出，独立于权威模拟频率，避免姿态 RPC 集中挤满一个网络帧。 */
inline constexpr uint8 GULI_POSE_DISPATCH_PHASE_COUNT = 3u;

/** 每帧最多 512 块；为空间分块留余量，不表示当前已有一万士兵。 */
inline constexpr uint32 GULI_MAX_POSE_CHUNKS_PER_FRAME = 512u;

/** Network-only units. Authority simulation retains full centimeter precision. */
inline constexpr double GULI_POSE_XY_STEP_CENTIMETERS = 100.0;
inline constexpr double GULI_POSE_Z_STEP_CENTIMETERS = 10.0;
inline constexpr double GULI_POSE_VELOCITY_STEP_CENTIMETERS_PER_SECOND = 100.0;

/** 表现瞬移标志；生命等玩法事实仍以离散状态复制为准。 */
inline constexpr uint8 GULI_SOLDIER_POSE_FLAG_TELEPORT = 1u << 0u;
inline constexpr uint8 GULI_VALID_SOLDIER_POSE_FLAGS = GULI_SOLDIER_POSE_FLAG_TELEPORT;

UENUM(BlueprintType)
enum class EGuLiSelectionRadiusPreset : uint8
{
	Small = 0,
	Medium,
	Large
};

UENUM(BlueprintType)
enum class EGuLiSelectionKind : uint8
{
	Radius = 0,
	Point,
	Box,
	SameType
};

UENUM(BlueprintType)
enum class EGuLiSelectionModifier : uint8
{
	Replace = 0,
	Toggle,
	Clear,
	Add
};

// Accepted/PartiallyAccepted 只代表接令结果，不代表单位已到目标；其他枚举给出具体拒绝原因。
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

/** 区分选兵与移动两个独立请求序号空间；路由 ACK 时必须同时比较种类和 ID。 */
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
	Move,
	AttackMove
};

/** 战局内稳定的士兵身份，0 无效，同一战局不复用；不是数组下标或客户端 Mass 句柄。 */
USTRUCT(BlueprintType)
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

/** 服务器分配的临时控制组身份；与某次选择的冻结成员集合关联，不是永久编队 ID。 */
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
	GULISTRIKE_API uint8 QuantizeYawDegrees(float YawDegrees);
	GULISTRIKE_API float DequantizeYawDegrees(uint8 QuantizedYaw);
}

/** 客户端选兵意图。SeedSoldierId 仅是带射线校验的点选目标提示；最终成员由服务器产生。 */
USTRUCT()
struct GULISTRIKE_API FGuLiSelectionRequest
{
	GENERATED_BODY()

	// 默认 Radius 保持已有原生 QA 调用的区域意图；玩家默认工具形状另由 Controller 管理。
	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	EGuLiSelectionKind Kind = EGuLiSelectionKind::Radius;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	FVector_NetQuantize Center = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	EGuLiSelectionRadiusPreset RadiusPreset = EGuLiSelectionRadiusPreset::Small;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	EGuLiSelectionModifier Modifier = EGuLiSelectionModifier::Replace;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	FGuLiSoldierId SeedSoldierId;

	/** Mutually exclusive with SeedSoldierId for point/same-type Actor selection. */
	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	FGuLiControllableActorId SeedActorId;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	FVector_NetQuantize RayOrigin = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	FVector_NetQuantizeNormal RayDirection = FVector::ForwardVector;

	// 点选屏幕容差对应的视锥半角；服务器另加有界的位置表现误差，不允许无限扩大。
	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	float PickHalfAngleRadians = 0.012f;

	// 从同一 RayOrigin 反投影，按屏幕左上、右上、右下、左下顺序排列。
	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	FVector_NetQuantizeNormal BoxTopLeftRay = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	FVector_NetQuantizeNormal BoxTopRightRay = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	FVector_NetQuantizeNormal BoxBottomRightRay = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	FVector_NetQuantizeNormal BoxBottomLeftRay = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	uint32 ClientRequestId = 0u;

	// 客户端已知的选择版本；排队选兵会用前一 ACK 的服务器版本更新此值。
	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	uint32 KnownSelectionRevision = 0u;

	bool IsWellFormed() const;
};

/** 客户端移动意图：Target 为目标，SelectionRevision 指向服务器已有的冻结选择，ClientCommandId 用于回执关联。 */
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

	/** Ground moves reach every compatible selection; cluster/factory orders route only to miners. */
	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	EGuLiMiningOrderType MiningOrderType = EGuLiMiningOrderType::Move;

	UPROPERTY(EditAnywhere, Category = "Commander|Network")
	uint16 TargetClusterId = 0u;

	/** None is an ordinary ground/mining command; otherwise this is explicit outpost relocation. */
	UPROPERTY(EditAnywhere, Category="Commander|Network") FName TargetTerritoryId;

	bool IsWellFormed() const;
};

/** 仅拥有者可见的临时控制组：MemberIds 为冻结成员，AliveCount/ActiveOrderId 为可刷新的摘要。 */
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

/** 服务器确认的 OwnerOnly 选择；每组最多 25 人，SelectionRevision 标识成员选择版本。 */
USTRUCT()
struct GULISTRIKE_API FGuLiCommanderSelectionState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	TArray<FGuLiControlCohortDescriptor> Cohorts;

	/** Stable Actor selections share the same SelectionRevision as Mass cohorts. */
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	TArray<FGuLiControllableActorId> ActorIds;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 SelectionRevision = 0u;

	// 服务器最后接受的选兵请求 ID；用于把属性副本与本地选兵意图对应。
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

	/** Frozen MemberIds count used to interpret the two masks below; only the low 25 bits may be set. */
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint8 MemberCount = 0u;

	/** Bit N means the Nth frozen member was alive, owned, and eligible for this move attempt. */
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 EligibleMemberMask = 0u;

	/** Bit N means the Nth frozen member was committed to BatchOrderId; always a subset of EligibleMemberMask. */
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 AcceptedMemberMask = 0u;

	uint32 GetValidMemberMask() const;
	bool IsMemberEligible(uint8 MemberIndex) const;
	bool IsMemberAccepted(uint8 MemberIndex) const;
	uint8 GetAcceptedMemberCount() const;
	void Sanitize();
};

/** 服务器对一次意图的业务回执；数据本身不是 RPC，由 NetSync 的 Client RPC 运送。
 * CommandKind + ClientCommandId 关联原请求；BatchOrderId 关联服务器接受的移动批次。
 * Result 是总体结果，CohortResults 是逐组结果，ServerSelectionRevision 是本回执携带的选择版本。 */
USTRUCT()
struct FGuLiEngineeringCommandAck
{
	GENERATED_BODY()
	UPROPERTY() FGuLiControllableActorId ActorId;
	UPROPERTY() EGuLiTransitOrderResult Result = EGuLiTransitOrderResult::InvalidRequest;
};

USTRUCT()
struct GULISTRIKE_API FGuLiCommandAck
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	EGuLiCommandKind CommandKind = EGuLiCommandKind::None;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 ClientCommandId = 0u;

	// 服务器接受移动时生成的批次号；普通选兵/新拒绝通常为 0，旧 Duplicate 可能携带缓存批次。
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 BatchOrderId = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	EGuLiCommandAckResult Result = EGuLiCommandAckResult::InvalidRequest;

	// 该 ACK 对应的服务器选择版本；缓存重放时不保证等于服务器此刻最新版本。
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 ServerSelectionRevision = 0u;

	// 逐控制组结果保留部分成功信息；不能把所有失败组当作已加入 BatchOrderId。
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	TArray<FGuLiCohortCommandAck> CohortResults;

	UPROPERTY() TArray<FGuLiEngineeringCommandAck> EngineeringResults;

	// 仅检查总体结果为全部或部分接受；不检查抵达、执行结束或复制状态是否已到达。
	bool IsAccepted() const;
	void Sanitize();
};

/** FastArray 承载的单兵离散状态：身份、阵营、生命和命令编号；不包含连续位置。 */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiSoldierStateItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	FGuLiSoldierId SoldierId;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	EGuLiTeam Team = EGuLiTeam::Unassigned;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint16 UnitTypeId = GULI_DEFAULT_SOLDIER_UNIT_TYPE_ID;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	EGuLiSoldierLifeState LifeState = EGuLiSoldierLifeState::Alive;

	UPROPERTY() bool bPhased = false;
	UPROPERTY() bool bExternalActionsLocked = false;
	/** Reliable landing baseline protects against a lost teleport pose packet. */
	UPROPERTY() uint32 DisplacementFrameFloor = 0;
	UPROPERTY() FVector DisplacementLocation = FVector::ZeroVector;
	UPROPERTY() float DisplacementYaw = 0;
	UPROPERTY() double DisplacementSimulationTime = 0;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	float Health = 100.0f;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	float MaxHealth = 100.0f;

	// 单兵离散状态版本，与整份名册的 SnapshotRevision、选择版本互相独立。
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 StateRevision = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 ActiveOrderId = 0u;

	bool IsAlive() const;
	void Sanitize();
};

DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiSoldierRemovalSignature, TConstArrayView<FGuLiSoldierId>);

/** FastArray 增量容器；由所属 Replicator 决定复制范围，并在增改/删除后标脏。 */
USTRUCT()
struct GULISTRIKE_API FGuLiSoldierStateFastArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FGuLiSoldierStateItem> Items;

	// Native notification carries the identities before FastArray erases them.
	FGuLiSoldierRemovalSignature OnRemoved;
	void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);

	// UE 增量序列化入口，由 WithNetDeltaSerializer 接入；脏标记由 Replicator 维护。
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

/** OwnerOnly authoritative move endpoint for one currently executing soldier. */
USTRUCT()
struct GULISTRIKE_API FGuLiMoveEndpointItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	FGuLiSoldierId SoldierId;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 ActiveOrderId = 0u;

	/** Authority location captured when this order was committed. */
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	FVector_NetQuantize CommandStart = FVector::ZeroVector;

	/** Actual free destination assigned by authority, not the original click location. */
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	FVector_NetQuantize FinalDestination = FVector::ZeroVector;

	/** Per-soldier non-zero serial, advanced whenever either endpoint or order changes. */
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 Revision = 0u;

	bool IsValid() const;
	void Sanitize();
};

/** Incremental OwnerOnly endpoint set; the owning component controls replication scope and authority writes. */
USTRUCT()
struct GULISTRIKE_API FGuLiMoveEndpointFastArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FGuLiMoveEndpointItem> Items;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<
			FGuLiMoveEndpointItem,
			FGuLiMoveEndpointFastArray>(Items, DeltaParams, *this);
	}

	void Sanitize();
	const FGuLiMoveEndpointItem* Find(FGuLiSoldierId SoldierId) const;
	FGuLiMoveEndpointItem* FindMutable(FGuLiSoldierId SoldierId);
	bool Upsert(
		FGuLiSoldierId SoldierId,
		uint32 ActiveOrderId,
		const FVector& CommandStart,
		const FVector& FinalDestination);
	bool Remove(FGuLiSoldierId SoldierId);
	int32 ReplaceWith(TConstArrayView<FGuLiMoveEndpointItem> Endpoints);
	bool ResetEndpoints();
};

template <>
struct TStructOpsTypeTraits<FGuLiMoveEndpointFastArray>
	: public TStructOpsTypeTraitsBase2<FGuLiMoveEndpointFastArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** Native quantized sample; only FGuLiEncodedPoseBlock is serialized on the wire. */
struct GULISTRIKE_API FGuLiQuantizedSoldierPose
{
	FGuLiSoldierId SoldierId;

	// Stable world coordinates: XY in meters, Z in decimeters.
	int32 WorldXMeters = 0;
	int32 WorldYMeters = 0;
	int32 WorldZDecimeters = 0;

	// Velocity components in meters/second.
	int16 VelocityXMetersPerSecond = 0;
	int16 VelocityYMetersPerSecond = 0;
	int16 VelocityZMetersPerSecond = 0;

	// 256 directions; interpolation and residuals take the shortest angular arc.
	uint8 FacingYaw = 0u;
	uint32 ActiveOrderId = 0u;
	EGuLiSoldierPoseState State = EGuLiSoldierPoseState::Idle;
	uint8 Flags = 0u;

	bool SetWorldLocationCentimeters(const FVector& WorldLocation);
	FVector GetWorldLocationCentimeters() const;
	bool SetVelocityCentimetersPerSecond(const FVector& Velocity);
	FVector GetVelocityCentimetersPerSecond() const;
	bool IsTeleport() const;
};

/** Native capture/decoded batch. Wire version and validation belong to the connection codec. */
struct GULISTRIKE_API FGuLiSoldierPoseChunk
{
	// 所属战局，必须与 Bootstrap 接受的 MatchEpoch 一致，隔离旧战局迟到数据。
	uint32 AuthorityEpoch = 0u;

	// 捕获帧序号；同帧多个块共享此值，不能用它直接过滤该帧后续块。
	uint32 FrameSequence = 0u;

	// 服务器模拟步编号；ServerTimeSeconds 是样本时间，客户端收包时间不是样本生成时间。
	uint32 ServerSimTick = 0u;
	float ServerTimeSeconds = 0.0f;

	// 块号从 0 开始，必须小于 ChunkCount；并不要求客户端等待全部块到齐。
	uint16 ChunkIndex = 0u;
	uint16 ChunkCount = 1u;

	TArray<FGuLiQuantizedSoldierPose> Samples;
};
