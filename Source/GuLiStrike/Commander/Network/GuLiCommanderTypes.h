// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Engine/NetSerialization.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GuLiCommanderTypes.generated.h"

/** 一个临时控制组最多 25 名士兵，允许不足额；不是网络 Actor 数量。 */
inline constexpr uint32 GULI_CONTROL_COHORT_TARGET_SIZE = 25u;

/** 拥有者选择状态最多 400 个控制组；协议上限不等于当前生成数量。 */
inline constexpr uint32 GULI_MAX_CONTROL_COHORTS = 400u;

/** Alt 同兵种扩选本次最多选入 1000 人；不限制 Shift 累计选择。 */
inline constexpr uint32 GULI_MAX_SAME_TYPE_SELECTION = 1000u;
inline constexpr uint16 GULI_DEFAULT_SOLDIER_UNIT_TYPE_ID = 1u;

/** 每块最多 32 个姿态样本，用于控制载荷；实际网络包还包含 UE/传输层开销。 */
inline constexpr uint32 GULI_MAX_POSE_SAMPLES_PER_CHUNK = 32u;

/** 30 Hz 权威模拟每三个 Tick 目标捕获一次姿态，即 10 Hz。 */
inline constexpr uint32 GULI_POSE_CAPTURE_RATE_HZ = 10u;

/** 每帧最多 512 块；为空间分块留余量，不表示当前已有一万士兵。 */
inline constexpr uint32 GULI_MAX_POSE_CHUNKS_PER_FRAME = 512u;

/** 相对位置每单位 10 cm，速度每单位 10 cm/s；使用有符号 int16。 */
inline constexpr float GULI_POSE_QUANTIZATION_CENTIMETERS = 10.0f;

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
	Move
};

/** 战局内稳定的士兵身份，0 无效，同一战局不复用；不是数组下标或客户端 Mass 句柄。 */
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
	GULISTRIKE_API uint16 QuantizeYawDegrees(float YawDegrees);
	GULISTRIKE_API float DequantizeYawDegrees(uint16 QuantizedYaw);
	GULISTRIKE_API int16 QuantizeCentimetersToDecimeters(float Centimeters);
	GULISTRIKE_API float DequantizeDecimetersToCentimeters(int16 Decimeters);
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

	// 仅检查总体结果为全部或部分接受；不检查抵达、执行结束或复制状态是否已到达。
	bool IsAccepted() const;
	void Sanitize();
};

/** FastArray 承载的单兵离散状态：身份、阵营、生命和命令编号；不包含连续位置。 */
USTRUCT()
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

/** FastArray 增量容器；由所属 Replicator 决定复制范围，并在增改/删除后标脏。 */
USTRUCT()
struct GULISTRIKE_API FGuLiSoldierStateFastArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FGuLiSoldierStateItem> Items;

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

/** 单兵压缩姿态：以 SoldierId 关联名册，位置相对所属块 Anchor，速度与朝向用于平滑。 */
USTRUCT()
struct GULISTRIKE_API FGuLiCompressedSoldierPose
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	FGuLiSoldierId SoldierId;

	// 三轴相对位置采用分米；世界坐标需要 Anchor + 解量化偏移，不能直接将该值当厘米。
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	int16 RelativeXDecimeters = 0;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	int16 RelativeYDecimeters = 0;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	int16 RelativeZDecimeters = 0;

	// 三轴速度采用分米/秒；不是两次包到达时间之间的位移。
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	int16 VelocityXDecimetersPerSecond = 0;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	int16 VelocityYDecimetersPerSecond = 0;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	int16 VelocityZDecimetersPerSecond = 0;

	// 一周朝向量化到 uint16；绕回 0/65535 时需要按角度而非普通整数差插值。
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

/** 一帧权威姿态中可独立消费的块；丢块不补齐整帧，由后续新样本恢复表现。 */
USTRUCT()
struct GULISTRIKE_API FGuLiSoldierPoseChunk
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint16 ProtocolVersion = GULI_COMMANDER_PROTOCOL_VERSION;

	// 所属战局，必须与 Bootstrap 接受的 MatchEpoch 一致，隔离旧战局迟到数据。
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 AuthorityEpoch = 0u;

	// 捕获帧序号；同帧多个块共享此值，不能用它直接过滤该帧后续块。
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 FrameSequence = 0u;

	// 服务器模拟步编号；ServerTimeSeconds 是样本时间，客户端收包时间不是样本生成时间。
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint32 ServerSimTick = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	float ServerTimeSeconds = 0.0f;

	// 块号从 0 开始，必须小于 ChunkCount；并不要求客户端等待全部块到齐。
	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint16 ChunkIndex = 0u;

	UPROPERTY(VisibleAnywhere, Category = "Commander|Network")
	uint16 ChunkCount = 1u;

	// 本块公共世界坐标锚点；每个压缩样本只传相对该锚点的位置。
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
