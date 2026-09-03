// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Relay/GuLiWingmanRelayServer.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Misc/AutomationTest.h"

namespace GuLiWingmanRelayTransactionTests
{
	FGuLiWingmanGroupHandle MakeGroup(const uint32 Seed = 1u)
	{
		FGuLiWingmanGroupHandle Group;
		Group.ShipInstanceId = FGuid(0xa1000000u + Seed, 0xb2000000u, 0xc3000000u, 0xd4000000u);
		Group.ShipGeneration = 2u;
		Group.GroupGeneration = 3u;
		return Group;
	}

	FGuLiGroupAbilityConfigSnapshot MakeConfig(const FGuLiWingmanGroupHandle& Group)
	{
		FGuLiGroupAbilityConfigSnapshot Config;
		Config.ShipInstanceId = Group.ShipInstanceId;
		Config.ShipGeneration = Group.ShipGeneration;
		Config.GroupGeneration = Group.GroupGeneration;
		Config.AbilitySetRevision = 4u;
		Config.SnapshotRevision = 5u;
		Config.bGroupAbilitiesValid = true;
		Config.FormationAbilityId = TAG_GuLi_ShipAbility_Formation_DoubleRing;
		Config.BasicWeaponAbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
		Config.MissileAbilityId = TAG_GuLi_ShipAbility_Weapon_Missile_Salvo;
		Config.FormationDefinitionRevision = 6u;
		Config.FormationDefinitionChecksum = 0x1111222233334444ull;
		Config.BasicWeaponDefinitionRevision = 7u;
		Config.BasicWeaponDefinitionChecksum = 0x2222333344445555ull;
		Config.MissileDefinitionRevision = 8u;
		Config.MissileDefinitionChecksum = 0x3333444455556666ull;
		Config.FormationCommandRevision = 9u;
		Config.EffectiveClientSimTick = 100u;
		Config.RefreshHash();
		return Config;
	}

	FGuLiWingmanRelayValidationRevisions MakeRevisions()
	{
		FGuLiWingmanRelayValidationRevisions Revisions;
		Revisions.NavSchemaRevision = 2u;
		Revisions.NavDataChecksum = 0xabcdef0123456789ull;
		Revisions.TuningRevision = 3u;
		Revisions.ObstacleRevision = 4u;
		return Revisions;
	}

	FGuLiCarrierSourceResolver FoundCarrier()
	{
		return [](const FGuLiCarrierSourceRef&, FGuLiRelayCarrierState& Out)
		{
			Out.Transform = FTransform::Identity;
			Out.Velocity = FVector(100.0, 0.0, 0.0);
			Out.ServerWorldTimeSeconds = 0.1;
			return EGuLiRelayCarrierLookupResult::Found;
		};
	}

	FGuLiCandidateWorldValidator PermitWorld()
	{
		return [](const FGuLiWingmanCandidateWorldValidationContext& Context)
		{
			return Context.IsWellFormed()
				? EGuLiWingmanRejectReason::None : EGuLiWingmanRejectReason::InvalidIdentity;
		};
	}

	struct FFixture
	{
		FGuLiWingmanRelayServer Relay;
		FGuid Owner = FGuid(1u, 2u, 3u, 4u);
		FGuid Backup = FGuid(5u, 6u, 7u, 8u);
		FGuLiWingmanBootstrapBundle Bootstrap;

		bool Initialize(FAutomationTestBase& Test, const uint32 Seed = 1u)
		{
			const FGuLiWingmanGroupHandle Group = MakeGroup(Seed);
			if (!Test.TestTrue(TEXT("Strict relay initializes"), Relay.InitializeGroup(
				17u, Group, Owner, Backup, MakeConfig(Group), 0.0))) return false;
			if (!Test.TestTrue(TEXT("Strict connection contract configures"),
				Relay.ConfigureStrictFlightContract(7u, MakeRevisions(), 0.0))) return false;
			return Test.TestTrue(TEXT("Strict six-scope cut builds"), Relay.BuildBootstrap(Bootstrap));
		}

		FGuLiWingmanCandidateBatch MakeFlight(
			const uint8 FlightIndex,
			const uint32 CandidateSequence,
			const uint32 FrameSequence = 1u,
			const uint32 BaseAcceptedSequence = 0u,
			const uint32 ClientTick = 100u,
			const double CaptureTime = 0.1) const
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
			Candidate.ClientSimTick = ClientTick;
			Candidate.CaptureEstimatedServerTimeSeconds = CaptureTime;
			Candidate.NavSchemaRevision = Relay.GetValidationRevisions().NavSchemaRevision;
			Candidate.NavDataChecksum = Relay.GetValidationRevisions().NavDataChecksum;
			Candidate.TuningRevision = Relay.GetValidationRevisions().TuningRevision;
			Candidate.ObstacleRevision = Relay.GetValidationRevisions().ObstacleRevision;
			Candidate.CarrierSource.CanonicalEpoch = 1u;
			Candidate.CarrierSource.MoveRevision = CandidateSequence;
			Candidate.AbilitySetRevision = Relay.GetAbilityConfig().AbilitySetRevision;
			Candidate.FormationCommandRevision = Relay.GetAbilityConfig().FormationCommandRevision;
			Candidate.FormationDefinitionChecksum = Relay.GetAbilityConfig().FormationDefinitionChecksum;
			for (const FGuLiWingmanRosterEntry& Entry : Relay.GetRoster())
			{
				if (Entry.bDead || Entry.Wingman.Flight.FlightIndex != FlightIndex) continue;
				FGuLiWingmanCandidateSample& Sample = Candidate.Samples.AddDefaulted_GetRef();
				Sample.Wingman = Entry.Wingman;
				Sample.PositionCentimeters = FIntVector(
					static_cast<int32>(FlightIndex) * 1000,
					static_cast<int32>(Entry.Wingman.MemberIndex) * 100, 1000);
				Sample.VelocityCentimetersPerSecond = FIntVector(100, 0, 0);
				Sample.RotationCentiDegrees = FIntVector::ZeroValue;
				Sample.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Follow);
				Candidate.RequiredMemberMask |= static_cast<uint8>(1u << Entry.Wingman.MemberIndex);
			}
			return Candidate;
		}

		FGuLiWingmanAtomicCandidateBatchFragment MakeFragment(
			const TArray<FGuLiWingmanCandidateBatch>& AllFlights,
			const TArray<FGuLiWingmanCandidateBatch>& FragmentFlights,
			const uint8 FragmentIndex,
			const uint8 FragmentCount,
			const uint64 BatchId = 1u) const
		{
			FGuLiWingmanAtomicCandidateBatchFragment Fragment;
			Fragment.Header.BatchId = BatchId;
			Fragment.Header.BatchKind = Bootstrap.AtomicBatchKind;
			Fragment.Header.Group = Relay.GetLeaseState().Group;
			Fragment.Header.ConnectionGeneration = Relay.GetConnectionGeneration();
			Fragment.Header.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
			Fragment.Header.FrozenRosterRevision = Relay.GetRosterRevision();
			Fragment.Header.FrozenRequiredFlightMask = Bootstrap.RequiredFlightMask;
			Fragment.Header.FrozenRequiredMemberMaskHash = Bootstrap.RequiredMemberMaskHash;
			Fragment.Header.BaselineRevision = Bootstrap.AtomicBaselineRevision;
			Fragment.Header.BaselineHash = Bootstrap.AtomicBaselineHash;
			Fragment.Header.IncludedFlightMask = Bootstrap.RequiredFlightMask;
			Fragment.Header.ClientBatchStartTick = 100u;
			Fragment.Header.FragmentCount = FragmentCount;
			Fragment.Header.BatchPayloadHash = GuLiWingmanRelayHash::CandidatePayloads(AllFlights);
			Fragment.Flights = FragmentFlights;
			Fragment.FragmentIndex = FragmentIndex;
			uint32 TotalBytes = 0u;
			for (const FGuLiWingmanCandidateBatch& Flight : AllFlights)
			{
				FGuLiWingmanAtomicCandidateBatchFragment One;
				One.Flights.Add(Flight);
				TotalBytes += One.EstimatePayloadBytes();
			}
			Fragment.Header.BatchPayloadBytes = TotalBytes;
			return Fragment;
		}

		TArray<FGuLiWingmanCandidateBatch> MakeAllFlights(
			const uint32 FirstCandidateSequence = 1u,
			const uint32 FrameSequence = 1u,
			const uint32 BaseAcceptedSequence = 0u,
			const uint32 ClientTick = 100u,
			const double CaptureTime = 0.1) const
		{
			TArray<FGuLiWingmanCandidateBatch> Flights;
			for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
			{
				Flights.Add(MakeFlight(FlightIndex, FirstCandidateSequence + FlightIndex,
					FrameSequence, BaseAcceptedSequence, ClientTick, CaptureTime));
			}
			return Flights;
		}

		bool AckAndActivate(FAutomationTestBase& Test, const double NowSeconds = 0.11)
		{
			FGuLiGroupAbilityConfigAck Ack;
			Ack.Group = Relay.GetLeaseState().Group;
			Ack.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
			Ack.SnapshotRevision = Relay.GetAbilityConfig().SnapshotRevision;
			Ack.SnapshotHash = Relay.GetAbilityConfig().SnapshotHash;
			if (!Test.TestTrue(TEXT("Ability config ACK"),
				Relay.AcknowledgeAbilityConfig(Owner, Ack, NowSeconds)))
			{
				return false;
			}
			const FGuLiWingmanTransferBaseline* TransferBaseline = Bootstrap.bHasTransferBaseline
				? &Bootstrap.TransferBaseline : nullptr;
			return Test.TestTrue(TEXT("Six-scope ACK"), Relay.AcknowledgeBootstrap(
				Owner, Bootstrap.Commit, TransferBaseline, NowSeconds + 0.01));
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanAtomicCandidateTransactionTest,
	"GuLiStrike.Wingman.Relay.ProtocolTransactions.AtomicAllOrNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanAtomicCandidateTransactionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTransactionTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	const TArray<FGuLiWingmanCandidateBatch> Flights = Fixture.MakeAllFlights();
	TArray<FGuLiWingmanCandidateBatch> InvalidFlights = Flights;
	--InvalidFlights.Last().AbilitySetRevision;
	TArray<FGuLiWingmanCandidateBatch> InvalidFirst;
	InvalidFirst.Append(InvalidFlights.GetData(), 2);
	TArray<FGuLiWingmanCandidateBatch> InvalidSecond;
	InvalidSecond.Append(InvalidFlights.GetData() + 2, 3);
	TestEqual(TEXT("An incomplete invalid transaction is only pending"),
		Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Owner, Fixture.MakeFragment(InvalidFlights, InvalidFirst, 0u, 2u, 10u), 0.1,
			FoundCarrier(), PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Pending);
	const FGuLiWingmanAtomicBatchAcceptance InvalidComplete = Fixture.Relay.SubmitAtomicCandidateFragment(
		Fixture.Owner, Fixture.MakeFragment(InvalidFlights, InvalidSecond, 1u, 2u, 10u), 0.1,
		FoundCarrier(), PermitWorld());
	TestEqual(TEXT("One invalid Flight rejects the complete transaction"), InvalidComplete.RejectReason,
		EGuLiWingmanRejectReason::StaleAbilitySetRevision);
	TestEqual(TEXT("A fully assembled rejected transaction writes no Accepted Store"),
		Fixture.Relay.GetAcceptedHistory().Num(), 0);
	for (uint8 Flight = 0u; Flight < GULI_WINGMAN_FLIGHT_COUNT; ++Flight)
	{
		TestEqual(TEXT("Rejected transaction advances no Flight sequence"),
			Fixture.Relay.GetAcceptedSequenceForFlight(Flight), 0u);
	}

	TArray<FGuLiWingmanCandidateBatch> First;
	First.Append(Flights.GetData(), 2);
	TArray<FGuLiWingmanCandidateBatch> Second;
	Second.Append(Flights.GetData() + 2, 3);
	const FGuLiWingmanAtomicBatchAcceptance Partial = Fixture.Relay.SubmitAtomicCandidateFragment(
		Fixture.Owner, Fixture.MakeFragment(Flights, First, 0u, 2u, 11u), 0.1,
		FoundCarrier(), PermitWorld());
	TestEqual(TEXT("First fragment is pending"), Partial.Disposition,
		EGuLiWingmanSubmissionDisposition::Pending);
	TestEqual(TEXT("Partial batch writes no Accepted Store"), Fixture.Relay.GetAcceptedHistory().Num(), 0);
	for (uint8 Flight = 0u; Flight < GULI_WINGMAN_FLIGHT_COUNT; ++Flight)
	{
		TestEqual(TEXT("Partial batch advances no Flight sequence"),
			Fixture.Relay.GetAcceptedSequenceForFlight(Flight), 0u);
	}
	const FGuLiWingmanAtomicBatchAcceptance Committed = Fixture.Relay.SubmitAtomicCandidateFragment(
		Fixture.Owner, Fixture.MakeFragment(Flights, Second, 1u, 2u, 11u), 0.1,
		FoundCarrier(), PermitWorld());
	TestEqual(TEXT("Complete batch commits"), Committed.Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("Exactly five Flight snapshots commit"), Fixture.Relay.GetAcceptedHistory().Num(), 5);
	for (const FGuLiWingmanCandidateBatch& Flight : Flights)
	{
		for (const FGuLiWingmanCandidateSample& Expected : Flight.Samples)
		{
			FGuLiWingmanCandidateSample Latest;
			double AcceptedTimeSeconds = 0.0;
			TestTrue(TEXT("Every member has an O(1) latest Accepted Store sample"),
				Fixture.Relay.TryGetLatestAcceptedSample(
					Expected.Wingman, Latest, &AcceptedTimeSeconds));
			TestTrue(TEXT("The current Accepted Store sample preserves exact identity and position"),
				Latest.Wingman == Expected.Wingman
					&& Latest.PositionCentimeters == Expected.PositionCentimeters);
			TestTrue(TEXT("The latest sample exposes a finite accepted timestamp"),
				FMath::IsFinite(AcceptedTimeSeconds));
		}
	}
	TestTrue(TEXT("Candidate batch alone does not bypass reliable scope ACK"),
		Fixture.Relay.GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Initializing);
	if (!Fixture.AckAndActivate(*this)) return false;
	TestTrue(TEXT("Atomic batch plus both ACKs activates"),
		Fixture.Relay.GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Active);
	TestEqual(TEXT("Server writes no Wingman movement"), Fixture.Relay.GetServerWingmanMovementWriteCount(), 0ull);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanReliableAcksBeforeAtomicCandidateTest,
	"GuLiStrike.Wingman.Relay.ProtocolTransactions.ReliableAcksBeforeAtomicCandidate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanReliableAcksBeforeAtomicCandidateTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTransactionTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, 11u)) return false;
	TestTrue(TEXT("Initial Bootstrap requires an atomic candidate batch"),
		Fixture.Bootstrap.bRequiresAtomicCandidateBatch);
	TestFalse(TEXT("Atomic candidate batch starts uncommitted"),
		Fixture.Relay.IsAtomicCandidateBatchCommitted());

	FGuLiGroupAbilityConfigAck AbilityAck;
	AbilityAck.Group = Fixture.Relay.GetLeaseState().Group;
	AbilityAck.LeaseEpoch = Fixture.Relay.GetLeaseState().LeaseEpoch;
	AbilityAck.SnapshotRevision = Fixture.Relay.GetAbilityConfig().SnapshotRevision;
	AbilityAck.SnapshotHash = Fixture.Relay.GetAbilityConfig().SnapshotHash;
	if (!TestTrue(TEXT("AbilityConfig ACK is accepted while Initializing before the atomic candidate"),
		Fixture.Relay.AcknowledgeAbilityConfig(Fixture.Owner, AbilityAck, 0.05)))
	{
		return false;
	}
	TestEqual(TEXT("AbilityConfig ACK alone keeps the group Initializing"),
		Fixture.Relay.GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Initializing);

	const FGuLiWingmanTransferBaseline* TransferBaseline = Fixture.Bootstrap.bHasTransferBaseline
		? &Fixture.Bootstrap.TransferBaseline : nullptr;
	if (!TestTrue(TEXT("Six-scope Bootstrap ACK is accepted while Initializing before the atomic candidate"),
		Fixture.Relay.AcknowledgeBootstrap(
			Fixture.Owner, Fixture.Bootstrap.Commit, TransferBaseline, 0.06)))
	{
		return false;
	}
	TestEqual(TEXT("Both reliable ACKs still keep the group Initializing without an atomic candidate"),
		Fixture.Relay.GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Initializing);
	TestEqual(TEXT("Reliable ACKs write no Accepted Store history"),
		Fixture.Relay.GetAcceptedHistory().Num(), 0);
	for (uint8 Flight = 0u; Flight < GULI_WINGMAN_FLIGHT_COUNT; ++Flight)
	{
		TestEqual(TEXT("Reliable ACKs advance no Flight sequence"),
			Fixture.Relay.GetAcceptedSequenceForFlight(Flight), 0u);
	}

	const TArray<FGuLiWingmanCandidateBatch> Flights = Fixture.MakeAllFlights();
	TArray<FGuLiWingmanCandidateBatch> First;
	First.Append(Flights.GetData(), 2);
	TArray<FGuLiWingmanCandidateBatch> Second;
	Second.Append(Flights.GetData() + 2, 3);
	const FGuLiWingmanAtomicBatchAcceptance Partial = Fixture.Relay.SubmitAtomicCandidateFragment(
		Fixture.Owner, Fixture.MakeFragment(Flights, First, 0u, 2u, 21u), 0.1,
		FoundCarrier(), PermitWorld());
	TestEqual(TEXT("First atomic fragment remains pending"), Partial.Disposition,
		EGuLiWingmanSubmissionDisposition::Pending);
	TestEqual(TEXT("Partial atomic batch still keeps the group Initializing"),
		Fixture.Relay.GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Initializing);
	TestEqual(TEXT("Partial atomic batch writes no Accepted Store history"),
		Fixture.Relay.GetAcceptedHistory().Num(), 0);

	const FGuLiWingmanAtomicBatchAcceptance Committed = Fixture.Relay.SubmitAtomicCandidateFragment(
		Fixture.Owner, Fixture.MakeFragment(Flights, Second, 1u, 2u, 21u), 0.1,
		FoundCarrier(), PermitWorld());
	TestEqual(TEXT("Completing the atomic batch is accepted"), Committed.Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted);
	TestTrue(TEXT("Atomic candidate batch is committed"),
		Fixture.Relay.IsAtomicCandidateBatchCommitted());
	TestEqual(TEXT("Atomic completion writes exactly one snapshot per Flight"),
		Fixture.Relay.GetAcceptedHistory().Num(), GULI_WINGMAN_FLIGHT_COUNT);
	TestEqual(TEXT("The group activates only after both ACKs and atomic completion"),
		Fixture.Relay.GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Active);
	TestEqual(TEXT("ACK-first activation still writes no server Wingman movement"),
		Fixture.Relay.GetServerWingmanMovementWriteCount(), 0ull);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanAtomicTransferKindsTransactionTest,
	"GuLiStrike.Wingman.Relay.ProtocolTransactions.AtomicResumeAndTakeover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanAtomicTransferKindsTransactionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTransactionTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, 5u)) return false;
	TArray<FGuLiWingmanCandidateBatch> Flights = Fixture.MakeAllFlights();
	if (!TestEqual(TEXT("Initial atomic Bootstrap commits"),
		Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Owner, Fixture.MakeFragment(Flights, Flights, 0u, 1u), 0.1,
			FoundCarrier(), PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted)
		|| !Fixture.AckAndActivate(*this)) return false;

	Fixture.Owner = FGuid(9u, 10u, 11u, 12u);
	TestTrue(TEXT("Authority begins takeover"),
		Fixture.Relay.BeginTakeover(Fixture.Owner, Fixture.Backup, 0.5));
	TestTrue(TEXT("Takeover freezes a fresh six-scope baseline"),
		Fixture.Relay.BuildBootstrap(Fixture.Bootstrap));
	TestEqual(TEXT("Takeover requires the typed atomic envelope"), Fixture.Bootstrap.AtomicBatchKind,
		EGuLiWingmanAtomicBatchKind::Takeover);
	TestTrue(TEXT("Takeover carries the frozen prior Accepted snapshot"),
		Fixture.Bootstrap.bHasTransferBaseline
		&& Fixture.Bootstrap.AcceptedSnapshot.Num() == GULI_WINGMAN_FLIGHT_COUNT);
	if (!Fixture.AckAndActivate(*this, 0.55)) return false;
	Flights = Fixture.MakeAllFlights(6u, 1u, 1u, 106u, 0.6);
	TestEqual(TEXT("Takeover atomically rebases all Flights"),
		Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Owner, Fixture.MakeFragment(Flights, Flights, 0u, 1u, 2u), 0.6,
			FoundCarrier(), PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted);
	TestTrue(TEXT("Takeover becomes Active only after atomic batch and both ACKs"),
		Fixture.Relay.GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Active);

	Fixture.Relay.AdvanceTime(1.61, FoundCarrier());
	TestEqual(TEXT("1 Hz watchdog makes the silent owner Stale"),
		Fixture.Relay.GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Stale);
	TestTrue(TEXT("Authority begins resume"), Fixture.Relay.BeginResume(1.65));
	TestTrue(TEXT("Resume freezes a fresh six-scope baseline"),
		Fixture.Relay.BuildBootstrap(Fixture.Bootstrap));
	TestEqual(TEXT("Resume requires the typed atomic envelope"), Fixture.Bootstrap.AtomicBatchKind,
		EGuLiWingmanAtomicBatchKind::Resume);
	if (!Fixture.AckAndActivate(*this, 1.66)) return false;
	// Resume retains the same Lease and its per-Flight Frame high-water marks.
	Flights = Fixture.MakeAllFlights(11u, 2u, 2u, 112u, 1.7);
	TestEqual(TEXT("Resume atomically rebases all Flights"),
		Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Owner, Fixture.MakeFragment(Flights, Flights, 0u, 1u, 3u), 1.7,
			FoundCarrier(), PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted);
	TestTrue(TEXT("Resume returns to Active after the same transaction gates"),
		Fixture.Relay.GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Active);
	TestEqual(TEXT("Three atomic transactions still write no server Wingman movement"),
		Fixture.Relay.GetServerWingmanMovementWriteCount(), 0ull);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanStrictContractGateTransactionTest,
	"GuLiStrike.Wingman.Relay.ProtocolTransactions.StrictContractGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanStrictContractGateTransactionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTransactionTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, 4u)) return false;
	const TArray<FGuLiWingmanCandidateBatch> Flights = Fixture.MakeAllFlights();
	if (!TestEqual(TEXT("Atomic baseline commits before strict gate checks"),
		Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Owner, Fixture.MakeFragment(Flights, Flights, 0u, 1u), 0.1,
			FoundCarrier(), PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted)
		|| !Fixture.AckAndActivate(*this)) return false;

	auto SubmitRejected = [&Fixture](FGuLiWingmanCandidateBatch Candidate,
		const EGuLiWingmanRejectReason ExpectedReason)
	{
		const FGuLiWingmanSubmissionResult Result = Fixture.Relay.SubmitCandidate(
			Fixture.Owner, Candidate, 0.3, FoundCarrier(), PermitWorld());
		FGuLiWingmanCandidateResultWire WireResult;
		WireResult.CandidateSequence = Result.Sequence;
		WireResult.Acceptance = Result.Acceptance;
		FGuLiWingmanCandidateResultWire WireCopy;
		return Result.RejectReason == ExpectedReason
			&& Result.Acceptance.Disposition == EGuLiWingmanSubmissionDisposition::Rejected
			&& Result.Acceptance.RejectReason == ExpectedReason
			&& Result.Acceptance.AcceptedSnapshotSequence == 1u
			&& Result.Acceptance.RebaseBaseline.AcceptedSequence == 1u
			&& Result.Acceptance.ValidatedPayloadHash == 0u
			&& Result.Acceptance.IsWellFormed()
			&& GuLiWingmanRelayWire::MakeValidatedCandidateResultCopy(WireResult, WireCopy)
			&& WireCopy.Acceptance.RejectReason == ExpectedReason
			&& WireCopy.Acceptance.RebaseBaseline.AcceptedSequence == 1u;
	};
	const FGuLiWingmanCandidateBatch Current = Fixture.MakeFlight(0u, 6u, 2u, 1u, 106u, 0.3);
	FGuLiWingmanCandidateBatch WrongConnection = Current;
	++WrongConnection.ConnectionGeneration;
	TestTrue(TEXT("Old connection generation is rejected with a typed rebase baseline"),
		SubmitRejected(WrongConnection, EGuLiWingmanRejectReason::WrongConnectionGeneration));
	FGuLiWingmanCandidateBatch WrongRoster = Current;
	++WrongRoster.RosterRevision;
	TestTrue(TEXT("Old roster revision is rejected before reserving state"),
		SubmitRejected(WrongRoster, EGuLiWingmanRejectReason::StaleRosterRevision));
	FGuLiWingmanCandidateBatch WrongNavigation = Current;
	++WrongNavigation.ObstacleRevision;
	TestTrue(TEXT("Mismatched navigation revisions are rejected"),
		SubmitRejected(WrongNavigation, EGuLiWingmanRejectReason::NavigationRevisionMismatch));
	FGuLiWingmanCandidateBatch WrongBaseline = Current;
	++WrongBaseline.BaseAcceptedSequence;
	TestTrue(TEXT("A stale Accepted baseline cannot advance the Flight"),
		SubmitRejected(WrongBaseline, EGuLiWingmanRejectReason::StaleAcceptedBaseline));
	FGuLiWingmanCandidateBatch WrongLease = Current;
	++WrongLease.LeaseEpoch;
	TestTrue(TEXT("Old lease epoch is rejected"),
		SubmitRejected(WrongLease, EGuLiWingmanRejectReason::WrongLease));
	FGuLiWingmanCandidateBatch WrongAbility = Current;
	--WrongAbility.AbilitySetRevision;
	TestTrue(TEXT("Old ability revision is rejected"),
		SubmitRejected(WrongAbility, EGuLiWingmanRejectReason::StaleAbilitySetRevision));
	FGuLiWingmanCandidateBatch PartialFlight = Current;
	PartialFlight.Samples.Pop(EAllowShrinking::No);
	PartialFlight.RequiredMemberMask &= static_cast<uint8>(~(1u << 4u));
	TestTrue(TEXT("Partial live Flight membership is rejected"),
		SubmitRejected(PartialFlight, EGuLiWingmanRejectReason::WrongFlightCoverage));

	TestEqual(TEXT("Rejected strict contracts do not advance the Flight sequence"),
		Fixture.Relay.GetAcceptedSequenceForFlight(0u), 1u);
	TestEqual(TEXT("Rejected strict contracts do not append Accepted snapshots"),
		Fixture.Relay.GetAcceptedHistory().Num(), GULI_WINGMAN_FLIGHT_COUNT);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanTrailAndGrantTransactionTest,
	"GuLiStrike.Wingman.Relay.ProtocolTransactions.TrailGrantPending",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanTrailAndGrantTransactionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTransactionTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, 2u)) return false;
	const TArray<FGuLiWingmanCandidateBatch> Flights = Fixture.MakeAllFlights();
	const FGuLiWingmanAtomicBatchAcceptance BootstrapResult = Fixture.Relay.SubmitAtomicCandidateFragment(
		Fixture.Owner, Fixture.MakeFragment(Flights, Flights, 0u, 1u), 0.1,
		FoundCarrier(), PermitWorld());
	if (!TestEqual(TEXT("Bootstrap Candidate commits"), BootstrapResult.Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted) || !Fixture.AckAndActivate(*this)) return false;

	FGuLiWingmanCandidateBatch NoGrant = Fixture.MakeFlight(0u, 6u, 2u, 1u, 106u, 0.3);
	NoGrant.RequestedRateClass = EGuLiWingmanUploadRateClass::HighRate10Hz;
	const FGuLiWingmanSubmissionResult NoGrantResult = Fixture.Relay.SubmitCandidate(
		Fixture.Owner, NoGrant, 0.3, FoundCarrier(), PermitWorld());
	TestEqual(TEXT("Requested high rate cannot self-authorize"), NoGrantResult.RejectReason,
		EGuLiWingmanRejectReason::UploadGrantMismatch);

	FGuLiWingmanUploadRateGrant Grant;
	TestTrue(TEXT("Authority combat evidence issues reliable high-rate Grant"),
		Fixture.Relay.IssueHighRateGrant(106u,
			EGuLiWingmanUploadRateGrantReason::ServerObservedCombat, 0.3, Grant));
	FGuLiWingmanCandidateBatch WithTrail = NoGrant;
	WithTrail.ObservedGrantRevision = Grant.GrantRevision;
	FGuLiWingmanCandidateTrailSample& Trail = WithTrail.TrailSamples.AddDefaulted_GetRef();
	Trail.ClientSimTick = 103u;
	Trail.CaptureEstimatedServerTimeSeconds = 0.2;
	Trail.CarrierSource = WithTrail.CarrierSource;
	Trail.Samples = Flights[0].Samples;
	for (FGuLiWingmanCandidateSample& Sample : Trail.Samples)
	{
		Sample.PositionCentimeters.X += 10;
	}
	for (FGuLiWingmanCandidateSample& Sample : WithTrail.Samples)
	{
		Sample.PositionCentimeters.X += 20;
	}
	int32 ValidatedWorldSegments = 0;
	const FGuLiCandidateWorldValidator CountWorldSegments = [&ValidatedWorldSegments](
		const FGuLiWingmanCandidateWorldValidationContext& Context)
	{
		ValidatedWorldSegments = Context.Segments.Num();
		return Context.IsWellFormed()
			? EGuLiWingmanRejectReason::None : EGuLiWingmanRejectReason::InvalidIdentity;
	};
	const FGuLiWingmanSubmissionResult TrailResult = Fixture.Relay.SubmitCandidate(
		Fixture.Owner, WithTrail, 0.3, FoundCarrier(), CountWorldSegments);
	TestEqual(TEXT("Granted Trail Candidate commits"), TrailResult.Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("Five members across two explicit segments reach the World gate"),
		ValidatedWorldSegments, 10);
	FGuLiWingmanCandidateResultWire AcceptedWire;
	AcceptedWire.CandidateSequence = TrailResult.Sequence;
	AcceptedWire.Acceptance = TrailResult.Acceptance;
	AcceptedWire.AcceptedBatch = TrailResult.AcceptedBatch;
	FGuLiWingmanCandidateResultWire AcceptedWireCopy;
	TestTrue(TEXT("Accepted DTO survives the shared remote/listen NetSerialize gate"),
		GuLiWingmanRelayWire::MakeValidatedCandidateResultCopy(AcceptedWire, AcceptedWireCopy));
	TestEqual(TEXT("Wire copy preserves the validated payload hash"),
		AcceptedWireCopy.Acceptance.ValidatedPayloadHash, TrailResult.AcceptedBatch.StableHash);
	TestEqual(TEXT("Wire copy preserves the exact Flight baseline"),
		AcceptedWireCopy.AcceptedBatch.BaseAcceptedSequence, WithTrail.BaseAcceptedSequence);
	FGuLiWingmanCandidateResultWire TamperedWire = AcceptedWire;
	TamperedWire.Acceptance.ValidatedPayloadHash ^= 1u;
	TestFalse(TEXT("A payload/acceptance hash mismatch never reaches the local apply path"),
		GuLiWingmanRelayWire::MakeValidatedCandidateResultCopy(TamperedWire, AcceptedWireCopy));

	FGuLiWingmanCandidateBatch Burst = Fixture.MakeFlight(0u, 7u, 3u, 2u, 109u, 0.35);
	Burst.RequestedRateClass = EGuLiWingmanUploadRateClass::HighRate10Hz;
	Burst.ObservedGrantRevision = Grant.GrantRevision;
	const FGuLiWingmanSubmissionResult BurstResult = Fixture.Relay.SubmitCandidate(
		Fixture.Owner, Burst, 0.35, FoundCarrier(), PermitWorld());
	TestEqual(TEXT("Even a granted client cannot exceed 10Hz"), BurstResult.RejectReason,
		EGuLiWingmanRejectReason::RateLimited);

	FGuLiWingmanCandidateBatch Pending = Fixture.MakeFlight(1u, 8u, 2u, 1u, 106u, 0.5);
	Pending.ObservedGrantRevision = Grant.GrantRevision;
	const FGuLiCarrierSourceResolver PendingCarrier = [](const FGuLiCarrierSourceRef&, FGuLiRelayCarrierState&)
	{
		return EGuLiRelayCarrierLookupResult::Pending;
	};
	TestEqual(TEXT("One unresolved Flight endpoint becomes pending"),
		Fixture.Relay.SubmitCandidate(Fixture.Owner, Pending, 0.5, PendingCarrier, PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Pending);
	FGuLiWingmanCandidateBatch Replacement = Fixture.MakeFlight(1u, 9u, 3u, 1u, 112u, 0.7);
	Replacement.ObservedGrantRevision = Grant.GrantRevision;
	TestEqual(TEXT("Newer same-Flight packet evicts pending and can commit"),
		Fixture.Relay.SubmitCandidate(Fixture.Owner, Replacement, 0.7, FoundCarrier(), PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted);
	TArray<FGuLiWingmanSubmissionResult> Deferred;
	Fixture.Relay.DrainDeferredCandidateResults(Deferred);
	TestTrue(TEXT("Evicted pending packet receives a rejection"), Deferred.Num() == 1
		&& Deferred[0].Sequence == Pending.CandidateSequence
		&& Deferred[0].Disposition == EGuLiWingmanSubmissionDisposition::Rejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanAtomicAssemblerDeadlineTest,
	"GuLiStrike.Wingman.Relay.ProtocolTransactions.AssemblyDeadline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanAtomicAssemblerDeadlineTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTransactionTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, 3u)) return false;
	const TArray<FGuLiWingmanCandidateBatch> Flights = Fixture.MakeAllFlights();
	TArray<FGuLiWingmanCandidateBatch> First;
	First.Add(Flights[0]);
	FGuLiWingmanAtomicCandidateAssembler Assembler;
	TArray<FGuLiWingmanCandidateBatch> OutFlights;
	EGuLiWingmanRejectReason RejectReason = EGuLiWingmanRejectReason::None;
	TestEqual(TEXT("Incomplete first fragment is retained"), Assembler.SubmitFragment(
		Fixture.MakeFragment(Flights, First, 0u, 2u), 1.0, OutFlights, RejectReason),
		EGuLiAtomicCandidateAssemblyDisposition::Pending);
	FGuLiWingmanAtomicBatchAcceptance Expired;
	TestFalse(TEXT("Assembly remains alive before 0.5 seconds"), Assembler.Expire(1.499, Expired));
	TestTrue(TEXT("Assembly expires exactly at 0.5 seconds"), Assembler.Expire(1.5, Expired));
	TestEqual(TEXT("Deadline reports explicit atomic expiry"), Expired.RejectReason,
		EGuLiWingmanRejectReason::AtomicBatchExpired);
	TestFalse(TEXT("Expired fragments leave no retained transaction"), Assembler.IsPending());
	return true;
}
#endif
