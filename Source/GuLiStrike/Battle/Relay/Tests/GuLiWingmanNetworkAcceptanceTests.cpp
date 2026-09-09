// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Relay/GuLiWingmanRelayServer.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace GuLiWingmanNetworkAcceptanceTests
{
	constexpr double ConfiguredOneWayLatencySeconds = 0.25;
	constexpr double ConfiguredRttSeconds = ConfiguredOneWayLatencySeconds * 2.0;

	FGuLiWingmanGroupHandle MakeGroup(const uint32 Seed)
	{
		FGuLiWingmanGroupHandle Group;
		Group.ShipInstanceId = FGuid(0x81000000u + Seed, 0x82000000u, 0x83000000u, 0x84000000u);
		Group.ShipGeneration = 7u;
		Group.GroupGeneration = 11u;
		return Group;
	}

	FGuLiGroupAbilityConfigSnapshot MakeAbilityConfig(const FGuLiWingmanGroupHandle& Group)
	{
		FGuLiGroupAbilityConfigSnapshot Config;
		Config.ShipInstanceId = Group.ShipInstanceId;
		Config.MatchEpoch = 59u;
		Config.Team = EGuLiTeam::Red;
		Config.OwnerPlayerGuid = FGuid(101u, 102u, 103u, 104u);
		Config.WingmanTypeId = TEXT("NetworkAcceptanceTestWingman");
		Config.ShipGeneration = Group.ShipGeneration;
		Config.GroupGeneration = Group.GroupGeneration;
		Config.AbilitySetRevision = 17u;
		Config.LoadoutRevision = 17u;
		Config.SnapshotRevision = 19u;
		Config.bGroupAbilitiesValid = true;
		Config.FormationAbilityId = TAG_GuLi_ShipAbility_Formation_DoubleRing;
		Config.BasicWeaponAbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
		Config.MissileAbilityId = TAG_GuLi_ShipAbility_Weapon_Missile_Salvo;
		Config.FormationDefinitionRevision = 23u;
		Config.FormationDefinitionChecksum = 0x1111222233334444ull;
		Config.BasicWeaponDefinitionRevision = 29u;
		Config.BasicWeaponDefinitionChecksum = 0x2222333344445555ull;
		Config.MissileDefinitionRevision = 31u;
		Config.MissileDefinitionChecksum = 0x3333444455556666ull;
		Config.FormationCommandRevision = 37u;
		Config.EffectiveClientSimTick = 100u;
		Config.RefreshHash();
		return Config;
	}

	FGuLiWingmanRelayValidationRevisions MakeValidationRevisions()
	{
		FGuLiWingmanRelayValidationRevisions Revisions;
		Revisions.NavSchemaRevision = 41u;
		Revisions.NavDataChecksum = 0xfedcba9876543210ull;
		Revisions.TuningRevision = 43u;
		Revisions.ObstacleRevision = 47u;
		return Revisions;
	}

	FGuLiCarrierSourceResolver FoundCarrier()
	{
		return [](const FGuLiCarrierSourceRef&, FGuLiRelayCarrierState& OutState)
		{
			OutState.Transform = FTransform::Identity;
			OutState.Velocity = FVector::ZeroVector;
			OutState.ServerWorldTimeSeconds = 0.0;
			return EGuLiRelayCarrierLookupResult::Found;
		};
	}

	FGuLiCandidateWorldValidator PermitWorld(int32* ValidatedSegmentCount = nullptr)
	{
		return [ValidatedSegmentCount](const FGuLiWingmanCandidateWorldValidationContext& Context)
		{
			if (ValidatedSegmentCount)
			{
				*ValidatedSegmentCount += Context.Segments.Num();
			}
			return Context.IsWellFormed()
				? EGuLiWingmanRejectReason::None
				: EGuLiWingmanRejectReason::InvalidIdentity;
		};
	}

	bool RoundTripCandidate(
		const FGuLiWingmanCandidateBatch& Source,
		FGuLiWingmanCandidateBatch& OutCopy,
		int32* OutWireBytes = nullptr)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		FGuLiWingmanCandidateBatch Writable = Source;
		bool bWriteSuccess = false;
		const bool bWriteHandled = Writable.NetSerialize(Writer, nullptr, bWriteSuccess);
		Writer.Close();
		if (!bWriteHandled || !bWriteSuccess || Writer.IsError() || Bytes.IsEmpty())
		{
			return false;
		}
		if (OutWireBytes)
		{
			*OutWireBytes = Bytes.Num();
		}

		OutCopy = FGuLiWingmanCandidateBatch{};
		FMemoryReader Reader(Bytes, true);
		bool bReadSuccess = false;
		const bool bReadHandled = OutCopy.NetSerialize(Reader, nullptr, bReadSuccess);
		const bool bConsumedExactly = Reader.Tell() == Bytes.Num();
		Reader.Close();
		return bReadHandled && bReadSuccess && !Reader.IsError() && bConsumedExactly
			&& OutCopy.IsWellFormed();
	}

	bool SerializeAtomicHeader(FArchive& Ar, FGuLiWingmanAtomicCandidateBatchHeader& Header)
	{
		Ar << Header.ProtocolVersion;
		Ar << Header.BatchId;
		uint8 BatchKind = static_cast<uint8>(Header.BatchKind);
		Ar << BatchKind;
		if (Ar.IsLoading())
		{
			Header.BatchKind = static_cast<EGuLiWingmanAtomicBatchKind>(BatchKind);
		}
		bool bGroupSuccess = false;
		Header.Group.NetSerialize(Ar, nullptr, bGroupSuccess);
		Ar << Header.ConnectionGeneration;
		Ar << Header.LeaseEpoch;
		Ar << Header.FrozenRosterRevision;
		Ar << Header.FrozenRequiredFlightMask;
		Ar << Header.FrozenRequiredMemberMaskHash;
		Ar << Header.BaselineRevision;
		Ar << Header.BaselineHash;
		Ar << Header.IncludedFlightMask;
		Ar << Header.ClientBatchStartTick;
		Ar << Header.BatchPayloadBytes;
		Ar << Header.FragmentCount;
		Ar << Header.BatchPayloadHash;
		uint8 AtomicCommit = Header.bAtomicCommit ? 1u : 0u;
		Ar << AtomicCommit;
		if (Ar.IsLoading())
		{
			Header.bAtomicCommit = AtomicCommit != 0u;
		}
		return bGroupSuccess && !Ar.IsError();
	}

	bool RoundTripAtomicFragment(
		const FGuLiWingmanAtomicCandidateBatchFragment& Source,
		FGuLiWingmanAtomicCandidateBatchFragment& OutCopy)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		FGuLiWingmanAtomicCandidateBatchHeader Header = Source.Header;
		bool bSuccess = SerializeAtomicHeader(Writer, Header);
		uint8 FragmentIndex = Source.FragmentIndex;
		uint8 FlightCount = static_cast<uint8>(Source.Flights.Num());
		Writer << FragmentIndex;
		Writer << FlightCount;
		for (const FGuLiWingmanCandidateBatch& Flight : Source.Flights)
		{
			FGuLiWingmanCandidateBatch Writable = Flight;
			bool bFlightSuccess = false;
			Writable.NetSerialize(Writer, nullptr, bFlightSuccess);
			bSuccess &= bFlightSuccess;
		}
		Writer.Close();
		if (!bSuccess || Writer.IsError() || Bytes.IsEmpty())
		{
			return false;
		}

		OutCopy = FGuLiWingmanAtomicCandidateBatchFragment{};
		FMemoryReader Reader(Bytes, true);
		bSuccess = SerializeAtomicHeader(Reader, OutCopy.Header);
		Reader << OutCopy.FragmentIndex;
		Reader << FlightCount;
		if (FlightCount == 0u || FlightCount > GULI_WINGMAN_FLIGHT_COUNT)
		{
			return false;
		}
		OutCopy.Flights.SetNum(FlightCount);
		for (FGuLiWingmanCandidateBatch& Flight : OutCopy.Flights)
		{
			bool bFlightSuccess = false;
			Flight.NetSerialize(Reader, nullptr, bFlightSuccess);
			bSuccess &= bFlightSuccess;
		}
		const bool bConsumedExactly = Reader.Tell() == Bytes.Num();
		Reader.Close();
		return bSuccess && !Reader.IsError() && bConsumedExactly && OutCopy.IsWellFormed();
	}

	bool RoundTripBootstrapCommit(
		const FGuLiWingmanBootstrapCommit& Source,
		FGuLiWingmanBootstrapCommit& OutCopy)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		uint32 ProtocolVersion = Source.ProtocolVersion;
		uint64 CutId = Source.CutId;
		FGuLiWingmanGroupHandle Group = Source.Group;
		uint8 ScopeCount = static_cast<uint8>(Source.Scopes.Num());
		Writer << ProtocolVersion;
		Writer << CutId;
		bool bSuccess = false;
		Group.NetSerialize(Writer, nullptr, bSuccess);
		Writer << ScopeCount;
		for (const FGuLiWingmanBootstrapScopeState& Scope : Source.Scopes)
		{
			uint8 ScopeValue = static_cast<uint8>(Scope.Scope);
			uint32 Revision = Scope.Revision;
			uint64 Hash = Scope.Hash;
			uint16 ChunkCount = Scope.ChunkCount;
			Writer << ScopeValue;
			Writer << Revision;
			Writer << Hash;
			Writer << ChunkCount;
		}
		Writer.Close();
		if (!bSuccess || Writer.IsError())
		{
			return false;
		}

		OutCopy = FGuLiWingmanBootstrapCommit{};
		FMemoryReader Reader(Bytes, true);
		Reader << OutCopy.ProtocolVersion;
		Reader << OutCopy.CutId;
		OutCopy.Group.NetSerialize(Reader, nullptr, bSuccess);
		Reader << ScopeCount;
		if (ScopeCount != static_cast<uint8>(EGuLiWingmanBootstrapScope::Count))
		{
			return false;
		}
		OutCopy.Scopes.SetNum(ScopeCount);
		for (FGuLiWingmanBootstrapScopeState& Scope : OutCopy.Scopes)
		{
			uint8 ScopeValue = 0u;
			Reader << ScopeValue;
			Scope.Scope = static_cast<EGuLiWingmanBootstrapScope>(ScopeValue);
			Reader << Scope.Revision;
			Reader << Scope.Hash;
			Reader << Scope.ChunkCount;
		}
		const bool bConsumedExactly = Reader.Tell() == Bytes.Num();
		Reader.Close();
		return bSuccess && !Reader.IsError() && bConsumedExactly && OutCopy.IsWellFormed();
	}

	bool RoundTripTransferBaseline(
		const FGuLiWingmanTransferBaseline& Source,
		FGuLiWingmanTransferBaseline& OutCopy)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		FGuLiWingmanTransferBaseline Writable = Source;
		Writer << Writable.ProtocolVersion;
		Writer << Writable.CutId;
		bool bSuccess = false;
		Writable.Group.NetSerialize(Writer, nullptr, bSuccess);
		Writer << Writable.LeaseEpoch;
		Writer << Writable.AbilityConfigRevision;
		Writer << Writable.AbilityConfigHash;
		Writer << Writable.AcceptedSnapshotRevision;
		Writer << Writable.AcceptedSnapshotHash;
		Writer.Close();
		if (!bSuccess || Writer.IsError())
		{
			return false;
		}

		OutCopy = FGuLiWingmanTransferBaseline{};
		FMemoryReader Reader(Bytes, true);
		Reader << OutCopy.ProtocolVersion;
		Reader << OutCopy.CutId;
		OutCopy.Group.NetSerialize(Reader, nullptr, bSuccess);
		Reader << OutCopy.LeaseEpoch;
		Reader << OutCopy.AbilityConfigRevision;
		Reader << OutCopy.AbilityConfigHash;
		Reader << OutCopy.AcceptedSnapshotRevision;
		Reader << OutCopy.AcceptedSnapshotHash;
		const bool bConsumedExactly = Reader.Tell() == Bytes.Num();
		Reader.Close();
		return bSuccess && !Reader.IsError() && bConsumedExactly && OutCopy.IsWellFormed();
	}

	bool RoundTripUploadGrant(
		const FGuLiWingmanUploadRateGrant& Source,
		FGuLiWingmanUploadRateGrant& OutCopy)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes, true);
		FGuLiWingmanUploadRateGrant Writable = Source;
		Writer << Writable.ProtocolVersion;
		bool bSuccess = false;
		Writable.Group.NetSerialize(Writer, nullptr, bSuccess);
		Writer << Writable.ConnectionGeneration;
		Writer << Writable.LeaseEpoch;
		uint8 RateClass = static_cast<uint8>(Writable.RateClass);
		Writer << RateClass;
		Writer << Writable.GrantRevision;
		Writer << Writable.EffectiveClientSimTick;
		Writer << Writable.ExpiryServerTimeSeconds;
		uint8 Reason = static_cast<uint8>(Writable.Reason);
		Writer << Reason;
		Writer.Close();
		if (!bSuccess || Writer.IsError())
		{
			return false;
		}

		OutCopy = FGuLiWingmanUploadRateGrant{};
		FMemoryReader Reader(Bytes, true);
		Reader << OutCopy.ProtocolVersion;
		OutCopy.Group.NetSerialize(Reader, nullptr, bSuccess);
		Reader << OutCopy.ConnectionGeneration;
		Reader << OutCopy.LeaseEpoch;
		Reader << RateClass;
		OutCopy.RateClass = static_cast<EGuLiWingmanUploadRateClass>(RateClass);
		Reader << OutCopy.GrantRevision;
		Reader << OutCopy.EffectiveClientSimTick;
		Reader << OutCopy.ExpiryServerTimeSeconds;
		Reader << Reason;
		OutCopy.Reason = static_cast<EGuLiWingmanUploadRateGrantReason>(Reason);
		const bool bConsumedExactly = Reader.Tell() == Bytes.Num();
		Reader.Close();
		return bSuccess && !Reader.IsError() && bConsumedExactly && OutCopy.IsWellFormed();
	}

	struct FRelayMutationSnapshot
	{
		int32 AcceptedHistoryCount = 0;
		uint32 LastAcceptedCandidateSequence = 0u;
		uint64 LastAcceptedHash = 0u;
		double LastAcceptedServerTimeSeconds = 0.0;
		uint64 ServerMovementWrites = 0u;
		TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> FlightSequences{};
		TStaticArray<uint32, GULI_WINGMAN_FLIGHT_COUNT> FlightFrames{};
		TStaticArray<double, GULI_WINGMAN_FLIGHT_COUNT> FlightLastValidTimes{};

		static FRelayMutationSnapshot Capture(const FGuLiWingmanRelayServer& Relay)
		{
			FRelayMutationSnapshot Snapshot;
			Snapshot.AcceptedHistoryCount = Relay.GetAcceptedHistory().Num();
			Snapshot.LastAcceptedCandidateSequence = Relay.GetLastAcceptedCandidateSequence();
			Snapshot.ServerMovementWrites = Relay.GetServerWingmanMovementWriteCount();
			if (!Relay.GetAcceptedHistory().IsEmpty())
			{
				Snapshot.LastAcceptedHash = Relay.GetAcceptedHistory().Last().StableHash;
				Snapshot.LastAcceptedServerTimeSeconds =
					Relay.GetAcceptedHistory().Last().ServerAcceptedTimeSeconds;
			}
			for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
			{
				Snapshot.FlightSequences[FlightIndex] = Relay.GetAcceptedSequenceForFlight(FlightIndex);
				Snapshot.FlightFrames[FlightIndex] = Relay.GetLastAcceptedFrameForFlight(FlightIndex);
				Snapshot.FlightLastValidTimes[FlightIndex] =
					Relay.GetLastValidCandidateTimeForFlight(FlightIndex);
			}
			return Snapshot;
		}

		bool Equals(const FRelayMutationSnapshot& Other) const
		{
			if (AcceptedHistoryCount != Other.AcceptedHistoryCount
				|| LastAcceptedCandidateSequence != Other.LastAcceptedCandidateSequence
				|| LastAcceptedHash != Other.LastAcceptedHash
				|| LastAcceptedServerTimeSeconds != Other.LastAcceptedServerTimeSeconds
				|| ServerMovementWrites != Other.ServerMovementWrites)
			{
				return false;
			}
			for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
			{
				if (FlightSequences[FlightIndex] != Other.FlightSequences[FlightIndex]
					|| FlightFrames[FlightIndex] != Other.FlightFrames[FlightIndex]
					|| FlightLastValidTimes[FlightIndex] != Other.FlightLastValidTimes[FlightIndex])
				{
					return false;
				}
			}
			return true;
		}
	};

	struct FFixture
	{
		FGuLiWingmanRelayServer Relay;
		FGuid Owner = FGuid(101u, 102u, 103u, 104u);
		FGuid Backup = FGuid(201u, 202u, 203u, 204u);
		FGuid Third = FGuid(301u, 302u, 303u, 304u);
		FGuLiWingmanBootstrapBundle Bootstrap;

		bool Initialize(FAutomationTestBase& Test, const uint32 Seed = 1u)
		{
			FGuLiWingmanRelayTuning Tuning;
			Tuning.CandidateBucketCapacity = 256.0;
			Tuning.CandidateTokensPerSecond = 256.0;
			const FGuLiWingmanGroupHandle Group = MakeGroup(Seed);
			if (!Test.TestTrue(TEXT("Relay initializes"), Relay.InitializeGroup(
				59u, Group, Owner, Backup, MakeAbilityConfig(Group), 0.0, Tuning)))
			{
				return false;
			}
			if (!Test.TestTrue(TEXT("Strict per-Flight contract configures"),
				Relay.ConfigureStrictFlightContract(61u, MakeValidationRevisions(), 0.0)))
			{
				return false;
			}
			return Test.TestTrue(TEXT("Initial six-scope Bootstrap builds"),
				Relay.BuildBootstrap(Bootstrap));
		}

		FGuLiWingmanCandidateBatch MakeFlight(
			const uint8 FlightIndex,
			const uint32 CandidateSequence,
			const uint32 FrameSequence,
			const uint32 BaseAcceptedSequence,
			const uint32 ClientSimTick,
			const double CaptureTimeSeconds,
			const bool bAddTrail = false) const
		{
			FGuLiWingmanCandidateBatch Candidate;
			Candidate.MatchEpoch = Relay.GetMatchEpoch();
			Candidate.ConnectionGeneration = Relay.GetConnectionGeneration();
			Candidate.Group = Relay.GetLeaseState().Group;
			Candidate.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
			Candidate.RosterRevision = Relay.GetRosterRevision();
			Candidate.FlightIndex = FlightIndex;
			Candidate.RequestedRateClass = EGuLiWingmanUploadRateClass::Cruise5Hz;
			Candidate.ObservedGrantRevision = Relay.GetUploadRateGrant().GrantRevision;
			Candidate.CandidateSequence = CandidateSequence;
			Candidate.FrameSequence = FrameSequence;
			Candidate.BaseAcceptedSequence = BaseAcceptedSequence;
			Candidate.ClientSimTick = ClientSimTick;
			Candidate.CaptureEstimatedServerTimeSeconds = CaptureTimeSeconds;
			Candidate.NavSchemaRevision = Relay.GetValidationRevisions().NavSchemaRevision;
			Candidate.NavDataChecksum = Relay.GetValidationRevisions().NavDataChecksum;
			Candidate.TuningRevision = Relay.GetValidationRevisions().TuningRevision;
			Candidate.ObstacleRevision = Relay.GetValidationRevisions().ObstacleRevision;
			Candidate.CarrierSource.CanonicalEpoch = 67u;
			Candidate.CarrierSource.MoveRevision = CandidateSequence;
			Candidate.AbilitySetRevision = Relay.GetAbilityConfig().AbilitySetRevision;
			Candidate.FormationCommandRevision = Relay.GetAbilityConfig().FormationCommandRevision;
			Candidate.FormationDefinitionChecksum = Relay.GetAbilityConfig().FormationDefinitionChecksum;
			for (const FGuLiWingmanRosterEntry& Entry : Relay.GetRoster())
			{
				if (Entry.bDead || Entry.Wingman.Flight.FlightIndex != FlightIndex)
				{
					continue;
				}
				FGuLiWingmanCandidateSample& Sample = Candidate.Samples.AddDefaulted_GetRef();
				Sample.Wingman = Entry.Wingman;
				Sample.PositionCentimeters = FIntVector(
					static_cast<int32>(FlightIndex) * 1000,
					static_cast<int32>(Entry.Wingman.MemberIndex) * 100,
					1000);
				Sample.VelocityCentimetersPerSecond = FIntVector::ZeroValue;
				Sample.RotationCentiDegrees = FIntVector::ZeroValue;
				Sample.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Follow);
				Candidate.RequiredMemberMask |= static_cast<uint8>(1u << Entry.Wingman.MemberIndex);
			}
			if (bAddTrail)
			{
				FGuLiWingmanCandidateTrailSample& Trail = Candidate.TrailSamples.AddDefaulted_GetRef();
				Trail.ClientSimTick = ClientSimTick - 1u;
				Trail.CaptureEstimatedServerTimeSeconds = CaptureTimeSeconds - 0.05;
				Trail.CarrierSource = Candidate.CarrierSource;
				Trail.Samples = Candidate.Samples;
			}
			return Candidate;
		}

		TArray<FGuLiWingmanCandidateBatch> MakeAllFlights(
			const uint32 FirstCandidateSequence,
			const uint32 FrameSequence,
			const uint32 ClientSimTick,
			const double CaptureTimeSeconds) const
		{
			TArray<FGuLiWingmanCandidateBatch> Flights;
			for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
			{
				Flights.Add(MakeFlight(
					FlightIndex,
					FirstCandidateSequence + FlightIndex,
					FrameSequence,
					Relay.GetAcceptedSequenceForFlight(FlightIndex),
					ClientSimTick,
					CaptureTimeSeconds));
			}
			return Flights;
		}

		TArray<FGuLiWingmanAtomicCandidateBatchFragment> MakeAtomicFragments(
			const FGuLiWingmanBootstrapBundle& Cut,
			const TArray<FGuLiWingmanCandidateBatch>& Flights,
			const uint64 BatchId,
			const bool bSplit) const
		{
			FGuLiWingmanAtomicCandidateBatchHeader Header;
			Header.BatchId = BatchId;
			Header.BatchKind = Cut.AtomicBatchKind;
			Header.Group = Relay.GetLeaseState().Group;
			Header.ConnectionGeneration = Relay.GetConnectionGeneration();
			Header.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
			Header.FrozenRosterRevision = Relay.GetRosterRevision();
			Header.FrozenRequiredFlightMask = Cut.RequiredFlightMask;
			Header.FrozenRequiredMemberMaskHash = Cut.RequiredMemberMaskHash;
			Header.BaselineRevision = Cut.AtomicBaselineRevision;
			Header.BaselineHash = Cut.AtomicBaselineHash;
			Header.IncludedFlightMask = Cut.RequiredFlightMask;
			Header.ClientBatchStartTick = Flights[0].ClientSimTick;
			Header.FragmentCount = bSplit ? 2u : 1u;
			Header.BatchPayloadHash = GuLiWingmanRelayHash::CandidatePayloads(Flights);
			Header.bAtomicCommit = true;
			for (const FGuLiWingmanCandidateBatch& Flight : Flights)
			{
				FGuLiWingmanAtomicCandidateBatchFragment OneFlight;
				OneFlight.Flights.Add(Flight);
				Header.BatchPayloadBytes += OneFlight.EstimatePayloadBytes();
			}

			TArray<FGuLiWingmanAtomicCandidateBatchFragment> Fragments;
			if (!bSplit)
			{
				FGuLiWingmanAtomicCandidateBatchFragment& Fragment = Fragments.AddDefaulted_GetRef();
				Fragment.Header = Header;
				Fragment.Flights = Flights;
				return Fragments;
			}

			FGuLiWingmanAtomicCandidateBatchFragment& First = Fragments.AddDefaulted_GetRef();
			First.Header = Header;
			First.FragmentIndex = 0u;
			First.Flights.Append(Flights.GetData(), 2);
			FGuLiWingmanAtomicCandidateBatchFragment& Second = Fragments.AddDefaulted_GetRef();
			Second.Header = Header;
			Second.FragmentIndex = 1u;
			Second.Flights.Append(Flights.GetData() + 2, Flights.Num() - 2);
			return Fragments;
		}

		bool AcknowledgeCut(
			FAutomationTestBase& Test,
			const FGuid& Sender,
			const FGuLiWingmanBootstrapBundle& Cut,
			const double NowSeconds)
		{
			FGuLiGroupAbilityConfigAck AbilityAck;
			AbilityAck.Group = Cut.Commit.Group;
			AbilityAck.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
			AbilityAck.SnapshotRevision = Cut.AbilityConfig.SnapshotRevision;
			AbilityAck.SnapshotHash = Cut.AbilityConfig.SnapshotHash;
			if (!Test.TestTrue(TEXT("Ability config ACK succeeds"),
				Relay.AcknowledgeAbilityConfig(Sender, AbilityAck, NowSeconds)))
			{
				return false;
			}

			FGuLiWingmanBootstrapCommit WireCommit;
			if (!Test.TestTrue(TEXT("Bootstrap commit crosses an independent byte buffer"),
				RoundTripBootstrapCommit(Cut.Commit, WireCommit)))
			{
				return false;
			}
			FGuLiWingmanTransferBaseline WireBaseline;
			const FGuLiWingmanTransferBaseline* AppliedBaseline = nullptr;
			if (Cut.bHasTransferBaseline)
			{
				if (!Test.TestTrue(TEXT("Transfer baseline crosses an independent byte buffer"),
					RoundTripTransferBaseline(Cut.TransferBaseline, WireBaseline)))
				{
					return false;
				}
				AppliedBaseline = &WireBaseline;
			}
			return Test.TestTrue(TEXT("Six-scope Bootstrap ACK succeeds"),
				Relay.AcknowledgeBootstrap(Sender, WireCommit, AppliedBaseline, NowSeconds + 0.01));
		}
	};

	bool TestNonAcceptedIsNonMutating(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FRelayMutationSnapshot& Before,
		const FGuLiWingmanRelayServer& Relay)
	{
		return Test.TestTrue(What, Before.Equals(FRelayMutationSnapshot::Capture(Relay)));
	}

	bool SubmitInitialAtomicBootstrap(FAutomationTestBase& Test, FFixture& Fixture)
	{
		const TArray<FGuLiWingmanCandidateBatch> Flights =
			Fixture.MakeAllFlights(1u, 1u, 100u, 0.1);
		const TArray<FGuLiWingmanAtomicCandidateBatchFragment> SourceFragments =
			Fixture.MakeAtomicFragments(Fixture.Bootstrap, Flights, 101u, true);
		FGuLiWingmanAtomicCandidateBatchFragment FirstWire;
		FGuLiWingmanAtomicCandidateBatchFragment SecondWire;
		if (!Test.TestTrue(TEXT("Bootstrap fragment zero uses Candidate NetSerialize"),
			RoundTripAtomicFragment(SourceFragments[0], FirstWire))
			|| !Test.TestTrue(TEXT("Bootstrap fragment one uses Candidate NetSerialize"),
				RoundTripAtomicFragment(SourceFragments[1], SecondWire)))
		{
			return false;
		}

		const FRelayMutationSnapshot Empty = FRelayMutationSnapshot::Capture(Fixture.Relay);
		const FGuLiWingmanAtomicBatchAcceptance Reordered = Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Owner, SecondWire, 0.25, FoundCarrier(), PermitWorld());
		Test.TestEqual(TEXT("Out-of-order first reliable fragment is pending"), Reordered.Disposition,
			EGuLiWingmanSubmissionDisposition::Pending);
		TestNonAcceptedIsNonMutating(Test, TEXT("Partial Bootstrap changes no accepted state"),
			Empty, Fixture.Relay);

		const FGuLiWingmanAtomicBatchAcceptance Duplicate = Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Owner, SecondWire, 0.27, FoundCarrier(), PermitWorld());
		Test.TestEqual(TEXT("Duplicate fragment is rejected"), Duplicate.RejectReason,
			EGuLiWingmanRejectReason::Duplicate);
		TestNonAcceptedIsNonMutating(Test, TEXT("Duplicate fragment changes no accepted state"),
			Empty, Fixture.Relay);

		// Fragment zero's first transmission is deterministically lost. Its reliable retry arrives
		// before the 0.5 second assembly deadline and completes the original transaction.
		const FGuLiWingmanAtomicBatchAcceptance Completed = Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Owner, FirstWire, 0.42, FoundCarrier(), PermitWorld());
		Test.TestEqual(TEXT("Reliable retry completes all Flights atomically"), Completed.Disposition,
			EGuLiWingmanSubmissionDisposition::Accepted);
		Test.TestEqual(TEXT("Exactly five initial Flight snapshots commit"),
			Fixture.Relay.GetAcceptedHistory().Num(), static_cast<int32>(GULI_WINGMAN_FLIGHT_COUNT));
		return Fixture.AcknowledgeCut(Test, Fixture.Owner, Fixture.Bootstrap, 0.45)
			&& Test.TestEqual(TEXT("Initial Bootstrap becomes Active"),
				Fixture.Relay.GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Active);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanListenHostWireParityAcceptanceTest,
	"GuLiStrike.Wingman.NetworkAcceptance.ListenHostWireDtoParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanListenHostWireParityAcceptanceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiWingmanNetworkAcceptanceTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, 1u) || !SubmitInitialAtomicBootstrap(*this, Fixture))
	{
		return false;
	}

	FGuLiWingmanCandidateBatch Source = Fixture.MakeFlight(0u, 71u, 2u, 1u, 106u, 0.55, true);
	FGuLiWingmanCandidateBatch WireCandidate;
	int32 WireBytes = 0;
	TestTrue(TEXT("Listen request crosses Candidate.NetSerialize"),
		RoundTripCandidate(Source, WireCandidate, &WireBytes));
	TestTrue(TEXT("The request has a non-empty wire payload"), WireBytes > 0);
	TestEqual(TEXT("Wire Candidate keeps the stable payload hash"),
		WireCandidate.ComputeStablePayloadHash(), Source.ComputeStablePayloadHash());
	Source.Samples[0].PositionCentimeters.X += 999999;
	TestNotEqual(TEXT("Mutating caller memory cannot alter the decoded request"),
		WireCandidate.Samples[0].PositionCentimeters.X, Source.Samples[0].PositionCentimeters.X);

	const FGuLiWingmanSubmissionResult Accepted = Fixture.Relay.SubmitCandidate(
		Fixture.Owner, WireCandidate, 0.70, FoundCarrier(), PermitWorld());
	TestEqual(TEXT("Wire-decoded request is accepted"), Accepted.Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted);

	FGuLiWingmanCandidateResultWire ResultWire;
	ResultWire.CandidateSequence = Accepted.Sequence;
	ResultWire.Acceptance = Accepted.Acceptance;
	ResultWire.AcceptedBatch = Accepted.AcceptedBatch;
	FGuLiWingmanCandidateResultWire ListenCopy;
	FGuLiWingmanCandidateResultWire RemoteCopy;
	TestTrue(TEXT("Production listen path validates a Candidate-result wire copy"),
		GuLiWingmanRelayWire::MakeValidatedCandidateResultCopy(ResultWire, ListenCopy));
	TestTrue(TEXT("Remote Client RPC consume path uses the same wire-copy contract"),
		GuLiWingmanRelayWire::MakeValidatedCandidateResultCopy(ResultWire, RemoteCopy));
	TestEqual(TEXT("Listen and remote Accepted hashes are byte-contract identical"),
		ListenCopy.AcceptedBatch.StableHash, RemoteCopy.AcceptedBatch.StableHash);
	TestEqual(TEXT("Listen and remote correlation sequences are identical"),
		ListenCopy.CandidateSequence, RemoteCopy.CandidateSequence);
	ResultWire.AcceptedBatch.Samples[0].PositionCentimeters.X += 1;
	TestNotEqual(TEXT("Wire-copy consumer rejects shared-memory aliasing"),
		ListenCopy.AcceptedBatch.Samples[0].PositionCentimeters.X,
		ResultWire.AcceptedBatch.Samples[0].PositionCentimeters.X);

	AddInfo(TEXT("formal_gate=false; scope=deterministic_in_memory_wire_contract; "
		"not_socket_or_multiprocess_evidence=true; source_dedicated_server_blocked=true"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanDeterministicWeakNetworkAcceptanceTest,
	"GuLiStrike.Wingman.NetworkAcceptance.DeterministicWeakNetworkRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanDeterministicWeakNetworkAcceptanceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiWingmanNetworkAcceptanceTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, 2u) || !SubmitInitialAtomicBootstrap(*this, Fixture))
	{
		return false;
	}

	FGuLiWingmanUploadRateGrant AuthorityGrant;
	TestTrue(TEXT("Only authority issues the reliable 10 Hz Grant"),
		Fixture.Relay.IssueHighRateGrant(106u,
			EGuLiWingmanUploadRateGrantReason::ServerObservedCombat, 0.47, AuthorityGrant));
	FGuLiWingmanUploadRateGrant ClientGrant;
	TestTrue(TEXT("Grant crosses an independent reliable byte buffer"),
		RoundTripUploadGrant(AuthorityGrant, ClientGrant));
	TestEqual(TEXT("Client observes the exact authority Grant revision"),
		ClientGrant.GrantRevision, Fixture.Relay.GetUploadRateGrant().GrantRevision);

	struct FPacket
	{
		double SendTimeSeconds = 0.0;
		double DeliveryTimeSeconds = 0.0;
		uint32 Ordinal = 0u;
		bool bDuplicate = false;
		TArray<uint8> Bytes;
	};
	auto EncodeCandidate = [this](const FGuLiWingmanCandidateBatch& Candidate, TArray<uint8>& OutBytes)
	{
		FMemoryWriter Writer(OutBytes, true);
		FGuLiWingmanCandidateBatch Writable = Candidate;
		bool bSuccess = false;
		Writable.NetSerialize(Writer, nullptr, bSuccess);
		Writer.Close();
		TestTrue(TEXT("Weak-network packet serializes through the production DTO"),
			bSuccess && !Writer.IsError() && !OutBytes.IsEmpty());
	};

	TArray<FPacket> Packets;
	uint32 OriginalSent = 0u;
	uint32 OriginalDropped = 0u;
	uint32 DuplicatesInjected = 0u;
	constexpr double JitterByFrame[4] = { 0.0, 0.12, -0.10, 0.03 };
	for (uint32 FrameOffset = 0u; FrameOffset < 4u; ++FrameOffset)
	{
		const double SendTime = 0.55 + static_cast<double>(FrameOffset) * 0.10;
		for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
		{
			const uint32 Ordinal = FrameOffset * GULI_WINGMAN_FLIGHT_COUNT + FlightIndex;
			++OriginalSent;
			FGuLiWingmanCandidateBatch Candidate = Fixture.MakeFlight(
				FlightIndex,
				100u + Ordinal,
				2u + FrameOffset,
				1u,
				106u + FrameOffset * 3u,
				SendTime,
				true);
			Candidate.RequestedRateClass = EGuLiWingmanUploadRateClass::HighRate10Hz;
			Candidate.ObservedGrantRevision = ClientGrant.GrantRevision;
			if (Ordinal == 18u)
			{
				// Exactly one of twenty original unreliable packets is lost: 5%.
				++OriginalDropped;
				continue;
			}

			FPacket Packet;
			Packet.SendTimeSeconds = SendTime;
			Packet.DeliveryTimeSeconds = SendTime + ConfiguredOneWayLatencySeconds
				+ JitterByFrame[FrameOffset] + static_cast<double>(FlightIndex) * 0.001;
			Packet.Ordinal = Ordinal;
			EncodeCandidate(Candidate, Packet.Bytes);
			Packets.Add(Packet);
			if (Ordinal == 0u || Ordinal == 12u)
			{
				FPacket Duplicate = Packet;
				Duplicate.bDuplicate = true;
				Duplicate.DeliveryTimeSeconds += 0.015;
				Packets.Add(MoveTemp(Duplicate));
				++DuplicatesInjected;
			}
		}
	}
	Packets.Sort([](const FPacket& Lhs, const FPacket& Rhs)
	{
		if (!FMath::IsNearlyEqual(Lhs.DeliveryTimeSeconds, Rhs.DeliveryTimeSeconds))
		{
			return Lhs.DeliveryTimeSeconds < Rhs.DeliveryTimeSeconds;
		}
		return Lhs.bDuplicate < Rhs.bDuplicate;
	});

	uint32 MaximumDeliveredOrdinal = 0u;
	int32 ReorderedDeliveries = 0;
	int32 AcceptedPackets = 0;
	int32 NonAcceptedPackets = 0;
	int32 ValidatedWorldSegments = 0;
	double RttSumSeconds = 0.0;
	for (const FPacket& Packet : Packets)
	{
		if (Packet.Ordinal < MaximumDeliveredOrdinal)
		{
			++ReorderedDeliveries;
		}
		MaximumDeliveredOrdinal = FMath::Max(MaximumDeliveredOrdinal, Packet.Ordinal);
		FMemoryReader Reader(Packet.Bytes, true);
		FGuLiWingmanCandidateBatch Decoded;
		bool bDecoded = false;
		Decoded.NetSerialize(Reader, nullptr, bDecoded);
		const bool bConsumedExactly = Reader.Tell() == Packet.Bytes.Num();
		Reader.Close();
		TestTrue(TEXT("Delivered packet decodes from an independent byte buffer"),
			bDecoded && bConsumedExactly && !Reader.IsError());

		const FRelayMutationSnapshot Before = FRelayMutationSnapshot::Capture(Fixture.Relay);
		const FGuLiWingmanSubmissionResult Result = Fixture.Relay.SubmitCandidate(
			Fixture.Owner,
			Decoded,
			Packet.DeliveryTimeSeconds,
			FoundCarrier(),
			PermitWorld(&ValidatedWorldSegments));
		RttSumSeconds += Packet.DeliveryTimeSeconds + ConfiguredOneWayLatencySeconds
			- Packet.SendTimeSeconds;
		if (Result.Disposition == EGuLiWingmanSubmissionDisposition::Accepted)
		{
			++AcceptedPackets;
			TestEqual(TEXT("An accepted Flight appends exactly one store record"),
				Fixture.Relay.GetAcceptedHistory().Num(), Before.AcceptedHistoryCount + 1);
		}
		else
		{
			++NonAcceptedPackets;
			TestNonAcceptedIsNonMutating(*this,
				TEXT("Rejected/duplicate/reordered packet advances no store, relay, sequence or accepted time"),
				Before,
				Fixture.Relay);
		}
	}

	TestEqual(TEXT("The deterministic stream sends twenty original packets"), OriginalSent, 20u);
	TestEqual(TEXT("Exactly five percent of original packets are lost"), OriginalDropped, 1u);
	TestEqual(TEXT("Two duplicate deliveries are injected"), DuplicatesInjected, 2u);
	TestTrue(TEXT("Jitter creates observable packet reordering"), ReorderedDeliveries > 0);
	TestTrue(TEXT("Weak-network stream still accepts live Flight progress"), AcceptedPackets > 0);
	TestTrue(TEXT("Loss/reorder/duplicates exercise non-accepting outcomes"), NonAcceptedPackets > 0);
	TestEqual(TEXT("Normal pose relay never runs server World movement validation"),
		ValidatedWorldSegments, 0);
	const double MeanRttMilliseconds = Packets.IsEmpty()
		? 0.0 : (RttSumSeconds / static_cast<double>(Packets.Num())) * 1000.0;
	TestTrue(TEXT("Configured deterministic RTT remains approximately 500 ms"),
		MeanRttMilliseconds >= 400.0 && MeanRttMilliseconds <= 650.0);

	// Carrier-history delivery is independent from normal pose relay. A current
	// owner packet commits immediately and never enters deferred server movement work.
	FGuLiWingmanCandidateBatch PendingSource = Fixture.MakeFlight(
		0u, 401u, 8u, Fixture.Relay.GetAcceptedSequenceForFlight(0u), 124u, 1.30, true);
	PendingSource.RequestedRateClass = EGuLiWingmanUploadRateClass::HighRate10Hz;
	PendingSource.ObservedGrantRevision = ClientGrant.GrantRevision;
	FGuLiWingmanCandidateBatch PendingWire;
	TestTrue(TEXT("Pending cross-channel Candidate uses NetSerialize"),
		RoundTripCandidate(PendingSource, PendingWire));
	int32 CarrierResolverCalls = 0;
	const FGuLiCarrierSourceResolver CarrierPending = [&CarrierResolverCalls](
		const FGuLiCarrierSourceRef&, FGuLiRelayCarrierState&)
	{
		++CarrierResolverCalls;
		return EGuLiRelayCarrierLookupResult::Pending;
	};
	const FRelayMutationSnapshot BeforePending = FRelayMutationSnapshot::Capture(Fixture.Relay);
	const FGuLiWingmanSubmissionResult PendingResult = Fixture.Relay.SubmitCandidate(
		Fixture.Owner, PendingWire, 1.35, CarrierPending, PermitWorld());
	TestEqual(TEXT("Missing carrier history cannot delay client-authored pose relay"),
		PendingResult.Disposition, EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("Normal pose relay never calls the carrier resolver"),
		CarrierResolverCalls, 0);
	TestEqual(TEXT("The accepted pose appends exactly one remote presentation cut"),
		Fixture.Relay.GetAcceptedHistory().Num(), BeforePending.AcceptedHistoryCount + 1);
	const FRelayMutationSnapshot AfterAcceptedPose = FRelayMutationSnapshot::Capture(Fixture.Relay);
	// Transport cadence has no normal-pose pending queue to expire.
	Fixture.Relay.AdvancePacketDeadlines(1.60, FoundCarrier());
	TArray<FGuLiWingmanSubmissionResult> Deferred;
	Fixture.Relay.DrainDeferredCandidateResults(Deferred);
	TestTrue(TEXT("Normal pose relay creates no deferred carrier timeout"), Deferred.IsEmpty());
	TestNonAcceptedIsNonMutating(*this, TEXT("Packet-deadline maintenance leaves the accepted pose intact"),
		AfterAcceptedPose, Fixture.Relay);
	TArray<uint32> SequenceBeforeRecovery;
	SequenceBeforeRecovery.SetNumZeroed(GULI_WINGMAN_FLIGHT_COUNT);
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		SequenceBeforeRecovery[FlightIndex] = Fixture.Relay.GetAcceptedSequenceForFlight(FlightIndex);
	}
	TestTrue(TEXT("The connection-silence watchdog runs after one second without traffic"),
		Fixture.Relay.RunLeaseMaintenance(2.4));
	TestEqual(TEXT("Missing connection traffic, rather than Flight movement, makes the group Stale"),
		Fixture.Relay.GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Stale);

	// Resume supplies a late-join client one new six-scope cut and requires one atomic all-Flight
	// Candidate before the group can become Active again.
	TestTrue(TEXT("Stale owner can begin Resume"), Fixture.Relay.BeginResume(2.45));
	FGuLiWingmanBootstrapBundle ResumeCut;
	TestTrue(TEXT("Resume builds a late-join six-scope Bootstrap"),
		Fixture.Relay.BuildBootstrap(ResumeCut));
	FGuLiWingmanBootstrapCommit LateJoinWireCommit;
	TestTrue(TEXT("Late-join client receives a well-formed wire commit"),
		RoundTripBootstrapCommit(ResumeCut.Commit, LateJoinWireCommit));
	TestEqual(TEXT("Late-join commit contains exactly six scopes"),
		LateJoinWireCommit.Scopes.Num(), static_cast<int32>(EGuLiWingmanBootstrapScope::Count));
	TestTrue(TEXT("Late-join cut freezes a transfer baseline"),
		ResumeCut.bHasTransferBaseline && ResumeCut.TransferBaseline.IsWellFormed());
	// Resume is ACK-first. Neither a valid nor invalid atomic fragment may enter the
	// assembler until the exact reliable six-scope cut has been acknowledged.
	if (!Fixture.AcknowledgeCut(*this, Fixture.Owner, ResumeCut, 2.46))
	{
		return false;
	}

	TArray<FGuLiWingmanCandidateBatch> InvalidResumeFlights =
		Fixture.MakeAllFlights(500u, 9u, 136u, 2.48);
	--InvalidResumeFlights.Last().AbilitySetRevision;
	const FGuLiWingmanAtomicCandidateBatchFragment InvalidResumeSource =
		Fixture.MakeAtomicFragments(ResumeCut, InvalidResumeFlights, 501u, false)[0];
	FGuLiWingmanAtomicCandidateBatchFragment InvalidResumeWire;
	TestTrue(TEXT("Rejected Resume transaction still crosses the wire"),
		RoundTripAtomicFragment(InvalidResumeSource, InvalidResumeWire));
	const FRelayMutationSnapshot BeforeInvalidResume = FRelayMutationSnapshot::Capture(Fixture.Relay);
	const FGuLiWingmanAtomicBatchAcceptance InvalidResume =
		Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Owner, InvalidResumeWire, 2.52, FoundCarrier(), PermitWorld());
	TestEqual(TEXT("One stale ability Flight rejects the whole Resume"),
		InvalidResume.RejectReason, EGuLiWingmanRejectReason::StaleAbilitySetRevision);
	TestNonAcceptedIsNonMutating(*this, TEXT("Rejected Resume commits no Flight or accepted time"),
		BeforeInvalidResume, Fixture.Relay);

	const TArray<FGuLiWingmanCandidateBatch> ResumeFlights =
		Fixture.MakeAllFlights(510u, 9u, 136u, 2.48);
	const TArray<FGuLiWingmanAtomicCandidateBatchFragment> ResumeSources =
		Fixture.MakeAtomicFragments(ResumeCut, ResumeFlights, 511u, true);
	FGuLiWingmanAtomicCandidateBatchFragment ResumeFirst;
	FGuLiWingmanAtomicCandidateBatchFragment ResumeSecond;
	TestTrue(TEXT("Resume fragment zero crosses the wire"),
		RoundTripAtomicFragment(ResumeSources[0], ResumeFirst));
	TestTrue(TEXT("Resume fragment one crosses the wire"),
		RoundTripAtomicFragment(ResumeSources[1], ResumeSecond));
	const FRelayMutationSnapshot BeforeResumePartial = FRelayMutationSnapshot::Capture(Fixture.Relay);
	const FGuLiWingmanAtomicBatchAcceptance ResumePartial =
		Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Owner, ResumeSecond, 2.53, FoundCarrier(), PermitWorld());
	TestEqual(TEXT("Out-of-order Resume fragment remains partial"), ResumePartial.Disposition,
		EGuLiWingmanSubmissionDisposition::Pending);
	TestNonAcceptedIsNonMutating(*this, TEXT("Partial Resume advances nothing"),
		BeforeResumePartial, Fixture.Relay);
	const FGuLiWingmanAtomicBatchAcceptance ResumeComplete =
		Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Owner, ResumeFirst, 2.65, FoundCarrier(), PermitWorld());
	TestEqual(TEXT("Complete Resume atomically restores all Flights"), ResumeComplete.Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("Resume returns the whole group to Active"),
		Fixture.Relay.GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Active);
	TestTrue(TEXT("First 1 Hz maintenance after Resume stays Active"),
		Fixture.Relay.RunLeaseMaintenance(3.4));
	TestEqual(TEXT("Fresh all-Flight Resume survives its first watchdog"),
		Fixture.Relay.GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Active);

	// A takeover repeats the frozen-cut transaction with the backup as the new owner. The old owner
	// is rejected before assembly and cannot advance any store/sequence/freshness state.
	FGuLiWingmanPendingLeaseOffer Offer;
	TestTrue(TEXT("Authority sends Backup a non-committing Lease Offer"),
		Fixture.Relay.BeginLeaseOffer(Fixture.Backup, Fixture.Third, 3.45, Offer));
	TestEqual(TEXT("Offer preview keeps the old owner Active"),
		Fixture.Relay.GetLeaseState().OwnerPlayerGuid, Fixture.Owner);
	TestTrue(TEXT("Exact Ready commits the new Lease"),
		Fixture.Relay.AcknowledgeLeaseOfferReady(Fixture.Backup, Offer.OfferRevision, 3.50));
	FGuLiWingmanBootstrapBundle TakeoverCut;
	TestTrue(TEXT("Takeover builds a new frozen six-scope cut"),
		Fixture.Relay.BuildBootstrap(TakeoverCut));
	if (!Fixture.AcknowledgeCut(*this, Fixture.Backup, TakeoverCut, 3.51))
	{
		return false;
	}
	const TArray<FGuLiWingmanCandidateBatch> TakeoverFlights =
		Fixture.MakeAllFlights(600u, 10u, 154u, 3.52);
	const TArray<FGuLiWingmanAtomicCandidateBatchFragment> TakeoverSources =
		Fixture.MakeAtomicFragments(TakeoverCut, TakeoverFlights, 601u, true);
	FGuLiWingmanAtomicCandidateBatchFragment TakeoverFirst;
	FGuLiWingmanAtomicCandidateBatchFragment TakeoverSecond;
	TestTrue(TEXT("Takeover fragment zero crosses the wire"),
		RoundTripAtomicFragment(TakeoverSources[0], TakeoverFirst));
	TestTrue(TEXT("Takeover fragment one crosses the wire"),
		RoundTripAtomicFragment(TakeoverSources[1], TakeoverSecond));
	const FRelayMutationSnapshot BeforeOldOwner = FRelayMutationSnapshot::Capture(Fixture.Relay);
	const FGuLiWingmanAtomicBatchAcceptance OldOwnerAttempt =
		Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Owner, TakeoverFirst, 3.53, FoundCarrier(), PermitWorld());
	TestEqual(TEXT("Old owner cannot seed the Takeover assembler"), OldOwnerAttempt.Disposition,
		EGuLiWingmanSubmissionDisposition::Rejected);
	TestNonAcceptedIsNonMutating(*this, TEXT("Old-owner Takeover request advances nothing"),
		BeforeOldOwner, Fixture.Relay);

	const FRelayMutationSnapshot BeforeTakeoverPartial = FRelayMutationSnapshot::Capture(Fixture.Relay);
	const FGuLiWingmanAtomicBatchAcceptance TakeoverPartial =
		Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Backup, TakeoverSecond, 3.54, FoundCarrier(), PermitWorld());
	TestEqual(TEXT("Reordered Takeover is pending until all Flights arrive"),
		TakeoverPartial.Disposition, EGuLiWingmanSubmissionDisposition::Pending);
	TestNonAcceptedIsNonMutating(*this, TEXT("Partial Takeover advances nothing"),
		BeforeTakeoverPartial, Fixture.Relay);
	const FGuLiWingmanAtomicBatchAcceptance TakeoverComplete =
		Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Backup, TakeoverFirst, 3.65, FoundCarrier(), PermitWorld());
	TestEqual(TEXT("Takeover atomically commits all five Flights"), TakeoverComplete.Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("Takeover returns the group to Active under the backup"),
		Fixture.Relay.GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Active);
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		TestEqual(TEXT("Resume and Takeover each advance the Flight exactly once"),
			Fixture.Relay.GetAcceptedSequenceForFlight(FlightIndex),
			SequenceBeforeRecovery[FlightIndex] + 2u);
	}
	TestEqual(TEXT("Authority performs zero Wingman movement writes"),
		Fixture.Relay.GetServerWingmanMovementWriteCount(), 0ull);

	UE_LOG(LogTemp, Display,
		TEXT("GULI_NETWORK_ACCEPTANCE_METRICS "
			"{\"formal_gate\":false,\"transport\":\"deterministic_in_memory\","
			"\"socket_or_multiprocess\":false,\"configured_rtt_ms\":500,"
			"\"original_packets\":%u,\"lost_original_packets\":%u,"
			"\"loss_percent\":5,\"duplicates\":%u,\"reordered_deliveries\":%d,"
			"\"accepted_packets\":%d,\"non_accepted_packets\":%d,"
			"\"mean_simulated_rtt_ms\":%.3f,\"dedicated_server_source_build_blocked\":true}"),
		OriginalSent,
		OriginalDropped,
		DuplicatesInjected,
		ReorderedDeliveries,
		AcceptedPackets,
		NonAcceptedPackets,
		MeanRttMilliseconds);
	AddInfo(FString::Printf(
		TEXT("metrics={\"formal_gate\":false,\"transport\":\"deterministic_in_memory\","
			"\"configured_rtt_ms\":500,\"original_packets\":%u,"
			"\"lost_original_packets\":%u,\"loss_percent\":5,\"duplicates\":%u,"
			"\"reordered_deliveries\":%d,\"accepted_packets\":%d,"
			"\"non_accepted_packets\":%d,\"mean_simulated_rtt_ms\":%.3f}"),
		OriginalSent,
		OriginalDropped,
		DuplicatesInjected,
		ReorderedDeliveries,
		AcceptedPackets,
		NonAcceptedPackets,
		MeanRttMilliseconds));
	AddInfo(TEXT("formal_gate=false; deterministic in-memory weak-network harness only; "
		"not socket/multiprocess evidence; source Dedicated Server remains blocked by engine distribution"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanRemotePresentationTimeoutAcceptanceTest,
	"GuLiStrike.Wingman.NetworkAcceptance.RemotePresentationExtrapolateFadeHide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanRemotePresentationTimeoutAcceptanceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiWingmanNetworkAcceptanceTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, 3u))
	{
		return false;
	}
	FGuLiWingmanCandidateBatch Source = Fixture.MakeFlight(0u, 1u, 1u, 0u, 100u, 10.0);
	Source.Samples[0].VelocityCentimetersPerSecond = FIntVector(100, 0, 0);
	FGuLiWingmanCandidateBatch Wire;
	TestTrue(TEXT("Remote presentation source crosses Candidate.NetSerialize"),
		RoundTripCandidate(Source, Wire));
	FGuLiWingmanPresentationPose Pose;
	TestTrue(TEXT("Remote pose reconstructs from decoded DTO"),
		GuLiWingmanPresentationPolicy::BuildPose(Wire.Samples[0], 10.0, 1u, Pose));
	const TArray<FGuLiWingmanPresentationPose> Poses = { Pose };

	const FGuLiWingmanPresentationEvaluation Extrapolated =
		GuLiWingmanPresentationPolicy::Evaluate(Poses, 10.5, 10.5);
	TestTrue(TEXT("Remote pose extrapolates through exactly 0.5 seconds"),
		Extrapolated.bVisible && Extrapolated.bInteractable && Extrapolated.bExtrapolating);
	TestTrue(TEXT("0.5-second extrapolation is velocity bounded"),
		Extrapolated.Transform.GetLocation().Equals(Pose.Location + FVector(50.0, 0.0, 0.0), 0.001));

	const FGuLiWingmanPresentationEvaluation Fading =
		GuLiWingmanPresentationPolicy::Evaluate(Poses, 10.575, 10.575);
	TestTrue(TEXT("Remote pose remains visual during the following 0.15-second fade"),
		Fading.bVisible);
	TestFalse(TEXT("Fading remote pose is no longer interactable"), Fading.bInteractable);
	TestFalse(TEXT("Fading remote pose stops extrapolating"), Fading.bExtrapolating);
	TestTrue(TEXT("Halfway through fade produces half opacity"),
		FMath::IsNearlyEqual(Fading.Opacity, 0.5f, 0.001f));

	const FGuLiWingmanPresentationEvaluation Hidden =
		GuLiWingmanPresentationPolicy::Evaluate(Poses, 10.65, 10.65);
	TestFalse(TEXT("Remote pose hides at 0.5 + 0.15 seconds"), Hidden.bVisible);
	TestEqual(TEXT("Hidden remote pose has zero opacity"), Hidden.Opacity, 0.0f);
	TestFalse(TEXT("Dedicated Server still cannot create presentation"),
		GuLiWingmanPresentationPolicy::ShouldCreateClientPresentation(NM_DedicatedServer));
	AddInfo(TEXT("formal_gate=false; presentation policy regression; no rendered/socket evidence"));
	return true;
}

#endif
