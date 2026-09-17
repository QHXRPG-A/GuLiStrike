// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Network/GuLiCommanderTypes.h"
#include "Commander/Network/GuLiCommanderPoseMetrics.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
	bool IsFiniteVector(const FVector& Vector)
	{
		return FMath::IsFinite(Vector.X) && FMath::IsFinite(Vector.Y) && FMath::IsFinite(Vector.Z);
	}

	bool IsValidSelectionPreset(const EGuLiSelectionRadiusPreset Preset)
	{
		switch (Preset)
		{
		case EGuLiSelectionRadiusPreset::Small:
		case EGuLiSelectionRadiusPreset::Medium:
		case EGuLiSelectionRadiusPreset::Large:
			return true;
		default:
			return false;
		}
	}

	bool IsValidSelectionModifier(const EGuLiSelectionModifier Modifier)
	{
		switch (Modifier)
		{
		case EGuLiSelectionModifier::Replace:
		case EGuLiSelectionModifier::Toggle:
		case EGuLiSelectionModifier::Clear:
		case EGuLiSelectionModifier::Add:
			return true;
		default:
			return false;
		}
	}

	bool IsUnitSelectionRay(const FVector& Ray)
	{
		return IsFiniteVector(Ray) && FMath::IsNearlyEqual(Ray.SizeSquared(), 1.0, 0.002);
	}

	bool IsValidSelectionBox(const FGuLiSelectionRequest& Request)
	{
		const FVector Rays[] = {
			Request.BoxTopLeftRay, Request.BoxTopRightRay,
			Request.BoxBottomRightRay, Request.BoxBottomLeftRay
		};
		const FVector CenterRay = (Rays[0] + Rays[1] + Rays[2] + Rays[3]).GetSafeNormal();
		if (CenterRay.IsNearlyZero())
		{
			return false;
		}
		double WindingSign = 0.0;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (!IsUnitSelectionRay(Rays[Index]) || FVector::DotProduct(Rays[Index], CenterRay) <= 0.0)
			{
				return false;
			}
			const FVector EdgeNormal = FVector::CrossProduct(Rays[Index], Rays[(Index + 1) % 4]);
			const double Side = FVector::DotProduct(EdgeNormal, CenterRay);
			if (FMath::Abs(Side) < 1.e-10 || (Index > 0 && Side * WindingSign <= 0.0))
			{
				return false;
			}
			WindingSign = Side;
		}
		return true;
	}

	bool IsValidLifeState(const EGuLiSoldierLifeState State)
	{
		return State == EGuLiSoldierLifeState::Alive
			|| State == EGuLiSoldierLifeState::Destroyed;
	}

	bool IsValidPoseState(const EGuLiSoldierPoseState State)
	{
		switch (State)
		{
		case EGuLiSoldierPoseState::Idle:
		case EGuLiSoldierPoseState::Moving:
		case EGuLiSoldierPoseState::Destroyed:
			return true;
		default:
			return false;
		}
	}

	bool IsValidCommandKind(const EGuLiCommandKind Kind)
	{
		return Kind == EGuLiCommandKind::Selection || Kind == EGuLiCommandKind::Move;
	}
}

// 双端按同样顺序读写：SerializeIntPacked 压缩整数；return true 表示走自定义序列化，bOutSuccess 才报告数据成功。
bool FGuLiSoldierId::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	(void)Map;
	Ar.SerializeIntPacked(Value);
	bOutSuccess = !Ar.IsError();
	return true;
}

// 控制组 ID 同样采用压缩整数；结构序列化只传数值，不验证该组是否属于调用玩家。
bool FGuLiControlCohortId::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	(void)Map;
	Ar.SerializeIntPacked(Value);
	bOutSuccess = !Ar.IsError();
	return true;
}

bool GuLiCommanderProtocol::IsPlayableTeam(const EGuLiTeam Team)
{
	return Team == EGuLiTeam::Red || Team == EGuLiTeam::Blue;
}

float GuLiCommanderProtocol::GetSelectionRadiusCentimeters(const EGuLiSelectionRadiusPreset Preset)
{
	switch (Preset)
	{
	case EGuLiSelectionRadiusPreset::Small:
		return 8000.0f;
	case EGuLiSelectionRadiusPreset::Medium:
		return 20000.0f;
	case EGuLiSelectionRadiusPreset::Large:
		return 45000.0f;
	default:
		return 0.0f;
	}
}

// 先拒绝非有限数，再把角度归一到一周，量化后按 16 位回绕。
uint16 GuLiCommanderProtocol::QuantizeYawDegrees(const float YawDegrees)
{
	if (!FMath::IsFinite(YawDegrees))
	{
		return 0u;
	}

	const float NormalizedYaw = FRotator::ClampAxis(YawDegrees);
	const uint32 Quantized = FMath::RoundToInt(NormalizedYaw * (65536.0f / 360.0f));
	return static_cast<uint16>(Quantized & 0xffffu);
}

float GuLiCommanderProtocol::DequantizeYawDegrees(const uint16 QuantizedYaw)
{
	return static_cast<float>(QuantizedYaw) * (360.0f / 65536.0f);
}

// 每 10 cm 一个量化单位，四舍五入后钳制 int16；空间分块应提前避免位置超界而不是依赖截断。
int16 GuLiCommanderProtocol::QuantizeCentimetersToDecimeters(const float Centimeters)
{
	if (!FMath::IsFinite(Centimeters))
	{
		return 0;
	}

	const int32 RoundedDecimeters = FMath::RoundToInt(Centimeters / GULI_POSE_QUANTIZATION_CENTIMETERS);
	return static_cast<int16>(FMath::Clamp(
		RoundedDecimeters,
		static_cast<int32>(MIN_int16),
		static_cast<int32>(MAX_int16)));
}

float GuLiCommanderProtocol::DequantizeDecimetersToCentimeters(const int16 Decimeters)
{
	return static_cast<float>(Decimeters) * GULI_POSE_QUANTIZATION_CENTIMETERS;
}

// 验证各选择操作使用的几何；权限、版本与点选种子仍由服务器独立判定。
bool FGuLiSelectionRequest::IsWellFormed() const
{
	if (ClientRequestId == 0u || !IsFiniteVector(Center)
		|| !IsValidSelectionPreset(RadiusPreset) || !IsValidSelectionModifier(Modifier))
	{
		return false;
	}
	if (Kind != EGuLiSelectionKind::Radius && Kind != EGuLiSelectionKind::Point
		&& Kind != EGuLiSelectionKind::Box && Kind != EGuLiSelectionKind::SameType)
	{
		return false;
	}
	if (Modifier == EGuLiSelectionModifier::Clear || Kind == EGuLiSelectionKind::Radius)
	{
		return true;
	}
	if (!IsFiniteVector(RayOrigin) || FVector(RayOrigin).GetAbsMax() > 2000000.0)
	{
		return false;
	}
	if (Kind == EGuLiSelectionKind::Box)
	{
		return IsValidSelectionBox(*this);
	}
	return SeedSoldierId.IsValid() != SeedActorId.IsValid()
		&& IsUnitSelectionRay(RayDirection)
		&& FMath::IsFinite(PickHalfAngleRadians)
		&& PickHalfAngleRadians >= 0.0001f && PickHalfAngleRadians <= 0.05f;
}

bool FGuLiMoveRequest::IsWellFormed() const
{
	const bool bTargetKindValid = MiningOrderType == EGuLiMiningOrderType::Move
		|| MiningOrderType == EGuLiMiningOrderType::MineCluster
		|| MiningOrderType == EGuLiMiningOrderType::ReturnToFactory;
	return ClientCommandId != 0u
		&& SelectionRevision != 0u
		&& IsFiniteVector(Target)
		&& bTargetKindValid
		&& (TargetTerritoryId.IsNone() || MiningOrderType == EGuLiMiningOrderType::Move)
		&& ((MiningOrderType == EGuLiMiningOrderType::MineCluster) == (TargetClusterId != 0u));
}

bool FGuLiControlCohortDescriptor::IsValid() const
{
	return CohortId.IsValid() && !MemberIds.IsEmpty();
}

bool FGuLiControlCohortDescriptor::Contains(const FGuLiSoldierId SoldierId) const
{
	return SoldierId.IsValid() && MemberIds.Contains(SoldierId);
}

// 输入整理会删除无效/重复成员并限制数量；这是数据约束，不是网络授权或反作弊验证。
void FGuLiControlCohortDescriptor::Sanitize()
{
	TSet<FGuLiSoldierId> SeenSoldiers;
	for (int32 Index = 0; Index < MemberIds.Num();)
	{
		const FGuLiSoldierId SoldierId = MemberIds[Index];
		if (!SoldierId.IsValid() || SeenSoldiers.Contains(SoldierId))
		{
			MemberIds.RemoveAt(Index, 1, EAllowShrinking::No);
			continue;
		}

		SeenSoldiers.Add(SoldierId);
		++Index;
	}

	if (MemberIds.Num() > static_cast<int32>(GULI_CONTROL_COHORT_TARGET_SIZE))
	{
		MemberIds.SetNum(static_cast<int32>(GULI_CONTROL_COHORT_TARGET_SIZE), EAllowShrinking::No);
	}

	AliveCount = FMath::Min<uint8>(AliveCount, static_cast<uint8>(MemberIds.Num()));
	if (AliveCount == 0u)
	{
		ActiveOrderId = 0u;
	}
}

// 发送前/接收后整理控制组；数量先按有界整数编码，再逐项编码成员，读写顺序必须一致。
bool FGuLiControlCohortDescriptor::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	if (Ar.IsSaving())
	{
		Sanitize();
	}

	bOutSuccess = true;
	bool bFieldSuccess = true;
	CohortId.NetSerialize(Ar, Map, bFieldSuccess);
	bOutSuccess = bOutSuccess && bFieldSuccess;

	uint32 MemberCount = Ar.IsSaving() ? static_cast<uint32>(MemberIds.Num()) : 0u;
	Ar.SerializeInt(MemberCount, GULI_CONTROL_COHORT_TARGET_SIZE + 1u);
	if (Ar.IsLoading())
	{
		MemberIds.SetNum(static_cast<int32>(MemberCount));
	}

	for (FGuLiSoldierId& SoldierId : MemberIds)
	{
		bFieldSuccess = true;
		SoldierId.NetSerialize(Ar, Map, bFieldSuccess);
		bOutSuccess = bOutSuccess && bFieldSuccess;
	}

	uint32 SerializedAliveCount = Ar.IsSaving() ? static_cast<uint32>(AliveCount) : 0u;
	Ar.SerializeInt(SerializedAliveCount, GULI_CONTROL_COHORT_TARGET_SIZE + 1u);
	if (Ar.IsLoading())
	{
		AliveCount = static_cast<uint8>(SerializedAliveCount);
	}
	Ar.SerializeIntPacked(ActiveOrderId);

	if (Ar.IsLoading())
	{
		Sanitize();
	}
	bOutSuccess = bOutSuccess && !Ar.IsError() && IsValid();
	return true;
}

// 除组内去重，还剔除跨组重复士兵和空组，保证一个选择内每名士兵至多出现一次。
void FGuLiCommanderSelectionState::Sanitize()
{
	TSet<FGuLiControlCohortId> SeenCohorts;
	TSet<FGuLiSoldierId> SeenSoldiers;
	for (int32 CohortIndex = 0; CohortIndex < Cohorts.Num();)
	{
		FGuLiControlCohortDescriptor& Cohort = Cohorts[CohortIndex];
		Cohort.Sanitize();
		if (!Cohort.CohortId.IsValid() || SeenCohorts.Contains(Cohort.CohortId))
		{
			Cohorts.RemoveAt(CohortIndex, 1, EAllowShrinking::No);
			continue;
		}

		for (int32 MemberIndex = 0; MemberIndex < Cohort.MemberIds.Num();)
		{
			const FGuLiSoldierId SoldierId = Cohort.MemberIds[MemberIndex];
			if (SeenSoldiers.Contains(SoldierId))
			{
				Cohort.MemberIds.RemoveAt(MemberIndex, 1, EAllowShrinking::No);
				continue;
			}

			SeenSoldiers.Add(SoldierId);
			++MemberIndex;
		}

		Cohort.AliveCount = FMath::Min<uint8>(Cohort.AliveCount, static_cast<uint8>(Cohort.MemberIds.Num()));
		if (Cohort.MemberIds.IsEmpty())
		{
			Cohorts.RemoveAt(CohortIndex, 1, EAllowShrinking::No);
			continue;
		}

		SeenCohorts.Add(Cohort.CohortId);
		++CohortIndex;
	}

	if (Cohorts.Num() > static_cast<int32>(GULI_MAX_CONTROL_COHORTS))
	{
		Cohorts.SetNum(static_cast<int32>(GULI_MAX_CONTROL_COHORTS), EAllowShrinking::No);
	}

	TSet<FGuLiControllableActorId> SeenActors;
	for (int32 Index = 0; Index < ActorIds.Num();)
	{
		if (!ActorIds[Index].IsValid() || SeenActors.Contains(ActorIds[Index]))
		{
			ActorIds.RemoveAt(Index, 1, EAllowShrinking::No);
			continue;
		}
		SeenActors.Add(ActorIds[Index]);
		++Index;
	}
	ActorIds.Sort();
	if (ActorIds.Num() > static_cast<int32>(GULI_MAX_CONTROLLABLE_ACTOR_SELECTION))
	{
		ActorIds.SetNum(static_cast<int32>(GULI_MAX_CONTROLLABLE_ACTOR_SELECTION), EAllowShrinking::No);
	}
}

// 先 Mass 组、再稳定 ActorId，最后选择版本与请求号；这是协议 v8 的固定字段顺序。
bool FGuLiCommanderSelectionState::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	if (Ar.IsSaving())
	{
		Sanitize();
	}

	uint32 CohortCount = Ar.IsSaving() ? static_cast<uint32>(Cohorts.Num()) : 0u;
	Ar.SerializeInt(CohortCount, GULI_MAX_CONTROL_COHORTS + 1u);
	if (Ar.IsLoading())
	{
		Cohorts.SetNum(static_cast<int32>(CohortCount));
	}

	bOutSuccess = !Ar.IsError();
	for (FGuLiControlCohortDescriptor& Cohort : Cohorts)
	{
		bool bCohortSuccess = true;
		Cohort.NetSerialize(Ar, Map, bCohortSuccess);
		bOutSuccess = bOutSuccess && bCohortSuccess;
	}

	uint32 ActorCount = Ar.IsSaving() ? static_cast<uint32>(ActorIds.Num()) : 0u;
	Ar.SerializeInt(ActorCount, GULI_MAX_CONTROLLABLE_ACTOR_SELECTION + 1u);
	if (Ar.IsLoading())
	{
		ActorIds.SetNum(static_cast<int32>(ActorCount));
	}
	for (FGuLiControllableActorId& ActorId : ActorIds)
	{
		bool bActorSuccess = true;
		ActorId.NetSerialize(Ar, Map, bActorSuccess);
		bOutSuccess = bOutSuccess && bActorSuccess;
	}

	Ar.SerializeIntPacked(SelectionRevision);
	Ar.SerializeIntPacked(AcceptedClientRequestId);

	if (Ar.IsLoading())
	{
		Sanitize();
	}
	bOutSuccess = bOutSuccess && !Ar.IsError();
	return true;
}

// 这是业务是否接受的便捷判断，不是 RPC 投递确认，也不是移动完成事件。
bool FGuLiCommandAck::IsAccepted() const
{
	return Result == EGuLiCommandAckResult::Accepted
		|| Result == EGuLiCommandAckResult::PartiallyAccepted;
}

uint32 FGuLiCohortCommandAck::GetValidMemberMask() const
{
	const uint32 ClampedCount = FMath::Min<uint32>(MemberCount, GULI_CONTROL_COHORT_TARGET_SIZE);
	return ClampedCount == 0u ? 0u : ((1u << ClampedCount) - 1u);
}

bool FGuLiCohortCommandAck::IsMemberEligible(const uint8 MemberIndex) const
{
	return MemberIndex < MemberCount
		&& MemberIndex < GULI_CONTROL_COHORT_TARGET_SIZE
		&& (EligibleMemberMask & (1u << MemberIndex)) != 0u;
}

bool FGuLiCohortCommandAck::IsMemberAccepted(const uint8 MemberIndex) const
{
	return MemberIndex < MemberCount
		&& MemberIndex < GULI_CONTROL_COHORT_TARGET_SIZE
		&& (AcceptedMemberMask & (1u << MemberIndex)) != 0u;
}

uint8 FGuLiCohortCommandAck::GetAcceptedMemberCount() const
{
	return static_cast<uint8>(FPlatformMath::CountBits(AcceptedMemberMask & GetValidMemberMask()));
}

// Bit positions are meaningful only inside the frozen cohort membership. Never accept an ineligible member.
void FGuLiCohortCommandAck::Sanitize()
{
	MemberCount = FMath::Min<uint8>(
		MemberCount,
		static_cast<uint8>(GULI_CONTROL_COHORT_TARGET_SIZE));
	const uint32 ValidMask = GetValidMemberMask();
	EligibleMemberMask &= ValidMask;
	AcceptedMemberMask &= EligibleMemberMask;

	if (MemberCount == 0u)
	{
		EligibleMemberMask = 0u;
		AcceptedMemberMask = 0u;
		if (Result == EGuLiCommandAckResult::Accepted
			|| Result == EGuLiCommandAckResult::PartiallyAccepted)
		{
			Result = EGuLiCommandAckResult::NoSelection;
		}
		return;
	}

	if (AcceptedMemberMask != 0u)
	{
		Result = AcceptedMemberMask == EligibleMemberMask
			? EGuLiCommandAckResult::Accepted
			: EGuLiCommandAckResult::PartiallyAccepted;
	}
	else if (EligibleMemberMask != 0u)
	{
		Result = EGuLiCommandAckResult::PathFailed;
	}
	else
	{
		Result = EGuLiCommandAckResult::NoSelection;
	}
}

// 整理种类与控制组，并让 v6 移动顶层结果、批次号和逐兵掩码保持同一事实。
// 权限、寻路和成员资格仍只能由服务器 Authority 决定。
void FGuLiCommandAck::Sanitize()
{
	if (!IsValidCommandKind(CommandKind))
	{
		CommandKind = EGuLiCommandKind::None;
		ClientCommandId = 0u;
		BatchOrderId = 0u;
		CohortResults.Reset();
		if (Result == EGuLiCommandAckResult::Accepted
			|| Result == EGuLiCommandAckResult::PartiallyAccepted)
		{
			Result = EGuLiCommandAckResult::InvalidRequest;
		}
		return;
	}

	// Explicit engineer relocation has per-Actor outcomes and never creates a Mass batch.
	if (CommandKind == EGuLiCommandKind::Move && !EngineeringResults.IsEmpty())
	{
		BatchOrderId = 0; CohortResults.Reset();
		return;
	}

	TSet<FGuLiControlCohortId> SeenCohorts;
	for (int32 Index = 0; Index < CohortResults.Num();)
	{
		FGuLiCohortCommandAck& CohortResult = CohortResults[Index];
		CohortResult.Sanitize();
		const FGuLiControlCohortId CohortId = CohortResult.CohortId;
		if (!CohortId.IsValid() || SeenCohorts.Contains(CohortId))
		{
			CohortResults.RemoveAt(Index, 1, EAllowShrinking::No);
			continue;
		}

		SeenCohorts.Add(CohortId);
		++Index;
	}

	if (CohortResults.Num() > static_cast<int32>(GULI_MAX_CONTROL_COHORTS))
	{
		CohortResults.SetNum(static_cast<int32>(GULI_MAX_CONTROL_COHORTS), EAllowShrinking::No);
	}

	// A v6 planning result is a single coherent contract: accepted bits require a batch,
	// and the top-level result is derived from the sanitized per-member masks. Transport,
	// permission and lifecycle rejections keep their explicit reason even when an asynchronous
	// planner already attached frozen cohort diagnostics.
	if (CommandKind != EGuLiCommandKind::Move)
	{
		return;
	}
	const bool bDerivePlanningResultFromMasks =
		Result == EGuLiCommandAckResult::Accepted
		|| Result == EGuLiCommandAckResult::PartiallyAccepted
		|| Result == EGuLiCommandAckResult::PathFailed
		|| Result == EGuLiCommandAckResult::NoSelection;
	if (!bDerivePlanningResultFromMasks)
	{
		BatchOrderId = 0u;
		for (FGuLiCohortCommandAck& CohortResult : CohortResults)
		{
			CohortResult.AcceptedMemberMask = 0u;
			CohortResult.Sanitize();
		}
		return;
	}
	if (CohortResults.IsEmpty())
	{
		BatchOrderId = 0u;
		if (Result == EGuLiCommandAckResult::Accepted
			|| Result == EGuLiCommandAckResult::PartiallyAccepted)
		{
			Result = EGuLiCommandAckResult::InvalidRequest;
		}
		return;
	}
	uint32 EligibleMemberCount = 0u;
	uint32 AcceptedMemberCount = 0u;
	for (const FGuLiCohortCommandAck& CohortResult : CohortResults)
	{
		EligibleMemberCount += FPlatformMath::CountBits(CohortResult.EligibleMemberMask);
		AcceptedMemberCount += FPlatformMath::CountBits(CohortResult.AcceptedMemberMask);
	}
	if (BatchOrderId == 0u && AcceptedMemberCount > 0u)
	{
		AcceptedMemberCount = 0u;
		for (FGuLiCohortCommandAck& CohortResult : CohortResults)
		{
			CohortResult.AcceptedMemberMask = 0u;
			CohortResult.Sanitize();
		}
	}
	if (AcceptedMemberCount == 0u)
	{
		BatchOrderId = 0u;
		Result = EligibleMemberCount > 0u
			? EGuLiCommandAckResult::PathFailed
			: EGuLiCommandAckResult::NoSelection;
		return;
	}
	Result = AcceptedMemberCount == EligibleMemberCount
		? EGuLiCommandAckResult::Accepted
		: EGuLiCommandAckResult::PartiallyAccepted;
}

bool FGuLiSoldierStateItem::IsAlive() const
{
	return LifeState == EGuLiSoldierLifeState::Alive && FMath::IsFinite(Health) && Health > 0.0f;
}

// 生命是绝对浮点值；非法最大值回退为 1，非法健康值不复活单位。
void FGuLiSoldierStateItem::Sanitize()
{
	if (DisplacementLocation.ContainsNaN() || !FMath::IsFinite(DisplacementSimulationTime) || !FMath::IsFinite(DisplacementYaw))
	{ DisplacementFrameFloor = 0; DisplacementLocation = FVector::ZeroVector; DisplacementSimulationTime = 0; DisplacementYaw = 0; }
	UnitTypeId = FMath::Max<uint16>(UnitTypeId, GULI_DEFAULT_SOLDIER_UNIT_TYPE_ID);
	if (!GuLiCommanderProtocol::IsPlayableTeam(Team))
	{
		Team = EGuLiTeam::Unassigned;
	}
	if (!IsValidLifeState(LifeState))
	{
		LifeState = EGuLiSoldierLifeState::Alive;
	}
	MaxHealth = FMath::IsFinite(MaxHealth) && MaxHealth > 0.0f
		? FMath::Min(MaxHealth, 1000000000.0f) : 1.0f;
	Health = FMath::IsFinite(Health) ? FMath::Clamp(Health, 0.0f, MaxHealth) : 0.0f;

	if (Health <= 0.0f || LifeState == EGuLiSoldierLifeState::Destroyed)
	{
		Health = 0.0f;
		LifeState = EGuLiSoldierLifeState::Destroyed;
		ActiveOrderId = 0u;
	}
}

void FGuLiSoldierStateFastArray::Sanitize()
{
	TSet<FGuLiSoldierId> SeenSoldiers;
	for (int32 Index = 0; Index < Items.Num();)
	{
		FGuLiSoldierStateItem& Item = Items[Index];
		Item.Sanitize();
		if (!Item.SoldierId.IsValid() || SeenSoldiers.Contains(Item.SoldierId))
		{
			Items.RemoveAt(Index, 1, EAllowShrinking::No);
			continue;
		}

		SeenSoldiers.Add(Item.SoldierId);
		++Index;
	}
}

const FGuLiSoldierStateItem* FGuLiSoldierStateFastArray::Find(const FGuLiSoldierId SoldierId) const
{
	return SoldierId.IsValid()
		? Items.FindByPredicate([SoldierId](const FGuLiSoldierStateItem& Item)
		{
			return Item.SoldierId == SoldierId;
		})
		: nullptr;
}

FGuLiSoldierStateItem* FGuLiSoldierStateFastArray::FindMutable(const FGuLiSoldierId SoldierId)
{
	return SoldierId.IsValid()
		? Items.FindByPredicate([SoldierId](const FGuLiSoldierStateItem& Item)
		{
			return Item.SoldierId == SoldierId;
		})
		: nullptr;
}

bool FGuLiMoveEndpointItem::IsValid() const
{
	return SoldierId.IsValid() && ActiveOrderId != 0u && Revision != 0u
		&& IsFiniteVector(CommandStart) && IsFiniteVector(FinalDestination);
}

void FGuLiMoveEndpointItem::Sanitize()
{
	if (!SoldierId.IsValid() || ActiveOrderId == 0u
		|| !IsFiniteVector(CommandStart) || !IsFiniteVector(FinalDestination))
	{
		SoldierId.Reset();
		ActiveOrderId = 0u;
		CommandStart = FVector::ZeroVector;
		FinalDestination = FVector::ZeroVector;
		Revision = 0u;
		return;
	}
	if (Revision == 0u)
	{
		Revision = 1u;
	}
}

void FGuLiMoveEndpointFastArray::Sanitize()
{
	TSet<FGuLiSoldierId> SeenSoldiers;
	for (int32 Index = 0; Index < Items.Num();)
	{
		FGuLiMoveEndpointItem& Item = Items[Index];
		Item.Sanitize();
		if (!Item.IsValid() || SeenSoldiers.Contains(Item.SoldierId))
		{
			Items.RemoveAt(Index, 1, EAllowShrinking::No);
			continue;
		}
		SeenSoldiers.Add(Item.SoldierId);
		++Index;
	}
	const int32 MaximumEndpoints = static_cast<int32>(
		GULI_MAX_CONTROL_COHORTS * GULI_CONTROL_COHORT_TARGET_SIZE);
	if (Items.Num() > MaximumEndpoints)
	{
		Items.SetNum(MaximumEndpoints, EAllowShrinking::No);
	}
}

const FGuLiMoveEndpointItem* FGuLiMoveEndpointFastArray::Find(const FGuLiSoldierId SoldierId) const
{
	return SoldierId.IsValid()
		? Items.FindByPredicate([SoldierId](const FGuLiMoveEndpointItem& Item)
		{
			return Item.SoldierId == SoldierId;
		})
		: nullptr;
}

FGuLiMoveEndpointItem* FGuLiMoveEndpointFastArray::FindMutable(const FGuLiSoldierId SoldierId)
{
	return SoldierId.IsValid()
		? Items.FindByPredicate([SoldierId](const FGuLiMoveEndpointItem& Item)
		{
			return Item.SoldierId == SoldierId;
		})
		: nullptr;
}

bool FGuLiMoveEndpointFastArray::Upsert(
	const FGuLiSoldierId SoldierId,
	const uint32 ActiveOrderId,
	const FVector& CommandStart,
	const FVector& FinalDestination)
{
	FGuLiMoveEndpointItem Candidate;
	Candidate.SoldierId = SoldierId;
	Candidate.ActiveOrderId = ActiveOrderId;
	Candidate.CommandStart = CommandStart;
	Candidate.FinalDestination = FinalDestination;
	Candidate.Revision = 1u;
	Candidate.Sanitize();
	if (!Candidate.IsValid())
	{
		return false;
	}

	if (FGuLiMoveEndpointItem* Existing = FindMutable(SoldierId))
	{
		if (Existing->ActiveOrderId == Candidate.ActiveOrderId
			&& FVector(Existing->CommandStart).Equals(CommandStart, 0.01)
			&& FVector(Existing->FinalDestination).Equals(FinalDestination, 0.01))
		{
			return false;
		}
		Candidate.Revision = Existing->Revision + 1u;
		if (Candidate.Revision == 0u)
		{
			Candidate.Revision = 1u;
		}
		Existing->ActiveOrderId = Candidate.ActiveOrderId;
		Existing->CommandStart = Candidate.CommandStart;
		Existing->FinalDestination = Candidate.FinalDestination;
		Existing->Revision = Candidate.Revision;
		MarkItemDirty(*Existing);
		return true;
	}
	const int32 MaximumEndpoints = static_cast<int32>(
		GULI_MAX_CONTROL_COHORTS * GULI_CONTROL_COHORT_TARGET_SIZE);
	if (Items.Num() >= MaximumEndpoints)
	{
		return false;
	}

	FGuLiMoveEndpointItem& Added = Items.Add_GetRef(Candidate);
	MarkItemDirty(Added);
	return true;
}

bool FGuLiMoveEndpointFastArray::Remove(const FGuLiSoldierId SoldierId)
{
	const int32 Index = Items.IndexOfByPredicate([SoldierId](const FGuLiMoveEndpointItem& Item)
	{
		return Item.SoldierId == SoldierId;
	});
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Items.RemoveAtSwap(Index, 1, EAllowShrinking::No);
	MarkArrayDirty();
	return true;
}

int32 FGuLiMoveEndpointFastArray::ReplaceWith(const TConstArrayView<FGuLiMoveEndpointItem> Endpoints)
{
	const int32 MaximumEndpoints = static_cast<int32>(
		GULI_MAX_CONTROL_COHORTS * GULI_CONTROL_COHORT_TARGET_SIZE);
	TMap<FGuLiSoldierId, FGuLiMoveEndpointItem> SanitizedBySoldier;
	SanitizedBySoldier.Reserve(FMath::Min(Endpoints.Num(), MaximumEndpoints));
	for (const FGuLiMoveEndpointItem& Endpoint : Endpoints)
	{
		FGuLiMoveEndpointItem Sanitized = Endpoint;
		Sanitized.Sanitize();
		if (Sanitized.IsValid()
			&& SanitizedBySoldier.Num() < MaximumEndpoints
			&& !SanitizedBySoldier.Contains(Sanitized.SoldierId))
		{
			SanitizedBySoldier.Add(Sanitized.SoldierId, Sanitized);
		}
	}

	int32 ChangedCount = 0;
	for (int32 Index = Items.Num() - 1; Index >= 0; --Index)
	{
		if (!SanitizedBySoldier.Contains(Items[Index].SoldierId))
		{
			Items.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			++ChangedCount;
		}
	}
	if (ChangedCount > 0)
	{
		MarkArrayDirty();
	}
	TMap<FGuLiSoldierId, int32> ExistingIndexBySoldier;
	ExistingIndexBySoldier.Reserve(Items.Num());
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		ExistingIndexBySoldier.Add(Items[Index].SoldierId, Index);
	}

	TArray<FGuLiSoldierId> SortedIds;
	SanitizedBySoldier.GetKeys(SortedIds);
	SortedIds.Sort();
	for (const FGuLiSoldierId SoldierId : SortedIds)
	{
		const FGuLiMoveEndpointItem& Incoming = SanitizedBySoldier.FindChecked(SoldierId);
		if (const int32* ExistingIndex = ExistingIndexBySoldier.Find(SoldierId))
		{
			FGuLiMoveEndpointItem& Existing = Items[*ExistingIndex];
			if (Existing.ActiveOrderId == Incoming.ActiveOrderId
				&& FVector(Existing.CommandStart).Equals(FVector(Incoming.CommandStart), 0.01)
				&& FVector(Existing.FinalDestination).Equals(
					FVector(Incoming.FinalDestination), 0.01))
			{
				continue;
			}
			Existing.ActiveOrderId = Incoming.ActiveOrderId;
			Existing.CommandStart = Incoming.CommandStart;
			Existing.FinalDestination = Incoming.FinalDestination;
			++Existing.Revision;
			if (Existing.Revision == 0u)
			{
				Existing.Revision = 1u;
			}
			MarkItemDirty(Existing);
		}
		else
		{
			FGuLiMoveEndpointItem Added = Incoming;
			Added.Revision = 1u;
			FGuLiMoveEndpointItem& AddedItem = Items.Add_GetRef(MoveTemp(Added));
			MarkItemDirty(AddedItem);
		}
		++ChangedCount;
	}
	return ChangedCount;
}

bool FGuLiMoveEndpointFastArray::ResetEndpoints()
{
	if (Items.IsEmpty())
	{
		return false;
	}
	Items.Reset();
	MarkArrayDirty();
	return true;
}

void FGuLiCompressedSoldierPose::SetRelativeLocationCentimeters(const FVector& RelativeLocation)
{
	RelativeXDecimeters = GuLiCommanderProtocol::QuantizeCentimetersToDecimeters(static_cast<float>(RelativeLocation.X));
	RelativeYDecimeters = GuLiCommanderProtocol::QuantizeCentimetersToDecimeters(static_cast<float>(RelativeLocation.Y));
	RelativeZDecimeters = GuLiCommanderProtocol::QuantizeCentimetersToDecimeters(static_cast<float>(RelativeLocation.Z));
}

FVector FGuLiCompressedSoldierPose::GetRelativeLocationCentimeters() const
{
	return FVector(
		GuLiCommanderProtocol::DequantizeDecimetersToCentimeters(RelativeXDecimeters),
		GuLiCommanderProtocol::DequantizeDecimetersToCentimeters(RelativeYDecimeters),
		GuLiCommanderProtocol::DequantizeDecimetersToCentimeters(RelativeZDecimeters));
}

void FGuLiCompressedSoldierPose::SetVelocityCentimetersPerSecond(const FVector& Velocity)
{
	VelocityXDecimetersPerSecond = GuLiCommanderProtocol::QuantizeCentimetersToDecimeters(static_cast<float>(Velocity.X));
	VelocityYDecimetersPerSecond = GuLiCommanderProtocol::QuantizeCentimetersToDecimeters(static_cast<float>(Velocity.Y));
	VelocityZDecimetersPerSecond = GuLiCommanderProtocol::QuantizeCentimetersToDecimeters(static_cast<float>(Velocity.Z));
}

FVector FGuLiCompressedSoldierPose::GetVelocityCentimetersPerSecond() const
{
	return FVector(
		GuLiCommanderProtocol::DequantizeDecimetersToCentimeters(VelocityXDecimetersPerSecond),
		GuLiCommanderProtocol::DequantizeDecimetersToCentimeters(VelocityYDecimetersPerSecond),
		GuLiCommanderProtocol::DequantizeDecimetersToCentimeters(VelocityZDecimetersPerSecond));
}

bool FGuLiCompressedSoldierPose::IsTeleport() const
{
	return (Flags & GULI_SOLDIER_POSE_FLAG_TELEPORT) != 0u;
}

void FGuLiCompressedSoldierPose::Sanitize()
{
	Flags &= GULI_VALID_SOLDIER_POSE_FLAGS;
	if (!IsValidPoseState(State))
	{
		State = EGuLiSoldierPoseState::Idle;
	}
	if (State == EGuLiSoldierPoseState::Destroyed)
	{
		VelocityXDecimetersPerSecond = 0;
		VelocityYDecimetersPerSecond = 0;
		VelocityZDecimetersPerSecond = 0;
		ActiveOrderId = 0u;
	}
}

// 位置/速度为 int16，朝向为 uint16，命令号压缩；状态占 2 bit、标志占 1 bit。
bool FGuLiCompressedSoldierPose::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	if (Ar.IsSaving())
	{
		Sanitize();
	}

	bool bIdSuccess = true;
	SoldierId.NetSerialize(Ar, Map, bIdSuccess);
	Ar << RelativeXDecimeters;
	Ar << RelativeYDecimeters;
	Ar << RelativeZDecimeters;
	Ar << VelocityXDecimetersPerSecond;
	Ar << VelocityYDecimetersPerSecond;
	Ar << VelocityZDecimetersPerSecond;
	Ar << FacingYaw;
	Ar.SerializeIntPacked(ActiveOrderId);

	uint8 StateValue = static_cast<uint8>(State);
	Ar.SerializeBits(&StateValue, 2u);
	if (Ar.IsLoading())
	{
		State = static_cast<EGuLiSoldierPoseState>(StateValue);
	}
	Ar.SerializeBits(&Flags, 1u);

	if (Ar.IsLoading())
	{
		Sanitize();
	}
	bOutSuccess = bIdSuccess && SoldierId.IsValid() && !Ar.IsError();
	return true;
}

// 整理会重置协议字段并裁剪数量，因此不能先整理再据其验证原始协议版本。
void FGuLiSoldierPoseChunk::Sanitize()
{
	ProtocolVersion = GULI_COMMANDER_PROTOCOL_VERSION;
	if (!FMath::IsFinite(ServerTimeSeconds) || ServerTimeSeconds < 0.0f)
	{
		ServerTimeSeconds = 0.0f;
	}

	ChunkCount = static_cast<uint16>(FMath::Clamp<uint32>(ChunkCount, 1u, GULI_MAX_POSE_CHUNKS_PER_FRAME));
	ChunkIndex = FMath::Min<uint16>(ChunkIndex, static_cast<uint16>(ChunkCount - 1u));

	TSet<FGuLiSoldierId> SeenSoldiers;
	for (int32 Index = 0; Index < Samples.Num();)
	{
		FGuLiCompressedSoldierPose& Sample = Samples[Index];
		Sample.Sanitize();
		if (!Sample.SoldierId.IsValid() || SeenSoldiers.Contains(Sample.SoldierId))
		{
			Samples.RemoveAt(Index, 1, EAllowShrinking::No);
			continue;
		}

		SeenSoldiers.Add(Sample.SoldierId);
		++Index;
	}

	if (Samples.Num() > static_cast<int32>(GULI_MAX_POSE_SAMPLES_PER_CHUNK))
	{
		Samples.SetNum(static_cast<int32>(GULI_MAX_POSE_SAMPLES_PER_CHUNK), EAllowShrinking::No);
	}
}

bool FGuLiSoldierPoseChunk::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiPose_Wire);
	TOptional<GuLiCommanderPoseMetrics::FScope> ReadMeasure;
	if (Ar.IsLoading()) ReadMeasure.Emplace(GuLiCommanderPoseMetrics::EScope::WireRead);
	if (Ar.IsSaving())
	{
		Sanitize();
	}

	Ar << ProtocolVersion;
	Ar.SerializeIntPacked(AuthorityEpoch);
	Ar.SerializeIntPacked(FrameSequence);
	Ar.SerializeIntPacked(ServerSimTick);
	Ar << ServerTimeSeconds;

	uint32 SerializedChunkCount = Ar.IsSaving() ? static_cast<uint32>(ChunkCount) : 0u;
	Ar.SerializeInt(SerializedChunkCount, GULI_MAX_POSE_CHUNKS_PER_FRAME + 1u);
	if (Ar.IsLoading())
	{
		ChunkCount = static_cast<uint16>(SerializedChunkCount);
	}

	uint32 SerializedChunkIndex = Ar.IsSaving() ? static_cast<uint32>(ChunkIndex) : 0u;
	Ar.SerializeInt(SerializedChunkIndex, GULI_MAX_POSE_CHUNKS_PER_FRAME);
	if (Ar.IsLoading())
	{
		ChunkIndex = static_cast<uint16>(SerializedChunkIndex);
	}

	bOutSuccess = true;
	bool bFieldSuccess = true;
	Anchor.NetSerialize(Ar, Map, bFieldSuccess);
	bOutSuccess = bOutSuccess && bFieldSuccess;

	uint32 SampleCount = Ar.IsSaving() ? static_cast<uint32>(Samples.Num()) : 0u;
	Ar.SerializeInt(SampleCount, GULI_MAX_POSE_SAMPLES_PER_CHUNK + 1u);
	if (Ar.IsLoading())
	{
		Samples.SetNum(static_cast<int32>(SampleCount));
	}

	for (FGuLiCompressedSoldierPose& Sample : Samples)
	{
		bFieldSuccess = true;
		Sample.NetSerialize(Ar, Map, bFieldSuccess);
		bOutSuccess = bOutSuccess && bFieldSuccess;
	}

	// 在接收后 Sanitize 之前保存原始头部有效性，避免非法协议/战局/块号被整理后掩盖。
	const bool bHeaderValid = ProtocolVersion == GULI_COMMANDER_PROTOCOL_VERSION
		&& AuthorityEpoch != 0u
		&& FrameSequence != 0u
		&& ChunkCount > 0u
		&& ChunkCount <= GULI_MAX_POSE_CHUNKS_PER_FRAME
		&& ChunkIndex < ChunkCount;
	if (Ar.IsLoading())
	{
		Sanitize();
	}
	bOutSuccess = bOutSuccess && bHeaderValid && !Ar.IsError();
	return true;
}
