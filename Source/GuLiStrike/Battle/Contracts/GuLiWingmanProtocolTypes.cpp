// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"

#include "Serialization/MemoryWriter.h"

namespace
{
	void AddGuid(uint64& Hash, const FGuid& Guid)
	{
		GuLiShipAbilityHash::AddUInt32(Hash, Guid.A);
		GuLiShipAbilityHash::AddUInt32(Hash, Guid.B);
		GuLiShipAbilityHash::AddUInt32(Hash, Guid.C);
		GuLiShipAbilityHash::AddUInt32(Hash, Guid.D);
	}

	void AddGroup(uint64& Hash, const FGuLiWingmanGroupHandle& Group)
	{
		AddGuid(Hash, Group.ShipInstanceId);
		GuLiShipAbilityHash::AddUInt32(Hash, Group.ShipGeneration);
		GuLiShipAbilityHash::AddUInt32(Hash, Group.GroupGeneration);
	}

	void AddDouble(uint64& Hash, const double Value)
	{
		uint64 Bits = 0u;
		static_assert(sizeof(Bits) == sizeof(Value));
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		GuLiShipAbilityHash::AddUInt64(Hash, Bits);
	}

	bool IsStableGuidLess(const FGuid& Lhs, const FGuid& Rhs)
	{
		if (Lhs.A != Rhs.A) return Lhs.A < Rhs.A;
		if (Lhs.B != Rhs.B) return Lhs.B < Rhs.B;
		if (Lhs.C != Rhs.C) return Lhs.C < Rhs.C;
		return Lhs.D < Rhs.D;
	}

	bool IsStableWingmanLess(const FGuLiWingmanHandle& Lhs, const FGuLiWingmanHandle& Rhs)
	{
		if (Lhs.Flight.Group.ShipInstanceId != Rhs.Flight.Group.ShipInstanceId)
		{
			return IsStableGuidLess(
				Lhs.Flight.Group.ShipInstanceId,
				Rhs.Flight.Group.ShipInstanceId);
		}
		if (Lhs.Flight.Group.ShipGeneration != Rhs.Flight.Group.ShipGeneration)
		{
			return Lhs.Flight.Group.ShipGeneration < Rhs.Flight.Group.ShipGeneration;
		}
		if (Lhs.Flight.Group.GroupGeneration != Rhs.Flight.Group.GroupGeneration)
		{
			return Lhs.Flight.Group.GroupGeneration < Rhs.Flight.Group.GroupGeneration;
		}
		if (Lhs.Flight.FlightIndex != Rhs.Flight.FlightIndex)
		{
			return Lhs.Flight.FlightIndex < Rhs.Flight.FlightIndex;
		}
		if (Lhs.MemberIndex != Rhs.MemberIndex)
		{
			return Lhs.MemberIndex < Rhs.MemberIndex;
		}
		return Lhs.EntityGeneration < Rhs.EntityGeneration;
	}

	void AddAttackTarget(uint64& Hash, const FGuLiWingmanAttackTarget& Target)
	{
		AddGuid(Hash, Target.Target.AuthorityId);
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Target.Target.Kind));
		GuLiShipAbilityHash::AddUInt32(Hash, Target.Target.Generation);
		GuLiShipAbilityHash::AddUInt32(Hash, Target.Target.LocalId);
		GuLiShipAbilityHash::AddUInt32(Hash, Target.Revision);
		GuLiShipAbilityHash::AddUInt32(Hash, Target.bGround);
		GuLiShipAbilityHash::AddUInt32(Hash, Target.bSpecified);
		GuLiShipAbilityHash::AddFloat(Hash, Target.Radius);
		for (const double Value : {Target.Location.X, Target.Location.Y, Target.Location.Z, Target.ServerTime})
		{
			AddDouble(Hash, Value);
		}
	}

	bool HasMatchingAttackClassification(const FGuLiWingmanAttackTarget& Target)
	{
		switch (Target.Target.Kind)
		{
		case EGuLiTargetKind::CommanderSoldier:
			return Target.bGround;
		case EGuLiTargetKind::Ship:
		case EGuLiTargetKind::Wingman:
			return !Target.bGround;
		default:
			return false;
		}
	}

	void AddCandidateSample(uint64& Hash, const FGuLiWingmanCandidateSample& Sample)
	{
		AddGroup(Hash, Sample.Wingman.Flight.Group);
		GuLiShipAbilityHash::AddUInt32(Hash, Sample.Wingman.Flight.FlightIndex);
		GuLiShipAbilityHash::AddUInt32(Hash, Sample.Wingman.MemberIndex);
		GuLiShipAbilityHash::AddUInt32(Hash, Sample.Wingman.EntityGeneration);
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Sample.PositionCentimeters.X));
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Sample.PositionCentimeters.Y));
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Sample.PositionCentimeters.Z));
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Sample.VelocityCentimetersPerSecond.X));
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Sample.VelocityCentimetersPerSecond.Y));
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Sample.VelocityCentimetersPerSecond.Z));
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Sample.RotationCentiDegrees.X));
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Sample.RotationCentiDegrees.Y));
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Sample.RotationCentiDegrees.Z));
		GuLiShipAbilityHash::AddUInt32(Hash, Sample.FlightMode);
	}

	uint8 BuildMemberMask(const TArray<FGuLiWingmanCandidateSample>& Samples, const uint8 FlightIndex)
	{
		uint8 Mask = 0u;
		for (const FGuLiWingmanCandidateSample& Sample : Samples)
		{
			if (Sample.Wingman.Flight.FlightIndex != FlightIndex
				|| Sample.Wingman.MemberIndex >= GULI_WINGMAN_MEMBERS_PER_FLIGHT)
			{
				return 0u;
			}
			Mask |= static_cast<uint8>(1u << Sample.Wingman.MemberIndex);
		}
		return Mask;
	}

	bool SerializeTag(FArchive& Ar, FGameplayTag& Tag)
	{
		FString StableName = Ar.IsSaving() && Tag.IsValid() ? Tag.ToString() : FString();
		Ar << StableName;
		if (Ar.IsLoading())
		{
			Tag = StableName.IsEmpty()
				? FGameplayTag()
				: FGameplayTag::RequestGameplayTag(FName(*StableName), false);
		}
		return !Ar.IsError() && (StableName.IsEmpty() || Tag.IsValid());
	}

	bool MatchesGroup(const FGuLiWingmanGroupHandle& Group, const FGuLiGroupAbilityConfigSnapshot& Config)
	{
		return Group.ShipInstanceId == Config.ShipInstanceId
			&& Group.ShipGeneration == Config.ShipGeneration
			&& Group.GroupGeneration == Config.GroupGeneration;
	}
}

bool FGuLiWingmanGroupHandle::IsValid() const
{
	return ShipInstanceId.IsValid() && ShipGeneration != 0u && GroupGeneration != 0u;
}

bool FGuLiWingmanGroupHandle::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	(void)Map;
	Ar << ShipInstanceId;
	Ar.SerializeIntPacked(ShipGeneration);
	Ar.SerializeIntPacked(GroupGeneration);
	// NetSerialize reports transport integrity only. Several wire DTOs contain an explicitly
	// optional Group-bearing payload (for example a Bootstrap without a transfer baseline),
	// whose default all-zero sentinel must cross the wire intact. Semantic validity remains
	// fail-closed at each DTO's IsWellFormed/server gate rather than aborting the entire RPC.
	bOutSuccess = !Ar.IsError();
	return true;
}

bool FGuLiWingmanFlightHandle::IsValid() const
{
	return Group.IsValid() && FlightIndex < GULI_WINGMAN_FLIGHT_COUNT;
}

bool FGuLiWingmanFlightHandle::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bool bGroupSuccess = false;
	Group.NetSerialize(Ar, Map, bGroupSuccess);
	Ar << FlightIndex;
	bOutSuccess = bGroupSuccess && !Ar.IsError() && IsValid();
	return true;
}

bool FGuLiWingmanHandle::IsValid() const
{
	return Flight.IsValid() && MemberIndex < GULI_WINGMAN_MEMBERS_PER_FLIGHT && EntityGeneration != 0u;
}

uint8 FGuLiWingmanHandle::GetGroupMemberIndex() const
{
	return IsValid()
		? static_cast<uint8>(Flight.FlightIndex * GULI_WINGMAN_MEMBERS_PER_FLIGHT + MemberIndex)
		: MAX_uint8;
}

bool FGuLiWingmanHandle::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bool bFlightSuccess = false;
	Flight.NetSerialize(Ar, Map, bFlightSuccess);
	Ar << MemberIndex;
	Ar.SerializeIntPacked(EntityGeneration);
	bOutSuccess = bFlightSuccess && !Ar.IsError() && IsValid();
	return true;
}

bool FGuLiTargetHandle::IsValid() const
{
	if (Kind == EGuLiTargetKind::None || !AuthorityId.IsValid() || Generation == 0u)
	{
		return false;
	}
	return Kind == EGuLiTargetKind::Ship || LocalId != 0u;
}

bool FGuLiTargetHandle::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	(void)Map;
	uint8 KindValue = static_cast<uint8>(Kind);
	Ar << KindValue;
	if (Ar.IsLoading())
	{
		Kind = KindValue <= static_cast<uint8>(EGuLiTargetKind::Wingman)
			? static_cast<EGuLiTargetKind>(KindValue)
			: EGuLiTargetKind::None;
	}
	Ar << AuthorityId;
	Ar.SerializeIntPacked(Generation);
	Ar.SerializeIntPacked(LocalId);
	bOutSuccess = !Ar.IsError() && IsValid();
	return true;
}

bool FGuLiAcceptedStateRef::IsValid() const
{
	return MatchEpoch != 0u && GroupGeneration != 0u && AcceptedSequence != 0u && ClientSimTick != 0u;
}

bool FGuLiAcceptedStateRef::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	(void)Map;
	Ar.SerializeIntPacked(MatchEpoch);
	Ar.SerializeIntPacked(GroupGeneration);
	Ar.SerializeIntPacked(AcceptedSequence);
	Ar.SerializeIntPacked(ClientSimTick);
	bOutSuccess = !Ar.IsError() && IsValid();
	return true;
}

bool FGuLiCarrierSourceRef::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	(void)Map;
	Ar.SerializeIntPacked(CanonicalEpoch);
	Ar.SerializeIntPacked(MoveRevision);
	bOutSuccess = !Ar.IsError() && IsValid();
	return true;
}

bool FGuLiWingmanCandidateSample::IsWellFormed(const FGuLiWingmanGroupHandle& ExpectedGroup) const
{
	constexpr int32 MaximumVelocityCentimetersPerSecond = 200000;
	constexpr int32 MaximumRotationCentiDegrees = 36000;
	return Wingman.IsValid() && Wingman.Flight.Group == ExpectedGroup
		&& VelocityCentimetersPerSecond.GetAbsMax() <= MaximumVelocityCentimetersPerSecond
		&& RotationCentiDegrees.GetAbsMax() <= MaximumRotationCentiDegrees
		&& FlightMode <= static_cast<uint8>(EGuLiWingmanFlightMode::Stale);
}

bool FGuLiWingmanCandidateSample::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bool bHandleSuccess = false;
	Wingman.NetSerialize(Ar, Map, bHandleSuccess);
	Ar << PositionCentimeters.X << PositionCentimeters.Y << PositionCentimeters.Z;
	Ar << VelocityCentimetersPerSecond.X << VelocityCentimetersPerSecond.Y << VelocityCentimetersPerSecond.Z;
	Ar << RotationCentiDegrees.X << RotationCentiDegrees.Y << RotationCentiDegrees.Z;
	Ar << FlightMode;
	bOutSuccess = bHandleSuccess && !Ar.IsError();
	return true;
}

bool FGuLiWingmanCandidateTrailSample::IsWellFormed(
	const FGuLiWingmanGroupHandle& ExpectedGroup,
	const uint8 ExpectedFlightIndex,
	const uint8 ExpectedMemberMask) const
{
	if (ClientSimTick == 0u || !FMath::IsFinite(CaptureEstimatedServerTimeSeconds)
		|| CaptureEstimatedServerTimeSeconds < 0.0 || !CarrierSource.IsValid()
		|| ExpectedFlightIndex >= GULI_WINGMAN_FLIGHT_COUNT || ExpectedMemberMask == 0u
		|| Samples.IsEmpty() || Samples.Num() > GULI_WINGMAN_MEMBERS_PER_FLIGHT
		|| BuildMemberMask(Samples, ExpectedFlightIndex) != ExpectedMemberMask)
	{
		return false;
	}
	TSet<FGuLiWingmanHandle> Seen;
	for (const FGuLiWingmanCandidateSample& Sample : Samples)
	{
		if (!Sample.IsWellFormed(ExpectedGroup) || Seen.Contains(Sample.Wingman))
		{
			return false;
		}
		Seen.Add(Sample.Wingman);
	}
	return true;
}

bool FGuLiWingmanCandidateTrailSample::NetSerialize(
	FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar.SerializeIntPacked(ClientSimTick);
	Ar << CaptureEstimatedServerTimeSeconds;
	bool bFieldSuccess = false;
	CarrierSource.NetSerialize(Ar, Map, bFieldSuccess);
	bool bAllFieldsSucceeded = bFieldSuccess;
	uint32 SampleCount = Ar.IsSaving() ? static_cast<uint32>(Samples.Num()) : 0u;
	if (Ar.IsSaving() && SampleCount > GULI_WINGMAN_MEMBERS_PER_FLIGHT)
	{
		bAllFieldsSucceeded = false;
		SampleCount = GULI_WINGMAN_MEMBERS_PER_FLIGHT;
	}
	Ar.SerializeInt(SampleCount, static_cast<uint32>(GULI_WINGMAN_MEMBERS_PER_FLIGHT) + 1u);
	if (Ar.IsLoading())
	{
		Samples.SetNum(static_cast<int32>(SampleCount));
	}
	for (uint32 Index = 0u; Index < SampleCount; ++Index)
	{
		Samples[static_cast<int32>(Index)].NetSerialize(Ar, Map, bFieldSuccess);
		bAllFieldsSucceeded &= bFieldSuccess;
	}
	bOutSuccess = bAllFieldsSucceeded && !Ar.IsError();
	return true;
}

bool FGuLiWingmanAttackFireRecord::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
    Ar << MemberIndex << SlotId << ShotIndex;
    Ar.SerializeIntPacked(ClientSimTick); Ar.SerializeIntPacked(ProfileRevision);
    Ar.SerializeIntPacked(LoadoutRevision); Ar.SerializeIntPacked(RunId);
    bool bTarget = false; Target.Target.NetSerialize(Ar, Map, bTarget);
    Ar << Target.Location << Target.Radius << Target.bGround << Target.bSpecified;
      Ar.SerializeIntPacked(Target.Revision); Ar << Target.ServerTime;
      Ar << ApproachDirection;
    bOutSuccess = !Ar.IsError() && bTarget && MemberIndex < GULI_WINGMAN_MEMBERS_PER_FLIGHT
        && ClientSimTick != 0 && !SlotId.IsNone() && ProfileRevision != 0 && LoadoutRevision != 0
          && RunId != 0 && Target.IsValid() && FMath::IsFinite(Target.ServerTime) && FMath::IsFinite(Target.Radius)
          && !ApproachDirection.ContainsNaN() && FMath::IsNearlyEqual(ApproachDirection.SizeSquared(), 1.0, 0.001)
          && FMath::Abs(ApproachDirection.Z) < 0.001;
    return true;
}

bool FGuLiWingmanCandidateBatch::IsWellFormed() const
{
	if (ProtocolVersion != GULI_WINGMAN_PROTOCOL_VERSION || MatchEpoch == 0u || !Group.IsValid()
		|| LeaseEpoch == 0u || CandidateSequence == 0u || ClientSimTick == 0u || !CarrierSource.IsValid()
		|| AbilitySetRevision == 0u || FormationCommandRevision == 0u || FormationDefinitionChecksum == 0u
		|| Samples.IsEmpty() || Samples.Num() > GULI_WINGMAN_GROUP_SIZE)
	{
		return false;
	}

	TSet<FGuLiWingmanHandle> Seen;
	for (const FGuLiWingmanCandidateSample& Sample : Samples)
	{
		if (!Sample.IsWellFormed(Group) || Seen.Contains(Sample.Wingman))
		{
			return false;
		}
		Seen.Add(Sample.Wingman);
	}

	// Compatibility is deliberately recognizable and cannot be produced by the new network bridge.
	// It keeps older UObject-free combat fixtures useful while production sends exactly one Flight.
	if (!UsesStrictFlightContract())
	{
		return FlightIndex == MAX_uint8 && ConnectionGeneration == 0u && RosterRevision == 0u
			&& RequiredMemberMask == 0u && ObservedGrantRevision == 0u && FrameSequence == 0u
			&& BaseAcceptedSequence == 0u && CaptureEstimatedServerTimeSeconds == 0.0
			&& NavSchemaRevision == 0u && NavDataChecksum == 0u && TuningRevision == 0u
			&& ObstacleRevision == 0u && TrailSamples.IsEmpty() && AttackFireRecords.IsEmpty();
	}

	if (ConnectionGeneration == 0u || RosterRevision == 0u || RequiredMemberMask == 0u
		|| RequiredMemberMask >= (1u << GULI_WINGMAN_MEMBERS_PER_FLIGHT)
		|| BuildMemberMask(Samples, FlightIndex) != RequiredMemberMask
		|| static_cast<uint8>(RequestedRateClass) > static_cast<uint8>(EGuLiWingmanUploadRateClass::HighRate10Hz)
		|| ObservedGrantRevision == 0u || FrameSequence == 0u
		|| !FMath::IsFinite(CaptureEstimatedServerTimeSeconds)
		|| CaptureEstimatedServerTimeSeconds < 0.0 || NavSchemaRevision == 0u
		|| NavDataChecksum == 0u || TuningRevision == 0u || ObstacleRevision == 0u
		|| TrailSamples.Num() > GULI_WINGMAN_MAX_TRAIL_SAMPLES)
	{
		return false;
	}

	uint32 PreviousTick = 0u;
	double PreviousCaptureTime = -1.0;
	for (const FGuLiWingmanCandidateTrailSample& Trail : TrailSamples)
	{
		if (!Trail.IsWellFormed(Group, FlightIndex, RequiredMemberMask)
			|| Trail.ClientSimTick <= PreviousTick || Trail.ClientSimTick >= ClientSimTick
			|| Trail.CaptureEstimatedServerTimeSeconds <= PreviousCaptureTime
			|| Trail.CaptureEstimatedServerTimeSeconds >= CaptureEstimatedServerTimeSeconds)
		{
			return false;
		}
		// A member bit alone is not an identity: entity generation is part of the stable handle.
		// Every trail frame must therefore contain exactly the endpoint's handle set.
		for (const FGuLiWingmanCandidateSample& TrailSample : Trail.Samples)
		{
			if (!Seen.Contains(TrailSample.Wingman))
			{
				return false;
			}
		}
		PreviousTick = Trail.ClientSimTick;
		PreviousCaptureTime = Trail.CaptureEstimatedServerTimeSeconds;
	}
    if (AttackFireRecords.Num() > GuLiWingmanAttack::MaximumFireRecordsPerFlight) return false;
    for (const auto& Shot : AttackFireRecords)
    {
        if (Shot.MemberIndex >= GULI_WINGMAN_MEMBERS_PER_FLIGHT || !(RequiredMemberMask & (1u << Shot.MemberIndex))
            || Shot.SlotId.IsNone() || Shot.RunId == 0 || Shot.ProfileRevision == 0 || Shot.LoadoutRevision == 0
            || !Shot.Target.IsValid() || !FMath::IsFinite(Shot.Target.ServerTime) || !FMath::IsFinite(Shot.Target.Radius)
            || Shot.ApproachDirection.ContainsNaN() || !FMath::IsNearlyEqual(Shot.ApproachDirection.SizeSquared(), 1.0, 0.001)
            || FMath::Abs(Shot.ApproachDirection.Z) > 0.001) return false;
        if (Shot.ClientSimTick != ClientSimTick && !TrailSamples.ContainsByPredicate(
            [&Shot](const auto& Trail) { return Trail.ClientSimTick == Shot.ClientSimTick; })) return false;
    }
    return true;
}

uint64 FGuLiWingmanCandidateBatch::ComputeStablePayloadHash() const
{
	uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
	GuLiShipAbilityHash::AddUInt32(Hash, ProtocolVersion);
	GuLiShipAbilityHash::AddUInt32(Hash, MatchEpoch);
	GuLiShipAbilityHash::AddUInt32(Hash, ConnectionGeneration);
	AddGroup(Hash, Group);
	GuLiShipAbilityHash::AddUInt32(Hash, LeaseEpoch);
	GuLiShipAbilityHash::AddUInt32(Hash, RosterRevision);
	GuLiShipAbilityHash::AddUInt32(Hash, FlightIndex);
	GuLiShipAbilityHash::AddUInt32(Hash, RequiredMemberMask);
	GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint8>(RequestedRateClass));
	GuLiShipAbilityHash::AddUInt32(Hash, ObservedGrantRevision);
	GuLiShipAbilityHash::AddUInt32(Hash, CandidateSequence);
	GuLiShipAbilityHash::AddUInt32(Hash, FrameSequence);
	GuLiShipAbilityHash::AddUInt32(Hash, BaseAcceptedSequence);
	GuLiShipAbilityHash::AddUInt32(Hash, ClientSimTick);
	AddDouble(Hash, CaptureEstimatedServerTimeSeconds);
	GuLiShipAbilityHash::AddUInt32(Hash, NavSchemaRevision);
	GuLiShipAbilityHash::AddUInt64(Hash, NavDataChecksum);
	GuLiShipAbilityHash::AddUInt32(Hash, TuningRevision);
	GuLiShipAbilityHash::AddUInt32(Hash, ObstacleRevision);
	GuLiShipAbilityHash::AddUInt32(Hash, CarrierSource.CanonicalEpoch);
	GuLiShipAbilityHash::AddUInt32(Hash, CarrierSource.MoveRevision);
	GuLiShipAbilityHash::AddUInt32(Hash, AbilitySetRevision);
	GuLiShipAbilityHash::AddUInt32(Hash, FormationCommandRevision);
	GuLiShipAbilityHash::AddUInt64(Hash, FormationDefinitionChecksum);
	GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Samples.Num()));
	for (const FGuLiWingmanCandidateSample& Sample : Samples)
	{
		AddCandidateSample(Hash, Sample);
	}
	GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(TrailSamples.Num()));
	for (const FGuLiWingmanCandidateTrailSample& Trail : TrailSamples)
	{
		GuLiShipAbilityHash::AddUInt32(Hash, Trail.ClientSimTick);
		AddDouble(Hash, Trail.CaptureEstimatedServerTimeSeconds);
		GuLiShipAbilityHash::AddUInt32(Hash, Trail.CarrierSource.CanonicalEpoch);
		GuLiShipAbilityHash::AddUInt32(Hash, Trail.CarrierSource.MoveRevision);
		for (const FGuLiWingmanCandidateSample& Sample : Trail.Samples)
		{
			AddCandidateSample(Hash, Sample);
		}
	}
    GuLiShipAbilityHash::AddUInt32(Hash, AttackFireRecords.Num());
    for (const auto& Shot : AttackFireRecords)
    {
        for (uint32 Value : {uint32(Shot.MemberIndex), Shot.ClientSimTick, Shot.ProfileRevision, Shot.LoadoutRevision,
            Shot.RunId, uint32(Shot.ShotIndex), Shot.Target.Revision, uint32(Shot.Target.Target.Kind),
            Shot.Target.Target.Generation, Shot.Target.Target.LocalId}) GuLiShipAbilityHash::AddUInt32(Hash, Value);
        GuLiShipAbilityHash::AddString(Hash, Shot.SlotId.ToString());
        GuLiShipAbilityHash::AddString(Hash, Shot.Target.Target.AuthorityId.ToString());
        for (double Value : {Shot.Target.Location.X, Shot.Target.Location.Y, Shot.Target.Location.Z, Shot.Target.ServerTime}) AddDouble(Hash, Value);
        for (double Value : {Shot.ApproachDirection.X, Shot.ApproachDirection.Y, Shot.ApproachDirection.Z}) AddDouble(Hash, Value);
        GuLiShipAbilityHash::AddFloat(Hash, Shot.Target.Radius);
        GuLiShipAbilityHash::AddUInt32(Hash, Shot.Target.bGround); GuLiShipAbilityHash::AddUInt32(Hash, Shot.Target.bSpecified);
    }
    return GuLiShipAbilityHash::Finish(Hash);
}

bool FGuLiWingmanCandidateBatch::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar.SerializeIntPacked(ProtocolVersion);
	Ar.SerializeIntPacked(MatchEpoch);
	Ar.SerializeIntPacked(ConnectionGeneration);
	bool bFieldSuccess = false;
	Group.NetSerialize(Ar, Map, bFieldSuccess);
	bool bAllFieldsSucceeded = bFieldSuccess;
	Ar.SerializeIntPacked(LeaseEpoch);
	Ar.SerializeIntPacked(RosterRevision);
	Ar << FlightIndex;
	Ar << RequiredMemberMask;
	uint8 RequestedRateValue = static_cast<uint8>(RequestedRateClass);
	Ar << RequestedRateValue;
	if (Ar.IsLoading())
	{
		RequestedRateClass = RequestedRateValue <= static_cast<uint8>(EGuLiWingmanUploadRateClass::HighRate10Hz)
			? static_cast<EGuLiWingmanUploadRateClass>(RequestedRateValue)
			: EGuLiWingmanUploadRateClass::Cruise5Hz;
	}
	Ar.SerializeIntPacked(ObservedGrantRevision);
	Ar.SerializeIntPacked(CandidateSequence);
	Ar.SerializeIntPacked(FrameSequence);
	Ar.SerializeIntPacked(BaseAcceptedSequence);
	Ar.SerializeIntPacked(ClientSimTick);
	Ar << CaptureEstimatedServerTimeSeconds;
	Ar.SerializeIntPacked(NavSchemaRevision);
	Ar << NavDataChecksum;
	Ar.SerializeIntPacked(TuningRevision);
	Ar.SerializeIntPacked(ObstacleRevision);
	CarrierSource.NetSerialize(Ar, Map, bFieldSuccess);
	bAllFieldsSucceeded &= bFieldSuccess;
	Ar.SerializeIntPacked(AbilitySetRevision);
	Ar.SerializeIntPacked(FormationCommandRevision);
	Ar << FormationDefinitionChecksum;

	uint32 SampleCount = Ar.IsSaving() ? static_cast<uint32>(Samples.Num()) : 0u;
	if (Ar.IsSaving() && SampleCount > GULI_WINGMAN_GROUP_SIZE)
	{
		bAllFieldsSucceeded = false;
		SampleCount = GULI_WINGMAN_GROUP_SIZE;
	}
	Ar.SerializeInt(SampleCount, static_cast<uint32>(GULI_WINGMAN_GROUP_SIZE) + 1u);
	if (Ar.IsLoading())
	{
		Samples.SetNum(static_cast<int32>(SampleCount));
	}
	for (uint32 Index = 0u; Index < SampleCount; ++Index)
	{
		Samples[static_cast<int32>(Index)].NetSerialize(Ar, Map, bFieldSuccess);
		bAllFieldsSucceeded &= bFieldSuccess;
	}
	uint32 TrailCount = Ar.IsSaving() ? static_cast<uint32>(TrailSamples.Num()) : 0u;
	if (Ar.IsSaving() && TrailCount > GULI_WINGMAN_MAX_TRAIL_SAMPLES)
	{
		bAllFieldsSucceeded = false;
		TrailCount = GULI_WINGMAN_MAX_TRAIL_SAMPLES;
	}
	Ar.SerializeInt(TrailCount, static_cast<uint32>(GULI_WINGMAN_MAX_TRAIL_SAMPLES) + 1u);
	if (Ar.IsLoading())
	{
		TrailSamples.SetNum(static_cast<int32>(TrailCount));
	}
	for (uint32 Index = 0u; Index < TrailCount; ++Index)
	{
		TrailSamples[static_cast<int32>(Index)].NetSerialize(Ar, Map, bFieldSuccess);
		bAllFieldsSucceeded &= bFieldSuccess;
	}
    uint32 FireCount = Ar.IsSaving() ? static_cast<uint32>(AttackFireRecords.Num()) : 0u;
    if (FireCount > GuLiWingmanAttack::MaximumFireRecordsPerFlight) { bOutSuccess = false; return false; }
    Ar.SerializeInt(FireCount, GuLiWingmanAttack::MaximumFireRecordsPerFlight + 1u);
    if (FireCount > GuLiWingmanAttack::MaximumFireRecordsPerFlight) { bOutSuccess = false; return false; }
    if (Ar.IsLoading()) AttackFireRecords.SetNum(FireCount);
    for (auto& Shot : AttackFireRecords) { Shot.NetSerialize(Ar, Map, bFieldSuccess); bAllFieldsSucceeded &= bFieldSuccess; }
	bOutSuccess = bAllFieldsSucceeded && !Ar.IsError() && IsWellFormed();
	return true;
}

bool FGuLiWingmanUploadRateGrant::IsWellFormed() const
{
	return ProtocolVersion == GULI_WINGMAN_PROTOCOL_VERSION && Group.IsValid()
		&& ConnectionGeneration != 0u && LeaseEpoch != 0u && GrantRevision != 0u
		&& EffectiveClientSimTick != 0u && FMath::IsFinite(ExpiryServerTimeSeconds)
		&& ExpiryServerTimeSeconds >= 0.0
		&& static_cast<uint8>(RateClass) <= static_cast<uint8>(EGuLiWingmanUploadRateClass::HighRate10Hz);
}

bool FGuLiWingmanUploadRateGrant::IsHighRateActive(
	const uint32 ClientSimTick, const double EstimatedServerTimeSeconds) const
{
	return IsWellFormed() && RateClass == EGuLiWingmanUploadRateClass::HighRate10Hz
		&& ClientSimTick >= EffectiveClientSimTick && FMath::IsFinite(EstimatedServerTimeSeconds)
		&& EstimatedServerTimeSeconds < ExpiryServerTimeSeconds;
}

bool FGuLiWingmanAtomicCandidateBatchHeader::IsWellFormed() const
{
	constexpr uint8 AllFlightsMask = static_cast<uint8>((1u << GULI_WINGMAN_FLIGHT_COUNT) - 1u);
	return ProtocolVersion == GULI_WINGMAN_PROTOCOL_VERSION && BatchId != 0u && Group.IsValid()
		&& ConnectionGeneration != 0u && LeaseEpoch != 0u && FrozenRosterRevision != 0u
		&& FrozenRequiredFlightMask != 0u && (FrozenRequiredFlightMask & ~AllFlightsMask) == 0u
		&& FrozenRequiredMemberMaskHash != 0u && BaselineRevision != 0u && BaselineHash != 0u
		&& IncludedFlightMask == FrozenRequiredFlightMask && ClientBatchStartTick != 0u
		&& BatchPayloadBytes > 0u && BatchPayloadBytes <= GULI_WINGMAN_ATOMIC_BATCH_MAX_BYTES
		&& FragmentCount > 0u && FragmentCount <= GULI_WINGMAN_ATOMIC_BATCH_MAX_FRAGMENTS
		&& BatchPayloadHash != 0u && bAtomicCommit
		&& static_cast<uint8>(BatchKind) <= static_cast<uint8>(EGuLiWingmanAtomicBatchKind::Takeover);
}

bool FGuLiWingmanAtomicCandidateBatchFragment::IsWellFormed() const
{
	if (!Header.IsWellFormed() || FragmentIndex >= Header.FragmentCount || Flights.IsEmpty()
		|| Flights.Num() > GULI_WINGMAN_FLIGHT_COUNT)
	{
		return false;
	}
	uint8 FlightMask = 0u;
	for (const FGuLiWingmanCandidateBatch& Flight : Flights)
	{
		if (!Flight.IsWellFormed() || !Flight.UsesStrictFlightContract()
			|| Flight.Group != Header.Group || Flight.ConnectionGeneration != Header.ConnectionGeneration
			|| Flight.LeaseEpoch != Header.LeaseEpoch || Flight.RosterRevision != Header.FrozenRosterRevision)
		{
			return false;
		}
		const uint8 FlightBit = static_cast<uint8>(1u << Flight.FlightIndex);
		if ((Header.IncludedFlightMask & FlightBit) == 0u || (FlightMask & FlightBit) != 0u)
		{
			return false;
		}
		FlightMask |= FlightBit;
	}
	return EstimatePayloadBytes() <= Header.BatchPayloadBytes;
}

uint32 FGuLiWingmanAtomicCandidateBatchFragment::EstimatePayloadBytes() const
{
	uint32 TotalBytes = 0u;
	for (const FGuLiWingmanCandidateBatch& Flight : Flights)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		FGuLiWingmanCandidateBatch Copy = Flight;
		bool bSuccess = false;
		Copy.NetSerialize(Writer, nullptr, bSuccess);
		if (!bSuccess || Writer.IsError())
		{
			return MAX_uint32;
		}
		TotalBytes += static_cast<uint32>(Bytes.Num());
	}
	return TotalBytes;
}

uint64 FGuLiWingmanAtomicCandidateBatchFragment::ComputePayloadHash() const
{
	uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
	for (const FGuLiWingmanCandidateBatch& Flight : Flights)
	{
		GuLiShipAbilityHash::AddUInt32(Hash, Flight.FlightIndex);
		GuLiShipAbilityHash::AddUInt64(Hash, Flight.ComputeStablePayloadHash());
	}
	return GuLiShipAbilityHash::Finish(Hash);
}

bool FGuLiWingmanFireIntent::IsWellFormed() const
{
	const int32 AimAbsMax = AimDirectionMilli.GetAbsMax();
	const double AimLengthSquared = FVector(
		static_cast<double>(AimDirectionMilli.X),
		static_cast<double>(AimDirectionMilli.Y),
		static_cast<double>(AimDirectionMilli.Z)).SizeSquared();
	return ProtocolVersion == GULI_WINGMAN_PROTOCOL_VERSION && MatchEpoch != 0u && Group.IsValid()
		&& LeaseEpoch != 0u && DomainFireSequence != 0u && Emitter.IsValid()
		&& Emitter.Flight.Group == Group && Binding.IsWellFormed()
		&& Binding.Domain == EGuLiWeaponDomain::Wingman
		&& Binding.MatchEpoch == MatchEpoch && SourceAcceptedState.IsValid()
		&& ClientFireTick != 0u
		&& SourceAcceptedState.MatchEpoch == MatchEpoch
		&& SourceAcceptedState.GroupGeneration == Group.GroupGeneration
		&& Target.IsValid() && TargetAssignmentRevision != 0u
		&& WeaponAbilityId.IsValid() && !SkillId.IsNone()
		&& LoadoutRevision != 0u && ProfileRevision != 0u
		&& WeaponDefinitionRevision != 0u && AbilitySetRevision != 0u && AimAbsMax <= 1000
		&& AimLengthSquared >= 250000.0 && AimLengthSquared <= 2250000.0;
}

bool FGuLiWingmanFireIntent::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar.SerializeIntPacked(ProtocolVersion);
	Ar.SerializeIntPacked(MatchEpoch);
	bool bFieldSuccess = false;
	Group.NetSerialize(Ar, Map, bFieldSuccess);
	bool bAllFieldsSucceeded = bFieldSuccess;
	Ar.SerializeIntPacked(LeaseEpoch);
	Ar.SerializeIntPacked(DomainFireSequence);
	Emitter.NetSerialize(Ar, Map, bFieldSuccess);
	bAllFieldsSucceeded &= bFieldSuccess;
	Ar.SerializeIntPacked(Binding.MatchEpoch);
	uint8 BindingTeam = static_cast<uint8>(Binding.Team);
	uint8 BindingDomain = static_cast<uint8>(Binding.Domain);
	Ar << BindingTeam;
	Ar << Binding.OwnerPlayerGuid;
	Ar << BindingDomain;
	Ar << Binding.SubjectId;
	Ar << Binding.SlotId;
	if (Ar.IsLoading())
	{
		Binding.Team = static_cast<EGuLiTeam>(BindingTeam);
		Binding.Domain = static_cast<EGuLiWeaponDomain>(BindingDomain);
	}
	SourceAcceptedState.NetSerialize(Ar, Map, bFieldSuccess);
	bAllFieldsSucceeded &= bFieldSuccess;
	Ar.SerializeIntPacked(ClientFireTick);
	Target.NetSerialize(Ar, Map, bFieldSuccess);
	bAllFieldsSucceeded &= bFieldSuccess;
	Ar.SerializeIntPacked(TargetAssignmentRevision);
	bAllFieldsSucceeded &= SerializeTag(Ar, WeaponAbilityId);
	Ar << SkillId;
	Ar.SerializeIntPacked(LoadoutRevision);
	Ar.SerializeIntPacked(ProfileRevision);
	Ar.SerializeIntPacked(WeaponDefinitionRevision);
	Ar.SerializeIntPacked(AbilitySetRevision);
	Ar << AimDirectionMilli.X << AimDirectionMilli.Y << AimDirectionMilli.Z;
	uint8 ClientLosBit = bClientPredictedLineOfSight ? 1u : 0u;
	Ar.SerializeBits(&ClientLosBit, 1);
	if (Ar.IsLoading())
	{
		bClientPredictedLineOfSight = ClientLosBit != 0u;
	}
	bOutSuccess = bAllFieldsSucceeded && !Ar.IsError() && IsWellFormed();
	return true;
}

bool FGuLiWingmanBootstrapScopeState::IsWellFormed() const
{
	return static_cast<uint8>(Scope) < static_cast<uint8>(EGuLiWingmanBootstrapScope::Count)
		&& Revision != 0u && Hash != 0u && ChunkCount != 0u;
}

const FGuLiWingmanBootstrapScopeState* FGuLiWingmanBootstrapCommit::FindScope(
	const EGuLiWingmanBootstrapScope Scope) const
{
	return Scopes.FindByPredicate([Scope](const FGuLiWingmanBootstrapScopeState& Entry)
	{
		return Entry.Scope == Scope;
	});
}

bool FGuLiWingmanBootstrapCommit::IsWellFormed() const
{
	if (ProtocolVersion != GULI_WINGMAN_PROTOCOL_VERSION || CutId == 0u || !Group.IsValid()
		|| Scopes.Num() != static_cast<int32>(EGuLiWingmanBootstrapScope::Count))
	{
		return false;
	}
	for (uint8 ScopeValue = 0u; ScopeValue < static_cast<uint8>(EGuLiWingmanBootstrapScope::Count); ++ScopeValue)
	{
		const FGuLiWingmanBootstrapScopeState* State = FindScope(static_cast<EGuLiWingmanBootstrapScope>(ScopeValue));
		if (!State || !State->IsWellFormed())
		{
			return false;
		}
	}
	return true;
}

bool FGuLiWingmanTransferBaseline::IsWellFormed() const
{
	return ProtocolVersion == GULI_WINGMAN_PROTOCOL_VERSION && CutId != 0u && Group.IsValid()
		&& LeaseEpoch != 0u && AbilityConfigRevision != 0u && AbilityConfigHash != 0u
		&& AcceptedSnapshotRevision != 0u && AcceptedSnapshotHash != 0u;
}

EGuLiWingmanRejectReason GuLiWingmanProtocol::ValidateCandidateAbilityConfig(
	const FGuLiWingmanCandidateBatch& Candidate, const FGuLiGroupAbilityConfigSnapshot* ConfirmedConfig)
{
	if (Candidate.ProtocolVersion != GULI_WINGMAN_PROTOCOL_VERSION)
	{
		return EGuLiWingmanRejectReason::ProtocolMismatch;
	}
	if (!Candidate.IsWellFormed())
	{
		return EGuLiWingmanRejectReason::InvalidIdentity;
	}
	if (!ConfirmedConfig || !ConfirmedConfig->IsWellFormed() || !ConfirmedConfig->HasRequiredV1Abilities())
	{
		return EGuLiWingmanRejectReason::MissingAbilityConfig;
	}
	if (!MatchesGroup(Candidate.Group, *ConfirmedConfig))
	{
		return EGuLiWingmanRejectReason::WrongGeneration;
	}
	if (Candidate.AbilitySetRevision != ConfirmedConfig->AbilitySetRevision)
	{
		return EGuLiWingmanRejectReason::StaleAbilitySetRevision;
	}
	if (Candidate.FormationCommandRevision != ConfirmedConfig->FormationCommandRevision)
	{
		return EGuLiWingmanRejectReason::StaleFormationCommand;
	}
	if (Candidate.FormationDefinitionChecksum != ConfirmedConfig->FormationDefinitionChecksum)
	{
		return EGuLiWingmanRejectReason::FormationChecksumMismatch;
	}
	return EGuLiWingmanRejectReason::None;
}

EGuLiWingmanRejectReason GuLiWingmanProtocol::ValidateFireIntentAbilityConfig(
	const FGuLiWingmanFireIntent& Intent, const FGuLiGroupAbilityConfigSnapshot* ConfirmedConfig)
{
	if (Intent.ProtocolVersion != GULI_WINGMAN_PROTOCOL_VERSION)
	{
		return EGuLiWingmanRejectReason::ProtocolMismatch;
	}
	if (!Intent.IsWellFormed())
	{
		return EGuLiWingmanRejectReason::InvalidIdentity;
	}
	if (!ConfirmedConfig || !ConfirmedConfig->IsWellFormed() || !ConfirmedConfig->HasRequiredV1Abilities())
	{
		return EGuLiWingmanRejectReason::MissingAbilityConfig;
	}
	if (!MatchesGroup(Intent.Group, *ConfirmedConfig))
	{
		return EGuLiWingmanRejectReason::WrongGeneration;
	}
	if (Intent.AbilitySetRevision != ConfirmedConfig->AbilitySetRevision)
	{
		return EGuLiWingmanRejectReason::StaleAbilitySetRevision;
	}
	if (Intent.LoadoutRevision != ConfirmedConfig->LoadoutRevision)
	{
		return EGuLiWingmanRejectReason::StaleLoadoutRevision;
	}
	const FGuLiWingmanWeaponChannelConfig* Channel =
		ConfirmedConfig->FindWeaponChannel(Intent.Binding);
	if (!Channel || !Channel->bEnabled)
	{
		return EGuLiWingmanRejectReason::UnknownWeaponChannel;
	}
	if (Intent.WeaponAbilityId != Channel->AbilityId)
	{
		return EGuLiWingmanRejectReason::UnknownWeaponAbility;
	}
	if (Intent.SkillId != Channel->SkillId)
	{
		return EGuLiWingmanRejectReason::WeaponSkillMismatch;
	}
	if (Intent.ProfileRevision != Channel->ProfileRevision)
	{
		return EGuLiWingmanRejectReason::StaleProfileRevision;
	}
	return Intent.WeaponDefinitionRevision == Channel->DefinitionRevision
		? EGuLiWingmanRejectReason::None : EGuLiWingmanRejectReason::WeaponDefinitionMismatch;
}

uint64 FGuLiWingmanAttackAuthorityState::ComputeStableHash() const
{
	uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
	GuLiShipAbilityHash::AddUInt32(Hash, Revision);
	AddAttackTarget(Hash, Target);
	GuLiShipAbilityHash::AddUInt32(Hash, AutomaticTargets.Num());
	for (const FGuLiWingmanAutoTargetAssignment& Assignment : AutomaticTargets)
	{
		AddGroup(Hash, Assignment.Emitter.Flight.Group);
		GuLiShipAbilityHash::AddUInt32(Hash, Assignment.Emitter.Flight.FlightIndex);
		GuLiShipAbilityHash::AddUInt32(Hash, Assignment.Emitter.MemberIndex);
		GuLiShipAbilityHash::AddUInt32(Hash, Assignment.Emitter.EntityGeneration);
		AddAttackTarget(Hash, Assignment.Target);
	}
	GuLiShipAbilityHash::AddUInt32(Hash, Checkpoints.Num());
	for (const FGuLiWingmanAttackCheckpoint& Checkpoint : Checkpoints)
	{
		AddGroup(Hash, Checkpoint.Emitter.Flight.Group);
		for (const uint32 Value : {
			static_cast<uint32>(Checkpoint.Emitter.Flight.FlightIndex),
			static_cast<uint32>(Checkpoint.Emitter.MemberIndex),
			Checkpoint.Emitter.EntityGeneration,
			Checkpoint.ProfileRevision,
			Checkpoint.RunId,
			Checkpoint.LeaseEpoch,
			static_cast<uint32>(Checkpoint.LastShotIndex)})
		{
			GuLiShipAbilityHash::AddUInt32(Hash, Value);
		}
		GuLiShipAbilityHash::AddString(Hash, Checkpoint.SlotId.ToString());
		GuLiShipAbilityHash::AddString(Hash, Checkpoint.SkillId.ToString());
		GuLiShipAbilityHash::AddUInt64(Hash, Checkpoint.DefinitionChecksum);
		AddGuid(Hash, Checkpoint.FrozenTargetHandle.AuthorityId);
		for (const uint32 Value : {
			static_cast<uint32>(Checkpoint.FrozenTargetHandle.Kind),
			Checkpoint.FrozenTargetHandle.Generation,
			Checkpoint.FrozenTargetHandle.LocalId})
		{
			GuLiShipAbilityHash::AddUInt32(Hash, Value);
		}
		for (const double Value : {
			Checkpoint.StartTime,
			Checkpoint.NextFireTime,
			Checkpoint.FrozenTarget.X,
			Checkpoint.FrozenTarget.Y,
			Checkpoint.FrozenTarget.Z,
			Checkpoint.ApproachDirection.X,
			Checkpoint.ApproachDirection.Y,
			Checkpoint.ApproachDirection.Z})
		{
			AddDouble(Hash, Value);
		}
	}
	return GuLiShipAbilityHash::Finish(Hash);
}

bool FGuLiWingmanAttackAuthorityState::IsWellFormed(
	const FGuLiWingmanGroupHandle& ExpectedGroup) const
{
	if (!ExpectedGroup.IsValid() || AutomaticTargets.Num() > GULI_WINGMAN_GROUP_SIZE
		|| Target.Location.ContainsNaN() || !FMath::IsFinite(Target.ServerTime)
		|| Target.ServerTime < 0.0 || !FMath::IsFinite(Target.Radius) || Target.Radius < 0.0f
		|| (Target.Target.IsValid() && (!Target.IsValid() || !Target.bSpecified
			|| !HasMatchingAttackClassification(Target)))
		|| (!Target.Target.IsValid() && Target.bSpecified)
		|| (Target.Target.IsValid() && !AutomaticTargets.IsEmpty()))
	{
		return false;
	}
	for (int32 Index = 0; Index < AutomaticTargets.Num(); ++Index)
	{
		const FGuLiWingmanAutoTargetAssignment& Assignment = AutomaticTargets[Index];
		if (!Assignment.IsWellFormed(ExpectedGroup)
			|| (Index > 0 && !IsStableWingmanLess(
				AutomaticTargets[Index - 1].Emitter,
				Assignment.Emitter)))
		{
			return false;
		}
	}
	return true;
}

bool FGuLiWingmanAttackAuthorityState::NetSerialize(
	FArchive& Ar,
	UPackageMap* Map,
	bool& bOutSuccess)
{
	bool bFieldSuccess = false;
	Target.NetSerialize(Ar, Map, bFieldSuccess);
	bool bAllFieldsSucceeded = bFieldSuccess;

	uint32 AutomaticTargetCount = Ar.IsSaving()
		? static_cast<uint32>(AutomaticTargets.Num()) : 0u;
	Ar.SerializeIntPacked(AutomaticTargetCount);
	if (AutomaticTargetCount > GULI_WINGMAN_GROUP_SIZE)
	{
		Ar.SetError();
		bOutSuccess = false;
		return true;
	}
	if (Ar.IsLoading()) AutomaticTargets.SetNum(static_cast<int32>(AutomaticTargetCount));
	for (FGuLiWingmanAutoTargetAssignment& Assignment : AutomaticTargets)
	{
		Assignment.NetSerialize(Ar, Map, bFieldSuccess);
		bAllFieldsSucceeded &= bFieldSuccess;
	}
	if (!AutomaticTargets.IsEmpty())
	{
		const FGuLiWingmanGroupHandle& AutomaticGroup =
			AutomaticTargets[0].Emitter.Flight.Group;
		for (const FGuLiWingmanAutoTargetAssignment& Assignment : AutomaticTargets)
		{
			bAllFieldsSucceeded &= Assignment.IsWellFormed(AutomaticGroup);
		}
	}

	constexpr uint32 MaximumCheckpointCount =
		GULI_WINGMAN_GROUP_SIZE * GULI_MAX_WINGMAN_WEAPON_CHANNELS;
	uint32 CheckpointCount = Ar.IsSaving()
		? static_cast<uint32>(Checkpoints.Num()) : 0u;
	Ar.SerializeIntPacked(CheckpointCount);
	if (CheckpointCount > MaximumCheckpointCount)
	{
		Ar.SetError();
		bOutSuccess = false;
		return true;
	}
	if (Ar.IsLoading()) Checkpoints.SetNum(static_cast<int32>(CheckpointCount));
	for (FGuLiWingmanAttackCheckpoint& Checkpoint : Checkpoints)
	{
		Checkpoint.NetSerialize(Ar, Map, bFieldSuccess);
		bAllFieldsSucceeded &= bFieldSuccess;
	}
	Ar.SerializeIntPacked(Revision);

	const bool bManualModeValid = Target.Target.IsValid()
		? Target.IsValid() && Target.bSpecified && AutomaticTargets.IsEmpty()
			&& HasMatchingAttackClassification(Target)
		: !Target.bSpecified;
	for (int32 Index = 1; Index < AutomaticTargets.Num(); ++Index)
	{
		bAllFieldsSucceeded &= IsStableWingmanLess(
			AutomaticTargets[Index - 1].Emitter,
			AutomaticTargets[Index].Emitter);
	}
	bOutSuccess = bAllFieldsSucceeded && bManualModeValid && !Ar.IsError();
	return true;
}

bool FGuLiWingmanAttackTarget::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
    uint8 Present = Target.IsValid(); Ar.SerializeBits(&Present,1);
    bool Valid = true;
    if (Present) Target.NetSerialize(Ar,Map,Valid);
    else if (Ar.IsLoading()) Target = {};
    Ar << Location << Radius << bGround << bSpecified << ServerTime;
    Ar.SerializeIntPacked(Revision);
    bOutSuccess = Valid && !Ar.IsError() && !Location.ContainsNaN() && FMath::IsFinite(Radius) && FMath::IsFinite(ServerTime);
    return true;
}

bool FGuLiWingmanAutoTargetAssignment::IsWellFormed(
	const FGuLiWingmanGroupHandle& ExpectedGroup) const
{
	return ExpectedGroup.IsValid() && Emitter.IsValid() && Emitter.Flight.Group == ExpectedGroup
		&& Target.IsValid() && !Target.bSpecified
		&& HasMatchingAttackClassification(Target)
		&& FMath::IsFinite(Target.ServerTime) && Target.ServerTime >= 0.0
		&& FMath::IsFinite(Target.Radius) && Target.Radius >= 0.0f;
}

bool FGuLiWingmanAutoTargetAssignment::NetSerialize(
	FArchive& Ar,
	UPackageMap* Map,
	bool& bOutSuccess)
{
	bool bEmitterSuccess = false;
	bool bTargetSuccess = false;
	Emitter.NetSerialize(Ar, Map, bEmitterSuccess);
	Target.NetSerialize(Ar, Map, bTargetSuccess);
	bOutSuccess = bEmitterSuccess && bTargetSuccess && !Ar.IsError()
		&& IsWellFormed(Emitter.Flight.Group);
	return true;
}

bool FGuLiWingmanEmergencyRebaseRequest::IsWellFormed() const
{
	return ProtocolVersion == GULI_WINGMAN_PROTOCOL_VERSION
		&& MatchEpoch != 0u
		&& ConnectionGeneration != 0u
		&& Wingman.IsValid()
		&& LeaseEpoch != 0u
		&& RosterRevision != 0u
		&& RequestSequence != 0u
		&& BaselineAcceptedSequence != 0u
		&& static_cast<uint8>(Reason)
			<= static_cast<uint8>(EGuLiWingmanEmergencyRebaseReason::CarrierBoundaryDeadlock);
}

bool FGuLiWingmanEmergencyRebaseRequest::NetSerialize(
	FArchive& Ar,
	UPackageMap* Map,
	bool& bOutSuccess)
{
	Ar.SerializeIntPacked(ProtocolVersion);
	Ar.SerializeIntPacked(MatchEpoch);
	Ar.SerializeIntPacked(ConnectionGeneration);
	bool bWingmanSuccess = false;
	Wingman.NetSerialize(Ar, Map, bWingmanSuccess);
	Ar.SerializeIntPacked(LeaseEpoch);
	Ar.SerializeIntPacked(RosterRevision);
	Ar.SerializeIntPacked(RequestSequence);
	Ar.SerializeIntPacked(BaselineAcceptedSequence);
	uint8 ReasonValue = static_cast<uint8>(Reason);
	Ar.SerializeBits(&ReasonValue, 2u);
	if (Ar.IsLoading())
	{
		Reason = static_cast<EGuLiWingmanEmergencyRebaseReason>(ReasonValue);
	}
	bOutSuccess = bWingmanSuccess && !Ar.IsError() && IsWellFormed();
	return true;
}

bool FGuLiWingmanEmergencyRebaseResponse::IsWellFormed() const
{
	if (ProtocolVersion != GULI_WINGMAN_PROTOCOL_VERSION
		|| !Wingman.IsValid()
		|| LeaseEpoch == 0u
		|| RequestSequence == 0u
		|| static_cast<uint8>(Result)
			> static_cast<uint8>(EGuLiWingmanEmergencyRebaseResult::NoSafePoint)
		|| ServerPosition.ContainsNaN()
		|| !FMath::IsFinite(RetryAfterServerTimeSeconds)
		|| RetryAfterServerTimeSeconds < 0.0)
	{
		return false;
	}
	return Result == EGuLiWingmanEmergencyRebaseResult::Accepted
		? AcceptedSequence != 0u
		: AcceptedSequence == 0u;
}

bool FGuLiWingmanEmergencyRebaseResponse::NetSerialize(
	FArchive& Ar,
	UPackageMap* Map,
	bool& bOutSuccess)
{
	Ar.SerializeIntPacked(ProtocolVersion);
	bool bWingmanSuccess = false;
	Wingman.NetSerialize(Ar, Map, bWingmanSuccess);
	Ar.SerializeIntPacked(LeaseEpoch);
	Ar.SerializeIntPacked(RequestSequence);
	uint8 ResultValue = static_cast<uint8>(Result);
	Ar.SerializeBits(&ResultValue, 3u);
	if (Ar.IsLoading())
	{
		Result = static_cast<EGuLiWingmanEmergencyRebaseResult>(ResultValue);
	}
	Ar.SerializeIntPacked(AcceptedSequence);
	Ar << ServerPosition;
	Ar << RetryAfterServerTimeSeconds;
	bOutSuccess = bWingmanSuccess && !Ar.IsError() && IsWellFormed();
	return true;
}

bool FGuLiWingmanAttackCheckpoint::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bool EmitterValid=false, TargetValid=false;
	Emitter.NetSerialize(Ar,Map,EmitterValid); FrozenTargetHandle.NetSerialize(Ar,Map,TargetValid);
	FString Slot=SlotId.ToString(), Skill=SkillId.ToString(); Ar << Slot << Skill;
	if(Ar.IsLoading()) { SlotId=FName(*Slot); SkillId=FName(*Skill); }
	Ar << DefinitionChecksum << ProfileRevision << RunId << LeaseEpoch << LastShotIndex;
	// Preserve doubles exactly: reliable takeover hashes cover this immutable cut.
	Ar << StartTime << NextFireTime << FrozenTarget << ApproachDirection;
	bOutSuccess=EmitterValid && TargetValid && !Ar.IsError() && !FrozenTarget.ContainsNaN() && !ApproachDirection.ContainsNaN();
	return true;
}
