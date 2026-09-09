// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Relay/GuLiWingmanRelayTypes.h"

#include "Gameplay/Ship/Abilities/GuLiShipAbilityTypes.h"
#include "Serialization/MemoryReader.h"
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

	void AddWingman(uint64& Hash, const FGuLiWingmanHandle& Wingman)
	{
		AddGroup(Hash, Wingman.Flight.Group);
		GuLiShipAbilityHash::AddUInt32(Hash, Wingman.Flight.FlightIndex);
		GuLiShipAbilityHash::AddUInt32(Hash, Wingman.MemberIndex);
		GuLiShipAbilityHash::AddUInt32(Hash, Wingman.EntityGeneration);
	}

	void AddVector(uint64& Hash, const FIntVector& Value)
	{
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Value.X));
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Value.Y));
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Value.Z));
	}

	void AddDouble(uint64& Hash, const double Value)
	{
		uint64 Bits = 0u;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		GuLiShipAbilityHash::AddUInt64(Hash, Bits);
	}

	bool AtomicHeadersEqual(const FGuLiWingmanAtomicCandidateBatchHeader& Lhs,
		const FGuLiWingmanAtomicCandidateBatchHeader& Rhs)
	{
		return Lhs.ProtocolVersion == Rhs.ProtocolVersion && Lhs.BatchId == Rhs.BatchId
			&& Lhs.BatchKind == Rhs.BatchKind && Lhs.Group == Rhs.Group
			&& Lhs.ConnectionGeneration == Rhs.ConnectionGeneration
			&& Lhs.LeaseEpoch == Rhs.LeaseEpoch
			&& Lhs.FrozenRosterRevision == Rhs.FrozenRosterRevision
			&& Lhs.FrozenRequiredFlightMask == Rhs.FrozenRequiredFlightMask
			&& Lhs.FrozenRequiredMemberMaskHash == Rhs.FrozenRequiredMemberMaskHash
			&& Lhs.BaselineRevision == Rhs.BaselineRevision
			&& Lhs.BaselineHash == Rhs.BaselineHash
			&& Lhs.IncludedFlightMask == Rhs.IncludedFlightMask
			&& Lhs.ClientBatchStartTick == Rhs.ClientBatchStartTick
			&& Lhs.BatchPayloadBytes == Rhs.BatchPayloadBytes
			&& Lhs.FragmentCount == Rhs.FragmentCount
			&& Lhs.BatchPayloadHash == Rhs.BatchPayloadHash
			&& Lhs.bAtomicCommit == Rhs.bAtomicCommit;
	}

	bool SerializeAcceptedBatch(FArchive& Ar, UPackageMap* Map, FGuLiWingmanAcceptedBatch& Batch)
	{
		bool bFieldSuccess = false;
		Batch.Group.NetSerialize(Ar, Map, bFieldSuccess);
		bool bAllFieldsSucceeded = bFieldSuccess;
		Batch.StateRef.NetSerialize(Ar, Map, bFieldSuccess);
		bAllFieldsSucceeded &= bFieldSuccess;
		Batch.CarrierSource.NetSerialize(Ar, Map, bFieldSuccess);
		bAllFieldsSucceeded &= bFieldSuccess;
		Ar.SerializeIntPacked(Batch.ConnectionGeneration);
		Ar.SerializeIntPacked(Batch.RosterRevision);
		Ar << Batch.FlightIndex;
		Ar.SerializeIntPacked(Batch.FrameSequence);
		Ar.SerializeIntPacked(Batch.BaseAcceptedSequence);
		Ar << Batch.CaptureEstimatedServerTimeSeconds;
		Ar.SerializeIntPacked(Batch.ValidationRevisions.NavSchemaRevision);
		Ar << Batch.ValidationRevisions.NavDataChecksum;
		Ar.SerializeIntPacked(Batch.ValidationRevisions.TuningRevision);
		Ar.SerializeIntPacked(Batch.ValidationRevisions.ObstacleRevision);
		Ar.SerializeIntPacked(Batch.AbilitySetRevision);
		Ar.SerializeIntPacked(Batch.FormationCommandRevision);
		Ar << Batch.FormationDefinitionChecksum;
		Ar << Batch.ServerAcceptedTimeSeconds;
		Ar << Batch.StableHash;
		Ar << Batch.RebasedMemberMask;

		uint32 SampleCount = Ar.IsSaving() ? static_cast<uint32>(Batch.Samples.Num()) : 0u;
		if (Ar.IsSaving() && SampleCount > GULI_WINGMAN_GROUP_SIZE)
		{
			bAllFieldsSucceeded = false;
			SampleCount = GULI_WINGMAN_GROUP_SIZE;
		}
		Ar.SerializeInt(SampleCount, static_cast<uint32>(GULI_WINGMAN_GROUP_SIZE) + 1u);
		if (Ar.IsLoading())
		{
			Batch.Samples.SetNum(static_cast<int32>(SampleCount));
		}
		for (uint32 Index = 0u; Index < SampleCount; ++Index)
		{
			Batch.Samples[static_cast<int32>(Index)].NetSerialize(Ar, Map, bFieldSuccess);
			bAllFieldsSucceeded &= bFieldSuccess;
		}
		return bAllFieldsSucceeded && !Ar.IsError() && Batch.IsWellFormed();
	}

	bool SerializeAcceptance(FArchive& Ar, UPackageMap* Map, FGuLiWingmanSimulationAcceptance& Acceptance)
	{
		Ar.SerializeIntPacked(Acceptance.ProtocolVersion);
		bool bFieldSuccess = false;
		Acceptance.Group.NetSerialize(Ar, Map, bFieldSuccess);
		bool bAllFieldsSucceeded = bFieldSuccess;
		Ar << Acceptance.FlightIndex;
		Ar.SerializeIntPacked(Acceptance.LeaseEpoch);
		Ar.SerializeIntPacked(Acceptance.RosterRevision);
		Ar.SerializeIntPacked(Acceptance.FrameSequence);
		uint8 Disposition = static_cast<uint8>(Acceptance.Disposition);
		uint8 RejectReason = static_cast<uint8>(Acceptance.RejectReason);
		Ar << Disposition << RejectReason;
		if (Ar.IsLoading())
		{
			if (Disposition > static_cast<uint8>(EGuLiWingmanSubmissionDisposition::Rejected)
				|| RejectReason > static_cast<uint8>(EGuLiWingmanRejectReason::AtomicBatchExpired))
			{
				bAllFieldsSucceeded = false;
			}
			Acceptance.Disposition = static_cast<EGuLiWingmanSubmissionDisposition>(Disposition);
			Acceptance.RejectReason = static_cast<EGuLiWingmanRejectReason>(RejectReason);
		}
		Ar.SerializeIntPacked(Acceptance.AcceptedSnapshotSequence);
		Ar << Acceptance.AcceptedServerTimeSeconds;
		Ar << Acceptance.ValidatedPayloadHash;
		Ar.SerializeIntPacked(Acceptance.NormalizationFlags);
		uint8 Availability = static_cast<uint8>(Acceptance.AvailabilityAfter);
		uint8 AllowedRate = static_cast<uint8>(Acceptance.AllowedUploadRateClass);
		Ar << Availability << AllowedRate;
		if (Ar.IsLoading())
		{
			if (Availability > static_cast<uint8>(EGuLiWingmanGroupLifecycle::Revoked)
				|| AllowedRate > static_cast<uint8>(EGuLiWingmanUploadRateClass::HighRate10Hz))
			{
				bAllFieldsSucceeded = false;
			}
			Acceptance.AvailabilityAfter = static_cast<EGuLiWingmanGroupLifecycle>(Availability);
			Acceptance.AllowedUploadRateClass = static_cast<EGuLiWingmanUploadRateClass>(AllowedRate);
		}
		Ar.SerializeIntPacked(Acceptance.GrantRevision);
		Ar << Acceptance.GrantExpiryServerTimeSeconds;
		uint8 bHasRebaseBaseline = Acceptance.RebaseBaseline.IsValid() ? 1u : 0u;
		Ar << bHasRebaseBaseline;
		if (Ar.IsLoading() && bHasRebaseBaseline > 1u)
		{
			bAllFieldsSucceeded = false;
		}
		if (bHasRebaseBaseline != 0u)
		{
			Acceptance.RebaseBaseline.NetSerialize(Ar, Map, bFieldSuccess);
			bAllFieldsSucceeded &= bFieldSuccess;
		}
		else if (Ar.IsLoading())
		{
			Acceptance.RebaseBaseline = FGuLiAcceptedStateRef{};
		}
		return bAllFieldsSucceeded && !Ar.IsError() && Acceptance.IsWellFormed();
	}

	bool HasExactlyOneScope(const FGuLiWingmanBootstrapCommit& Commit, const EGuLiWingmanBootstrapScope Scope,
		const uint32 ExpectedRevision, const uint64 ExpectedHash)
	{
		int32 MatchCount = 0;
		for (const FGuLiWingmanBootstrapScopeState& State : Commit.Scopes)
		{
			if (State.Scope == Scope)
			{
				++MatchCount;
				if (State.Revision != ExpectedRevision || State.Hash != ExpectedHash || State.ChunkCount != 1u)
				{
					return false;
				}
			}
		}
		return MatchCount == 1;
	}

	bool HasUniqueWingmen(const FGuLiWingmanGroupHandle& Group, const TArray<FGuLiWingmanRosterEntry>& Entries)
	{
		TSet<FGuLiWingmanHandle> Seen;
		for (const FGuLiWingmanRosterEntry& Entry : Entries)
		{
			if (!Entry.IsWellFormed(Group) || Seen.Contains(Entry.Wingman))
			{
				return false;
			}
			Seen.Add(Entry.Wingman);
		}
		return Seen.Num() == GULI_WINGMAN_GROUP_SIZE;
	}
}

bool FGuLiWingmanRosterEntry::IsWellFormed(const FGuLiWingmanGroupHandle& ExpectedGroup) const
{
	return Wingman.IsValid() && Wingman.Flight.Group == ExpectedGroup && !WingmanTypeId.IsNone();
}

bool FGuLiWingmanAuthorityEntry::IsWellFormed(const FGuLiWingmanGroupHandle& ExpectedGroup) const
{
	return Wingman.IsValid() && Wingman.Flight.Group == ExpectedGroup
		&& LeaseOwnerPlayerGuid.IsValid() && LeaseEpoch != 0u;
}

bool FGuLiWingmanHealthEntry::IsWellFormed(const FGuLiWingmanGroupHandle& ExpectedGroup) const
{
	return Wingman.IsValid() && Wingman.Flight.Group == ExpectedGroup
		&& MaximumHealthPermille != 0u && CurrentHealthPermille <= MaximumHealthPermille;
}

uint64 FGuLiWingmanAcceptedBatch::ComputeStableHash() const
{
	uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
	AddGroup(Hash, Group);
	GuLiShipAbilityHash::AddUInt32(Hash, StateRef.MatchEpoch);
	GuLiShipAbilityHash::AddUInt32(Hash, StateRef.GroupGeneration);
	GuLiShipAbilityHash::AddUInt32(Hash, StateRef.AcceptedSequence);
	GuLiShipAbilityHash::AddUInt32(Hash, StateRef.ClientSimTick);
	GuLiShipAbilityHash::AddUInt32(Hash, CarrierSource.CanonicalEpoch);
	GuLiShipAbilityHash::AddUInt32(Hash, CarrierSource.MoveRevision);
	GuLiShipAbilityHash::AddUInt32(Hash, ConnectionGeneration);
	GuLiShipAbilityHash::AddUInt32(Hash, RosterRevision);
	GuLiShipAbilityHash::AddUInt32(Hash, FlightIndex);
	GuLiShipAbilityHash::AddUInt32(Hash, FrameSequence);
	GuLiShipAbilityHash::AddUInt32(Hash, BaseAcceptedSequence);
	AddDouble(Hash, CaptureEstimatedServerTimeSeconds);
	GuLiShipAbilityHash::AddUInt32(Hash, ValidationRevisions.NavSchemaRevision);
	GuLiShipAbilityHash::AddUInt64(Hash, ValidationRevisions.NavDataChecksum);
	GuLiShipAbilityHash::AddUInt32(Hash, ValidationRevisions.TuningRevision);
	GuLiShipAbilityHash::AddUInt32(Hash, ValidationRevisions.ObstacleRevision);
	GuLiShipAbilityHash::AddUInt32(Hash, AbilitySetRevision);
	GuLiShipAbilityHash::AddUInt32(Hash, FormationCommandRevision);
	GuLiShipAbilityHash::AddUInt64(Hash, FormationDefinitionChecksum);
	GuLiShipAbilityHash::AddUInt32(Hash, RebasedMemberMask);
	GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Samples.Num()));
	for (const FGuLiWingmanCandidateSample& Sample : Samples)
	{
		AddWingman(Hash, Sample.Wingman);
		AddVector(Hash, Sample.PositionCentimeters);
		AddVector(Hash, Sample.VelocityCentimetersPerSecond);
		AddVector(Hash, Sample.RotationCentiDegrees);
		GuLiShipAbilityHash::AddUInt32(Hash, Sample.FlightMode);
	}
	return GuLiShipAbilityHash::Finish(Hash);
}

void FGuLiWingmanAcceptedBatch::RefreshHash()
{
	StableHash = ComputeStableHash();
}

bool FGuLiWingmanAcceptedBatch::IsWellFormed() const
{
	if (!Group.IsValid() || !StateRef.IsValid() || StateRef.GroupGeneration != Group.GroupGeneration
		|| !CarrierSource.IsValid() || AbilitySetRevision == 0u || FormationCommandRevision == 0u
		|| FormationDefinitionChecksum == 0u || Samples.IsEmpty() || Samples.Num() > GULI_WINGMAN_GROUP_SIZE
		|| !FMath::IsFinite(ServerAcceptedTimeSeconds) || ServerAcceptedTimeSeconds < 0.0
		|| StableHash == 0u || StableHash != ComputeStableHash())
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
	if (UsesStrictFlightContract())
	{
		if (ConnectionGeneration == 0u || RosterRevision == 0u || FrameSequence == 0u
			|| !FMath::IsFinite(CaptureEstimatedServerTimeSeconds)
			|| CaptureEstimatedServerTimeSeconds < 0.0 || !ValidationRevisions.IsWellFormed()
			|| Samples.Num() > GULI_WINGMAN_MEMBERS_PER_FLIGHT)
		{
			return false;
		}
		for (const FGuLiWingmanCandidateSample& Sample : Samples)
		{
			if (Sample.Wingman.Flight.FlightIndex != FlightIndex)
			{
				return false;
			}
		}
		if ((RebasedMemberMask & ~static_cast<uint8>((1u << GULI_WINGMAN_MEMBERS_PER_FLIGHT) - 1u)) != 0u
			|| (RebasedMemberMask != 0u
				&& (RebasedMemberMask & static_cast<uint8>(RebasedMemberMask - 1u)) != 0u))
		{
			return false;
		}
		if (RebasedMemberMask != 0u)
		{
			uint8 RebasedMember = 0u;
			while ((RebasedMemberMask & (1u << RebasedMember)) == 0u)
			{
				++RebasedMember;
			}
			if (!Samples.ContainsByPredicate([RebasedMember](const FGuLiWingmanCandidateSample& Sample)
			{
				return Sample.Wingman.MemberIndex == RebasedMember;
			}))
			{
				return false;
			}
		}
	}
	else if (RebasedMemberMask != 0u)
	{
		return false;
	}
	return true;
}

const FGuLiWingmanCandidateSample* FGuLiWingmanAcceptedBatch::FindSample(
	const FGuLiWingmanHandle& Wingman) const
{
	return Samples.FindByPredicate([&Wingman](const FGuLiWingmanCandidateSample& Sample)
	{
		return Sample.Wingman == Wingman;
	});
}

bool FGuLiWingmanSimulationAcceptance::IsWellFormed() const
{
	if (ProtocolVersion != GULI_WINGMAN_PROTOCOL_VERSION || !Group.IsValid() || LeaseEpoch == 0u
		|| (FlightIndex != MAX_uint8 && FlightIndex >= GULI_WINGMAN_FLIGHT_COUNT)
		|| !FMath::IsFinite(AcceptedServerTimeSeconds) || AcceptedServerTimeSeconds < 0.0)
	{
		return false;
	}
	const bool bHasAcceptedBaseline = AcceptedSnapshotSequence != 0u;
	if (bHasAcceptedBaseline != RebaseBaseline.IsValid()
		|| (bHasAcceptedBaseline
			&& (RebaseBaseline.GroupGeneration != Group.GroupGeneration
				|| RebaseBaseline.AcceptedSequence != AcceptedSnapshotSequence)))
	{
		return false;
	}
	if (Disposition == EGuLiWingmanSubmissionDisposition::Accepted)
	{
		return RejectReason == EGuLiWingmanRejectReason::None && AcceptedSnapshotSequence != 0u
			&& ValidatedPayloadHash != 0u;
	}
	if (Disposition == EGuLiWingmanSubmissionDisposition::Pending)
	{
		return RejectReason == EGuLiWingmanRejectReason::None;
	}
	return RejectReason != EGuLiWingmanRejectReason::None;
}

bool FGuLiWingmanAtomicBatchAcceptance::IsWellFormed() const
{
	if (BatchId == 0u || !Group.IsValid() || LeaseEpoch == 0u || FrozenRosterRevision == 0u
		|| BaselineRevision == 0u || BaselineHash == 0u
		|| static_cast<uint8>(BatchKind) > static_cast<uint8>(EGuLiWingmanAtomicBatchKind::Takeover))
	{
		return false;
	}
	if (Disposition == EGuLiWingmanSubmissionDisposition::Accepted)
	{
		return RejectReason == EGuLiWingmanRejectReason::None && CommittedFlightMask != 0u
			&& AcceptedSnapshotSequences.Num() == GULI_WINGMAN_FLIGHT_COUNT
			&& BatchValidatedPayloadHash != 0u;
	}
	if (Disposition == EGuLiWingmanSubmissionDisposition::Pending)
	{
		return CommittedFlightMask == 0u && RejectReason == EGuLiWingmanRejectReason::None
			&& AcceptedSnapshotSequences.IsEmpty() && BatchValidatedPayloadHash == 0u;
	}
	return CommittedFlightMask == 0u && RejectReason != EGuLiWingmanRejectReason::None;
}

bool FGuLiWingmanCandidateResultWire::IsWellFormed() const
{
	if (CandidateSequence == 0u || !Acceptance.IsWellFormed())
	{
		return false;
	}
	if (Acceptance.Disposition != EGuLiWingmanSubmissionDisposition::Accepted)
	{
		return true;
	}
	return AcceptedBatch.IsWellFormed() && AcceptedBatch.Group == Acceptance.Group
		&& AcceptedBatch.FlightIndex == Acceptance.FlightIndex
		&& AcceptedBatch.RosterRevision == Acceptance.RosterRevision
		&& AcceptedBatch.FrameSequence == Acceptance.FrameSequence
		&& AcceptedBatch.StateRef.AcceptedSequence == Acceptance.AcceptedSnapshotSequence
		&& AcceptedBatch.ServerAcceptedTimeSeconds == Acceptance.AcceptedServerTimeSeconds
		&& AcceptedBatch.StableHash == Acceptance.ValidatedPayloadHash
		&& AcceptedBatch.StateRef.MatchEpoch == Acceptance.RebaseBaseline.MatchEpoch
		&& AcceptedBatch.StateRef.GroupGeneration == Acceptance.RebaseBaseline.GroupGeneration
		&& AcceptedBatch.StateRef.AcceptedSequence == Acceptance.RebaseBaseline.AcceptedSequence
		&& AcceptedBatch.StateRef.ClientSimTick == Acceptance.RebaseBaseline.ClientSimTick;
}

bool FGuLiWingmanCandidateResultWire::NetSerialize(
	FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar.SerializeIntPacked(CandidateSequence);
	bool bAllFieldsSucceeded = SerializeAcceptance(Ar, Map, Acceptance);
	uint8 bHasAcceptedBatch = Acceptance.Disposition == EGuLiWingmanSubmissionDisposition::Accepted ? 1u : 0u;
	Ar << bHasAcceptedBatch;
	if (Ar.IsLoading() && bHasAcceptedBatch > 1u)
	{
		bAllFieldsSucceeded = false;
	}
	if (bHasAcceptedBatch != 0u)
	{
		bAllFieldsSucceeded &= SerializeAcceptedBatch(Ar, Map, AcceptedBatch);
	}
	else if (Ar.IsLoading())
	{
		AcceptedBatch = FGuLiWingmanAcceptedBatch{};
	}
	bOutSuccess = bAllFieldsSucceeded && !Ar.IsError() && IsWellFormed();
	return true;
}

bool GuLiWingmanRelayWire::MakeValidatedCandidateResultCopy(
	const FGuLiWingmanCandidateResultWire& Source,
	FGuLiWingmanCandidateResultWire& OutCopy)
{
	OutCopy = FGuLiWingmanCandidateResultWire{};
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes, true);
	FGuLiWingmanCandidateResultWire Writable = Source;
	bool bSuccess = false;
	Writable.NetSerialize(Writer, nullptr, bSuccess);
	if (!bSuccess || Writer.IsError() || Bytes.IsEmpty())
	{
		return false;
	}
	FMemoryReader Reader(Bytes, true);
	bSuccess = false;
	OutCopy.NetSerialize(Reader, nullptr, bSuccess);
	if (!bSuccess || Reader.IsError() || !Reader.AtEnd() || !OutCopy.IsWellFormed())
	{
		OutCopy = FGuLiWingmanCandidateResultWire{};
		return false;
	}
	return true;
}

bool FGuLiWingmanLeaseState::IsWellFormed() const
{
	return Group.IsValid() && OwnerPlayerGuid.IsValid() && LeaseEpoch != 0u
		&& FMath::IsFinite(LifecycleChangedTimeSeconds) && LifecycleChangedTimeSeconds >= 0.0;
}

bool FGuLiGroupAbilityConfigAck::IsWellFormed() const
{
	return Group.IsValid() && LeaseEpoch != 0u && SnapshotRevision != 0u && SnapshotHash != 0u;
}

bool FGuLiWingmanBootstrapBundle::IsWellFormed() const
{
    if ((AttackStateHash != 0 && AttackStateHash != AttackState.ComputeStableHash())
        || (AttackStateHash == 0 && (AttackState.Revision != 0
			|| AttackState.Target.Target.IsValid()
			|| !AttackState.AutomaticTargets.IsEmpty()
			|| !AttackState.Checkpoints.IsEmpty()))
        || AttackState.Checkpoints.Num() > GULI_WINGMAN_GROUP_SIZE * GULI_MAX_WINGMAN_WEAPON_CHANNELS) return false;

	if (!Commit.IsWellFormed() || !AbilityConfig.IsWellFormed()
		|| Commit.Group.ShipInstanceId != AbilityConfig.ShipInstanceId
		|| Commit.Group.ShipGeneration != AbilityConfig.ShipGeneration
		|| Commit.Group.GroupGeneration != AbilityConfig.GroupGeneration
		|| Roster.Num() != GULI_WINGMAN_GROUP_SIZE || AuthorityMap.Num() != GULI_WINGMAN_GROUP_SIZE
		|| Health.Num() != GULI_WINGMAN_GROUP_SIZE || !HasUniqueWingmen(Commit.Group, Roster))
	{
		return false;
	}
	if (!AttackState.IsWellFormed(Commit.Group))
	{
		return false;
	}
	if (bActiveRosterRefresh && (bRequiresAtomicCandidateBatch || bHasTransferBaseline))
	{
		return false;
	}
	if (ConnectionGeneration != 0u
		&& (RosterRevision == 0u || !ValidationRevisions.IsWellFormed()
			|| !UploadRateGrant.IsWellFormed()
			|| UploadRateGrant.Group != Commit.Group
			|| UploadRateGrant.ConnectionGeneration != ConnectionGeneration
			|| UploadRateGrant.LeaseEpoch != AuthorityMap[0].LeaseEpoch
			|| AtomicBaselineRevision == 0u
			|| AtomicBaselineHash == 0u || RequiredFlightMask == 0u
			|| RequiredMemberMaskHash != GuLiWingmanRelayHash::RequiredMemberMasks(Roster)))
	{
		return false;
	}

	TSet<FGuLiWingmanHandle> RosterHandles;
	for (const FGuLiWingmanRosterEntry& Entry : Roster)
	{
		RosterHandles.Add(Entry.Wingman);
	}
	TSet<FGuLiWingmanHandle> AuthorityHandles;
	for (const FGuLiWingmanAuthorityEntry& Entry : AuthorityMap)
	{
		if (!Entry.IsWellFormed(Commit.Group) || !RosterHandles.Contains(Entry.Wingman)
			|| AuthorityHandles.Contains(Entry.Wingman))
		{
			return false;
		}
		AuthorityHandles.Add(Entry.Wingman);
	}
	TSet<FGuLiWingmanHandle> HealthHandles;
	for (const FGuLiWingmanHealthEntry& Entry : Health)
	{
		if (!Entry.IsWellFormed(Commit.Group) || !RosterHandles.Contains(Entry.Wingman)
			|| HealthHandles.Contains(Entry.Wingman))
		{
			return false;
		}
		HealthHandles.Add(Entry.Wingman);
	}
	TSet<FGuLiWingmanHandle> DeadHandles;
	for (const FGuLiWingmanHandle& Entry : Dead)
	{
		if (!RosterHandles.Contains(Entry) || DeadHandles.Contains(Entry))
		{
			return false;
		}
		DeadHandles.Add(Entry);
	}
	for (const FGuLiWingmanAcceptedBatch& Batch : AcceptedSnapshot)
	{
		if (!Batch.IsWellFormed() || Batch.Group != Commit.Group)
		{
			return false;
		}
	}

	const FGuLiWingmanBootstrapScopeState* RosterScope = Commit.FindScope(EGuLiWingmanBootstrapScope::Roster);
	const FGuLiWingmanBootstrapScopeState* AuthorityScope = Commit.FindScope(EGuLiWingmanBootstrapScope::AuthorityMap);
	const FGuLiWingmanBootstrapScopeState* HealthScope = Commit.FindScope(EGuLiWingmanBootstrapScope::Health);
	const FGuLiWingmanBootstrapScopeState* DeadScope = Commit.FindScope(EGuLiWingmanBootstrapScope::Dead);
	const FGuLiWingmanBootstrapScopeState* AcceptedScope = Commit.FindScope(EGuLiWingmanBootstrapScope::AcceptedSnapshot);
	const FGuLiWingmanBootstrapScopeState* AbilityScope = Commit.FindScope(EGuLiWingmanBootstrapScope::GroupAbilityConfig);
	if (!RosterScope || !AuthorityScope || !HealthScope || !DeadScope || !AcceptedScope || !AbilityScope
		|| !HasExactlyOneScope(Commit, EGuLiWingmanBootstrapScope::Roster, RosterScope->Revision,
			GuLiWingmanRelayHash::Roster(Roster))
		|| !HasExactlyOneScope(Commit, EGuLiWingmanBootstrapScope::AuthorityMap, AuthorityScope->Revision,
			GuLiWingmanRelayHash::AuthorityMap(AuthorityMap))
		|| !HasExactlyOneScope(Commit, EGuLiWingmanBootstrapScope::Health, HealthScope->Revision,
			GuLiWingmanRelayHash::Health(Health))
		|| !HasExactlyOneScope(Commit, EGuLiWingmanBootstrapScope::Dead, DeadScope->Revision,
			GuLiWingmanRelayHash::Dead(Dead))
		|| !HasExactlyOneScope(Commit, EGuLiWingmanBootstrapScope::AcceptedSnapshot, AcceptedScope->Revision,
			GuLiWingmanRelayHash::AcceptedSnapshot(AcceptedSnapshot))
		|| !HasExactlyOneScope(Commit, EGuLiWingmanBootstrapScope::GroupAbilityConfig, AbilityScope->Revision,
			AbilityConfig.SnapshotHash))
	{
		return false;
	}

	return !bHasTransferBaseline || (TransferBaseline.IsWellFormed()
		&& TransferBaseline.CutId == Commit.CutId && TransferBaseline.Group == Commit.Group);
}

FGuLiWingmanSubmissionResult FGuLiWingmanSubmissionResult::Accepted(const FGuLiWingmanAcceptedBatch& Batch)
{
	FGuLiWingmanSubmissionResult Result;
	Result.Disposition = EGuLiWingmanSubmissionDisposition::Accepted;
	Result.RejectReason = EGuLiWingmanRejectReason::None;
	Result.Sequence = Batch.StateRef.AcceptedSequence;
	Result.AcceptedBatch = Batch;
	Result.Acceptance.Group = Batch.Group;
	Result.Acceptance.FlightIndex = Batch.FlightIndex;
	Result.Acceptance.RosterRevision = Batch.RosterRevision;
	Result.Acceptance.FrameSequence = Batch.FrameSequence;
	Result.Acceptance.Disposition = Result.Disposition;
	Result.Acceptance.RejectReason = Result.RejectReason;
	Result.Acceptance.AcceptedSnapshotSequence = Batch.StateRef.AcceptedSequence;
	Result.Acceptance.AcceptedServerTimeSeconds = Batch.ServerAcceptedTimeSeconds;
	Result.Acceptance.ValidatedPayloadHash = Batch.StableHash;
	Result.Acceptance.RebaseBaseline = Batch.StateRef;
	return Result;
}

EGuLiAtomicCandidateAssemblyDisposition FGuLiWingmanAtomicCandidateAssembler::SubmitFragment(
	const FGuLiWingmanAtomicCandidateBatchFragment& Fragment,
	const double NowSeconds,
	TArray<FGuLiWingmanCandidateBatch>& OutFlights,
	EGuLiWingmanRejectReason& OutRejectReason)
{
	OutFlights.Reset();
	OutRejectReason = EGuLiWingmanRejectReason::None;
	if (!FMath::IsFinite(NowSeconds) || NowSeconds < 0.0 || !Fragment.IsWellFormed())
	{
		OutRejectReason = EGuLiWingmanRejectReason::AtomicBatchFragmentInvalid;
		return EGuLiAtomicCandidateAssemblyDisposition::Rejected;
	}
	if (IsPending() && NowSeconds >= DeadlineSeconds)
	{
		Reset();
		OutRejectReason = EGuLiWingmanRejectReason::AtomicBatchExpired;
		return EGuLiAtomicCandidateAssemblyDisposition::Rejected;
	}
	if (!IsPending())
	{
		Header = Fragment.Header;
		Fragments.SetNum(Header.FragmentCount);
		ReceivedFragments.Init(false, Header.FragmentCount);
		DeadlineSeconds = NowSeconds + GULI_WINGMAN_ATOMIC_BATCH_ASSEMBLY_TIMEOUT_SECONDS;
		ReceivedPayloadBytes = 0u;
	}
	else if (!AtomicHeadersEqual(Header, Fragment.Header))
	{
		OutRejectReason = EGuLiWingmanRejectReason::AtomicBatchConflict;
		return EGuLiAtomicCandidateAssemblyDisposition::Rejected;
	}

	if (ReceivedFragments[Fragment.FragmentIndex])
	{
		OutRejectReason = EGuLiWingmanRejectReason::Duplicate;
		return EGuLiAtomicCandidateAssemblyDisposition::Rejected;
	}
	const uint32 FragmentBytes = Fragment.EstimatePayloadBytes();
	if (FragmentBytes == MAX_uint32 || FragmentBytes > GULI_WINGMAN_ATOMIC_BATCH_MAX_BYTES
		|| ReceivedPayloadBytes > GULI_WINGMAN_ATOMIC_BATCH_MAX_BYTES - FragmentBytes)
	{
		Reset();
		OutRejectReason = EGuLiWingmanRejectReason::AtomicBatchTooLarge;
		return EGuLiAtomicCandidateAssemblyDisposition::Rejected;
	}
	Fragments[Fragment.FragmentIndex] = Fragment;
	ReceivedFragments[Fragment.FragmentIndex] = true;
	ReceivedPayloadBytes += FragmentBytes;
	if (ReceivedFragments.Find(false) != INDEX_NONE)
	{
		return EGuLiAtomicCandidateAssemblyDisposition::Pending;
	}

	for (const FGuLiWingmanAtomicCandidateBatchFragment& Stored : Fragments)
	{
		OutFlights.Append(Stored.Flights);
	}
	OutFlights.Sort([](const FGuLiWingmanCandidateBatch& Lhs, const FGuLiWingmanCandidateBatch& Rhs)
	{
		return Lhs.FlightIndex < Rhs.FlightIndex;
	});
	uint8 AssembledFlightMask = 0u;
	for (const FGuLiWingmanCandidateBatch& Flight : OutFlights)
	{
		const uint8 Bit = static_cast<uint8>(1u << Flight.FlightIndex);
		if ((AssembledFlightMask & Bit) != 0u)
		{
			Reset();
			OutFlights.Reset();
			OutRejectReason = EGuLiWingmanRejectReason::WrongFlightCoverage;
			return EGuLiAtomicCandidateAssemblyDisposition::Rejected;
		}
		AssembledFlightMask |= Bit;
	}
	if (ReceivedPayloadBytes != Header.BatchPayloadBytes
		|| AssembledFlightMask != Header.IncludedFlightMask
		|| GuLiWingmanRelayHash::CandidatePayloads(OutFlights) != Header.BatchPayloadHash)
	{
		Reset();
		OutFlights.Reset();
		OutRejectReason = EGuLiWingmanRejectReason::AtomicBatchHashMismatch;
		return EGuLiAtomicCandidateAssemblyDisposition::Rejected;
	}
	Reset();
	return EGuLiAtomicCandidateAssemblyDisposition::Complete;
}

bool FGuLiWingmanAtomicCandidateAssembler::Expire(
	const double NowSeconds, FGuLiWingmanAtomicBatchAcceptance& OutExpired)
{
	if (!IsPending() || !FMath::IsFinite(NowSeconds) || NowSeconds < DeadlineSeconds)
	{
		return false;
	}
	OutExpired = FGuLiWingmanAtomicBatchAcceptance{};
	OutExpired.BatchId = Header.BatchId;
	OutExpired.BatchKind = Header.BatchKind;
	OutExpired.Group = Header.Group;
	OutExpired.LeaseEpoch = Header.LeaseEpoch;
	OutExpired.FrozenRosterRevision = Header.FrozenRosterRevision;
	OutExpired.BaselineRevision = Header.BaselineRevision;
	OutExpired.BaselineHash = Header.BaselineHash;
	OutExpired.Disposition = EGuLiWingmanSubmissionDisposition::Rejected;
	OutExpired.RejectReason = EGuLiWingmanRejectReason::AtomicBatchExpired;
	Reset();
	return true;
}

void FGuLiWingmanAtomicCandidateAssembler::Reset()
{
	Header = FGuLiWingmanAtomicCandidateBatchHeader{};
	Fragments.Reset();
	ReceivedFragments.Reset();
	DeadlineSeconds = 0.0;
	ReceivedPayloadBytes = 0u;
}

FGuLiWingmanSubmissionResult FGuLiWingmanSubmissionResult::AcceptedSequence(const uint32 Sequence)
{
	FGuLiWingmanSubmissionResult Result;
	Result.Disposition = EGuLiWingmanSubmissionDisposition::Accepted;
	Result.RejectReason = EGuLiWingmanRejectReason::None;
	Result.Sequence = Sequence;
	return Result;
}

FGuLiWingmanSubmissionResult FGuLiWingmanSubmissionResult::Pending(const uint32 CandidateSequence)
{
	FGuLiWingmanSubmissionResult Result;
	Result.Disposition = EGuLiWingmanSubmissionDisposition::Pending;
	Result.RejectReason = EGuLiWingmanRejectReason::None;
	Result.Sequence = CandidateSequence;
	return Result;
}

FGuLiWingmanSubmissionResult FGuLiWingmanSubmissionResult::Rejected(
	const EGuLiWingmanRejectReason Reason, const uint32 Sequence)
{
	FGuLiWingmanSubmissionResult Result;
	Result.Disposition = EGuLiWingmanSubmissionDisposition::Rejected;
	Result.RejectReason = Reason;
	Result.Sequence = Sequence;
	return Result;
}

void FGuLiWingmanTokenBucket::Reset(
	const double NowSeconds, const double InCapacity, const double InTokensPerSecond)
{
	Capacity = FMath::Max(0.0, InCapacity);
	TokensPerSecond = FMath::Max(0.0, InTokensPerSecond);
	AvailableTokens = Capacity;
	LastUpdateSeconds = FMath::IsFinite(NowSeconds) ? NowSeconds : 0.0;
	bInitialized = FMath::IsFinite(NowSeconds) && Capacity > 0.0;
}

bool FGuLiWingmanTokenBucket::Consume(const double NowSeconds, const double TokenCount)
{
	if (!bInitialized || !FMath::IsFinite(NowSeconds) || !FMath::IsFinite(TokenCount)
		|| NowSeconds < LastUpdateSeconds || TokenCount <= 0.0 || TokenCount > Capacity)
	{
		return false;
	}
	const double ElapsedSeconds = NowSeconds - LastUpdateSeconds;
	AvailableTokens = FMath::Min(Capacity, AvailableTokens + ElapsedSeconds * TokensPerSecond);
	LastUpdateSeconds = NowSeconds;
	if (AvailableTokens + UE_DOUBLE_SMALL_NUMBER < TokenCount)
	{
		return false;
	}
	AvailableTokens -= TokenCount;
	return true;
}

namespace GuLiWingmanRelayHash
{
	uint64 Roster(const TArray<FGuLiWingmanRosterEntry>& Entries)
	{
		uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Entries.Num()));
		for (const FGuLiWingmanRosterEntry& Entry : Entries)
		{
			AddWingman(Hash, Entry.Wingman);
			GuLiShipAbilityHash::AddString(Hash, Entry.WingmanTypeId.ToString());
			GuLiShipAbilityHash::AddBool(Hash, Entry.bDead);
		}
		return GuLiShipAbilityHash::Finish(Hash);
	}

	uint64 AuthorityMap(const TArray<FGuLiWingmanAuthorityEntry>& Entries)
	{
		uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Entries.Num()));
		for (const FGuLiWingmanAuthorityEntry& Entry : Entries)
		{
			AddWingman(Hash, Entry.Wingman);
			AddGuid(Hash, Entry.LeaseOwnerPlayerGuid);
			GuLiShipAbilityHash::AddUInt32(Hash, Entry.LeaseEpoch);
		}
		return GuLiShipAbilityHash::Finish(Hash);
	}

	uint64 Health(const TArray<FGuLiWingmanHealthEntry>& Entries)
	{
		uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Entries.Num()));
		for (const FGuLiWingmanHealthEntry& Entry : Entries)
		{
			AddWingman(Hash, Entry.Wingman);
			GuLiShipAbilityHash::AddUInt32(Hash, Entry.CurrentHealthPermille);
			GuLiShipAbilityHash::AddUInt32(Hash, Entry.MaximumHealthPermille);
		}
		return GuLiShipAbilityHash::Finish(Hash);
	}

	uint64 Dead(const TArray<FGuLiWingmanHandle>& Entries)
	{
		uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Entries.Num()));
		for (const FGuLiWingmanHandle& Entry : Entries)
		{
			AddWingman(Hash, Entry);
		}
		return GuLiShipAbilityHash::Finish(Hash);
	}

	uint64 AcceptedSnapshot(const TArray<FGuLiWingmanAcceptedBatch>& Entries)
	{
		uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Entries.Num()));
		for (const FGuLiWingmanAcceptedBatch& Entry : Entries)
		{
			GuLiShipAbilityHash::AddUInt64(Hash, Entry.StableHash);
		}
		return GuLiShipAbilityHash::Finish(Hash);
	}

	uint64 CandidatePayloads(const TArray<FGuLiWingmanCandidateBatch>& Entries)
	{
		TArray<const FGuLiWingmanCandidateBatch*, TInlineAllocator<GULI_WINGMAN_FLIGHT_COUNT>> Sorted;
		for (const FGuLiWingmanCandidateBatch& Entry : Entries)
		{
			Sorted.Add(&Entry);
		}
		Sorted.Sort([](const FGuLiWingmanCandidateBatch& Lhs, const FGuLiWingmanCandidateBatch& Rhs)
		{
			return Lhs.FlightIndex < Rhs.FlightIndex;
		});
		uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
		GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Sorted.Num()));
		for (const FGuLiWingmanCandidateBatch* Entry : Sorted)
		{
			GuLiShipAbilityHash::AddUInt32(Hash, Entry->FlightIndex);
			GuLiShipAbilityHash::AddUInt64(Hash, Entry->ComputeStablePayloadHash());
		}
		return GuLiShipAbilityHash::Finish(Hash);
	}

	uint64 RequiredMemberMasks(const TArray<FGuLiWingmanRosterEntry>& Entries)
	{
		uint8 Masks[GULI_WINGMAN_FLIGHT_COUNT] = {};
		for (const FGuLiWingmanRosterEntry& Entry : Entries)
		{
			if (Entry.Wingman.IsValid() && !Entry.bDead)
			{
				Masks[Entry.Wingman.Flight.FlightIndex] |= static_cast<uint8>(1u << Entry.Wingman.MemberIndex);
			}
		}
		uint64 Hash = GuLiShipAbilityHash::OffsetBasis;
		for (const uint8 Mask : Masks)
		{
			GuLiShipAbilityHash::AddUInt32(Hash, Mask);
		}
		return GuLiShipAbilityHash::Finish(Hash);
	}
}
