// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Network/GuLiCommanderTypes.h"

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
			return true;
		default:
			return false;
		}
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

bool FGuLiSoldierId::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	(void)Map;
	Ar.SerializeIntPacked(Value);
	bOutSuccess = !Ar.IsError();
	return true;
}

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

bool FGuLiSelectionRequest::IsWellFormed() const
{
	return ClientRequestId != 0u
		&& IsFiniteVector(Center)
		&& IsValidSelectionPreset(RadiusPreset)
		&& IsValidSelectionModifier(Modifier);
}

bool FGuLiMoveRequest::IsWellFormed() const
{
	return ClientCommandId != 0u
		&& SelectionRevision != 0u
		&& IsFiniteVector(Target);
}

bool FGuLiControlCohortDescriptor::IsValid() const
{
	return CohortId.IsValid() && !MemberIds.IsEmpty();
}

bool FGuLiControlCohortDescriptor::Contains(const FGuLiSoldierId SoldierId) const
{
	return SoldierId.IsValid() && MemberIds.Contains(SoldierId);
}

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
}

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

	Ar.SerializeIntPacked(SelectionRevision);
	Ar.SerializeIntPacked(AcceptedClientRequestId);

	if (Ar.IsLoading())
	{
		Sanitize();
	}
	bOutSuccess = bOutSuccess && !Ar.IsError();
	return true;
}

bool FGuLiCommandAck::IsAccepted() const
{
	return Result == EGuLiCommandAckResult::Accepted
		|| Result == EGuLiCommandAckResult::PartiallyAccepted;
}

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

	TSet<FGuLiControlCohortId> SeenCohorts;
	for (int32 Index = 0; Index < CohortResults.Num();)
	{
		const FGuLiControlCohortId CohortId = CohortResults[Index].CohortId;
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
}

bool FGuLiSoldierStateItem::IsAlive() const
{
	return LifeState == EGuLiSoldierLifeState::Alive && Health > 0u;
}

void FGuLiSoldierStateItem::Sanitize()
{
	if (!GuLiCommanderProtocol::IsPlayableTeam(Team))
	{
		Team = EGuLiTeam::Unassigned;
	}
	if (!IsValidLifeState(LifeState))
	{
		LifeState = EGuLiSoldierLifeState::Alive;
	}

	if (Health == 0u || LifeState == EGuLiSoldierLifeState::Destroyed)
	{
		Health = 0u;
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
