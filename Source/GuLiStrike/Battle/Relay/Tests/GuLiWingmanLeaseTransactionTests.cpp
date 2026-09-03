// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Combat/GuLiWingmanReplenishmentController.h"
#include "Battle/Relay/GuLiWingmanRelayAuthorityRegistry.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Misc/AutomationTest.h"

namespace GuLiWingmanLeaseTransactionTests
{
	FGuLiWingmanGroupHandle MakeGroup(const uint32 Seed = 1u)
	{
		FGuLiWingmanGroupHandle Group;
		Group.ShipInstanceId = FGuid(0x71000000u + Seed, 0x72000000u, 0x73000000u, 0x74000000u);
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
			return Test.TestTrue(TEXT("Initialize strict lease fixture"), Relay.InitializeGroup(
				77u, Group, Owner, Backup, MakeConfig(Group), 0.0))
				&& Test.TestTrue(TEXT("Configure strict Flight contract"),
					Relay.ConfigureStrictFlightContract(9u, MakeRevisions(), 0.0))
				&& Test.TestTrue(TEXT("Freeze initial Bootstrap"), Relay.BuildBootstrap(Bootstrap));
		}

		FGuLiWingmanCandidateBatch MakeFlight(
			const uint8 FlightIndex,
			const uint32 CandidateSequence,
			const uint32 FrameSequence,
			const uint32 ClientTick,
			const double CaptureTime) const
		{
			FGuLiWingmanCandidateBatch Candidate;
			Candidate.MatchEpoch = Relay.GetMatchEpoch();
			Candidate.ConnectionGeneration = Relay.GetConnectionGeneration();
			Candidate.Group = Relay.GetLeaseState().Group;
			Candidate.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
			Candidate.RosterRevision = Relay.GetRosterRevision();
			Candidate.FlightIndex = FlightIndex;
			Candidate.RequiredMemberMask = 0u;
			Candidate.RequestedRateClass = EGuLiWingmanUploadRateClass::Cruise5Hz;
			Candidate.ObservedGrantRevision = Relay.GetUploadRateGrant().GrantRevision;
			Candidate.CandidateSequence = CandidateSequence;
			Candidate.FrameSequence = FrameSequence;
			Candidate.BaseAcceptedSequence = Relay.GetAcceptedSequenceForFlight(FlightIndex);
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
				if (Entry.bDead || Entry.Wingman.Flight.FlightIndex != FlightIndex)
				{
					continue;
				}
				const FGuLiWingmanHealthEntry* Health = Relay.GetHealth().FindByPredicate(
					[&Entry](const FGuLiWingmanHealthEntry& Value)
					{
						return Value.Wingman == Entry.Wingman;
					});
				if (!Health || Health->CurrentHealthPermille == 0u)
				{
					continue;
				}
				FGuLiWingmanCandidateSample& Sample = Candidate.Samples.AddDefaulted_GetRef();
				Sample.Wingman = Entry.Wingman;
				Sample.PositionCentimeters = FIntVector(
					static_cast<int32>(FlightIndex) * 1000,
					static_cast<int32>(Entry.Wingman.MemberIndex) * 100,
					1000);
				Sample.VelocityCentimetersPerSecond = FIntVector(100, 0, 0);
				Sample.RotationCentiDegrees = FIntVector::ZeroValue;
				Sample.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Follow);
				Candidate.RequiredMemberMask |= static_cast<uint8>(1u << Entry.Wingman.MemberIndex);
			}
			return Candidate;
		}

		TArray<FGuLiWingmanCandidateBatch> MakeAllFlights(
			const uint32 FirstSequence,
			const uint32 FrameSequence,
			const uint32 ClientTick,
			const double CaptureTime) const
		{
			TArray<FGuLiWingmanCandidateBatch> Flights;
			for (uint8 Flight = 0u; Flight < GULI_WINGMAN_FLIGHT_COUNT; ++Flight)
			{
				Flights.Add(MakeFlight(
					Flight, FirstSequence + Flight, FrameSequence, ClientTick, CaptureTime));
			}
			return Flights;
		}

		FGuLiWingmanAtomicCandidateBatchFragment MakeFragment(
			const FGuLiWingmanBootstrapBundle& Frozen,
			const TArray<FGuLiWingmanCandidateBatch>& AllFlights,
			const TArray<FGuLiWingmanCandidateBatch>& FragmentFlights,
			const uint8 FragmentIndex,
			const uint8 FragmentCount,
			const uint64 BatchId) const
		{
			FGuLiWingmanAtomicCandidateBatchFragment Fragment;
			Fragment.Header.BatchId = BatchId;
			Fragment.Header.BatchKind = Frozen.AtomicBatchKind;
			Fragment.Header.Group = Relay.GetLeaseState().Group;
			Fragment.Header.ConnectionGeneration = Relay.GetConnectionGeneration();
			Fragment.Header.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
			Fragment.Header.FrozenRosterRevision = Frozen.RosterRevision;
			Fragment.Header.FrozenRequiredFlightMask = Frozen.RequiredFlightMask;
			Fragment.Header.FrozenRequiredMemberMaskHash = Frozen.RequiredMemberMaskHash;
			Fragment.Header.BaselineRevision = Frozen.AtomicBaselineRevision;
			Fragment.Header.BaselineHash = Frozen.AtomicBaselineHash;
			Fragment.Header.IncludedFlightMask = Frozen.RequiredFlightMask;
			Fragment.Header.ClientBatchStartTick = AllFlights.IsEmpty()
				? 0u : AllFlights[0].ClientSimTick;
			Fragment.Header.FragmentCount = FragmentCount;
			Fragment.Header.BatchPayloadHash = GuLiWingmanRelayHash::CandidatePayloads(AllFlights);
			Fragment.Flights = FragmentFlights;
			Fragment.FragmentIndex = FragmentIndex;
			uint32 TotalBytes = 0u;
			for (const FGuLiWingmanCandidateBatch& Flight : AllFlights)
			{
				FGuLiWingmanAtomicCandidateBatchFragment Single;
				Single.Flights.Add(Flight);
				TotalBytes += Single.EstimatePayloadBytes();
			}
			Fragment.Header.BatchPayloadBytes = TotalBytes;
			return Fragment;
		}

		bool AckFrozen(FAutomationTestBase& Test, const FGuid& Sender, const double NowSeconds)
		{
			FGuLiGroupAbilityConfigAck AbilityAck;
			AbilityAck.Group = Bootstrap.Commit.Group;
			AbilityAck.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
			AbilityAck.SnapshotRevision = Bootstrap.AbilityConfig.SnapshotRevision;
			AbilityAck.SnapshotHash = Bootstrap.AbilityConfig.SnapshotHash;
			const FGuLiWingmanTransferBaseline* Transfer = Bootstrap.bHasTransferBaseline
				? &Bootstrap.TransferBaseline : nullptr;
			return Test.TestTrue(TEXT("ACK exact ability projection"),
				Relay.AcknowledgeAbilityConfig(Sender, AbilityAck, NowSeconds))
				&& Test.TestTrue(TEXT("ACK exact six-scope baseline"),
					Relay.AcknowledgeBootstrap(
						Sender, Bootstrap.Commit, Transfer, NowSeconds + 0.001));
		}

		FGuLiWingmanAtomicBatchAcceptance SubmitWholeAtomic(
			const FGuid& Sender,
			const TArray<FGuLiWingmanCandidateBatch>& Flights,
			const uint64 BatchId,
			const double NowSeconds)
		{
			return Relay.SubmitAtomicCandidateFragment(
				Sender, MakeFragment(Bootstrap, Flights, Flights, 0u, 1u, BatchId),
				NowSeconds, FoundCarrier(), PermitWorld());
		}

		bool Activate(FAutomationTestBase& Test)
		{
			const TArray<FGuLiWingmanCandidateBatch> Flights = MakeAllFlights(1u, 1u, 100u, 0.1);
			return Test.TestEqual(TEXT("Initial full-Flight atomic Bootstrap is accepted"),
				SubmitWholeAtomic(Owner, Flights, 1u, 0.1).Disposition,
				EGuLiWingmanSubmissionDisposition::Accepted)
				&& AckFrozen(Test, Owner, 0.11)
				&& Test.TestEqual(TEXT("Initial group becomes Active"),
					Relay.GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Active);
		}
	};

	const FGuLiWingmanLeaseEvent* FindLastEvent(
		const FGuLiWingmanRelayServer& Relay, const EGuLiWingmanLeaseEventType Type)
	{
		for (int32 Index = Relay.GetLeaseEvents().Num() - 1; Index >= 0; --Index)
		{
			if (Relay.GetLeaseEvents()[Index].Type == Type)
			{
				return &Relay.GetLeaseEvents()[Index];
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanLeaseExactCadenceAndInitialDeadlineTest,
	"GuLiStrike.Wingman.LeaseTransactions.ExactCadenceAndInitialDeadline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanLeaseExactCadenceAndInitialDeadlineTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanLeaseTransactionTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, 11u)) return false;
	const double OriginalDeadline = Fixture.Relay.GetInitialCandidateDeadlineSeconds();
	const TArray<FGuLiWingmanCandidateBatch> Flights =
		Fixture.MakeAllFlights(1u, 1u, 100u, 0.2);
	TArray<FGuLiWingmanCandidateBatch> FirstTwo;
	FirstTwo.Append(Flights.GetData(), 2);
	TestEqual(TEXT("Partial initial batch is pending"),
		Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Owner, Fixture.MakeFragment(Fixture.Bootstrap, Flights, FirstTwo, 0u, 2u, 10u),
			0.2, FoundCarrier(), PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Pending);
	TestFalse(TEXT("A sub-second call cannot run lease maintenance"),
		Fixture.Relay.RunLeaseMaintenance(0.99));
	TestTrue(TEXT("The exact 1 second boundary runs once"), Fixture.Relay.RunLeaseMaintenance(1.0));
	TestFalse(TEXT("Repeated early calls cannot increase watchdog cadence"),
		Fixture.Relay.RunLeaseMaintenance(1.5));
	TestTrue(TEXT("The next exact boundary runs once"), Fixture.Relay.RunLeaseMaintenance(2.0));
	TestTrue(TEXT("Roster death cancels the partial assembler"),
		Fixture.Relay.MarkWingmanDead(Fixture.Relay.GetRoster()[0].Wingman));
	TestEqual(TEXT("Roster revision never resets the original Initial deadline"),
		Fixture.Relay.GetInitialCandidateDeadlineSeconds(), OriginalDeadline);
	TestTrue(TEXT("Revised initial baseline can be rebuilt"), Fixture.Relay.BuildBootstrap(Fixture.Bootstrap));
	FGuLiGroupAbilityConfigAck ExpiredAck;
	ExpiredAck.Group = Fixture.Bootstrap.Commit.Group;
	ExpiredAck.LeaseEpoch = Fixture.Relay.GetLeaseState().LeaseEpoch;
	ExpiredAck.SnapshotRevision = Fixture.Bootstrap.AbilityConfig.SnapshotRevision;
	ExpiredAck.SnapshotHash = Fixture.Bootstrap.AbilityConfig.SnapshotHash;
	TestFalse(TEXT("ACK at the exact expired entry boundary is rejected immediately"),
		Fixture.Relay.AcknowledgeAbilityConfig(Fixture.Owner, ExpiredAck, 3.0));
	TestTrue(TEXT("First maintenance after the deadline revokes Initializing"),
		Fixture.Relay.RunLeaseMaintenance(3.6));
	TestEqual(TEXT("Initial deadline failure is terminal"), Fixture.Relay.GetLeaseState().Lifecycle,
		EGuLiWingmanGroupLifecycle::Revoked);
	TestEqual(TEXT("Exactly three 1 Hz maintenance executions occurred"),
		Fixture.Relay.GetLeaseMaintenanceExecutionCount(), 3ull);
	const FGuLiWingmanLeaseEvent* Expired = FindLastEvent(
		Fixture.Relay, EGuLiWingmanLeaseEventType::InitialCandidateExpired);
	TestNotNull(TEXT("Initial expiry has an audit event"), Expired);
	if (Expired)
	{
		TestEqual(TEXT("Event records the theoretical 3 second deadline"),
			Expired->TheoreticalDeadlineSeconds, 3.0);
		TestTrue(TEXT("Detection lag is explicit and bounded by the 1 Hz interval plus frame"),
			Expired->DetectionLagSeconds >= 0.0 && Expired->DetectionLagSeconds <= 1.0);
	}
	TestEqual(TEXT("Partial/deadline handling never writes server movement"),
		Fixture.Relay.GetServerWingmanMovementWriteCount(), 0ull);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanLeaseFreshnessHeartbeatOfferTest,
	"GuLiStrike.Wingman.LeaseTransactions.FreshnessHeartbeatAndPendingOffer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanLeaseFreshnessHeartbeatOfferTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanLeaseTransactionTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, 12u) || !Fixture.Activate(*this)) return false;
	const double OriginalFreshness = Fixture.Relay.GetLastValidCandidateTimeForFlight(0u);
	FGuLiWingmanPendingLeaseOffer Offer;
	TestTrue(TEXT("Backup Offer preview starts"), Fixture.Relay.BeginLeaseOffer(
		Fixture.Backup, FGuid(21u, 22u, 23u, 24u), 0.2, Offer));
	TestTrue(TEXT("Heartbeat proves connection liveness"), Fixture.Relay.RecordLeaseHeartbeat(
		Fixture.Owner, Fixture.Relay.GetConnectionGeneration(),
		Fixture.Relay.GetLeaseState().LeaseEpoch, 0.9));
	TestEqual(TEXT("Heartbeat never refreshes movement freshness"),
		Fixture.Relay.GetLastValidCandidateTimeForFlight(0u), OriginalFreshness);
	TestTrue(TEXT("First watchdog still sees Active"), Fixture.Relay.RunLeaseMaintenance(1.0));
	TestEqual(TEXT("Freshness below 1 second remains Active"), Fixture.Relay.GetLeaseState().Lifecycle,
		EGuLiWingmanGroupLifecycle::Active);
	TestTrue(TEXT("Second watchdog detects Stale"), Fixture.Relay.RunLeaseMaintenance(2.0));
	TestEqual(TEXT("1 second threshold is Stale"), Fixture.Relay.GetLeaseState().Lifecycle,
		EGuLiWingmanGroupLifecycle::Stale);
	TestTrue(TEXT("Pending Offer remains independent from old Active freshness"),
		Fixture.Relay.GetPendingLeaseOffer().IsPending());
	TestTrue(TEXT("Third watchdog detects Unavailable"), Fixture.Relay.RunLeaseMaintenance(3.0));
	TestEqual(TEXT("2 second threshold is Unavailable"), Fixture.Relay.GetLeaseState().Lifecycle,
		EGuLiWingmanGroupLifecycle::Unavailable);
	TestTrue(TEXT("Offer still does not pause or replace the old lease"),
		Fixture.Relay.GetLeaseState().OwnerPlayerGuid == Fixture.Owner
		&& Fixture.Relay.GetPendingLeaseOffer().IsPending());
	TestTrue(TEXT("Fourth watchdog expires both silence and the unready Offer"),
		Fixture.Relay.RunLeaseMaintenance(4.0));
	TestTrue(TEXT("3 second threshold revokes only the active lease"),
		Fixture.Relay.IsActiveLeaseRevoked());
	TestFalse(TEXT("Expired Offer is removed"), Fixture.Relay.GetPendingLeaseOffer().IsPending());
	for (const EGuLiWingmanLeaseEventType Type : {
		EGuLiWingmanLeaseEventType::BecameStale,
		EGuLiWingmanLeaseEventType::BecameUnavailable,
		EGuLiWingmanLeaseEventType::ActiveLeaseRevoked})
	{
		const FGuLiWingmanLeaseEvent* Event = FindLastEvent(Fixture.Relay, Type);
		TestNotNull(TEXT("Freshness transition records deadline and lag"), Event);
		if (Event)
		{
			TestTrue(TEXT("1 Hz detection lag is in [0,1] seconds"),
				Event->DetectionLagSeconds >= 0.0 && Event->DetectionLagSeconds <= 1.0);
		}
	}
	TestEqual(TEXT("Heartbeat/watchdog paths have zero server movement"),
		Fixture.Relay.GetServerWingmanMovementWriteCount(), 0ull);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanLeaseGracefulTransferAtomicityTest,
	"GuLiStrike.Wingman.LeaseTransactions.GracefulTransferAckThenAtomicBatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanLeaseGracefulTransferAtomicityTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanLeaseTransactionTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, 13u) || !Fixture.Activate(*this)) return false;
	const FGuid NewOwner = Fixture.Backup;
	const uint32 OldLeaseEpoch = Fixture.Relay.GetLeaseState().LeaseEpoch;
	FGuLiWingmanPendingLeaseOffer Offer;
	TestTrue(TEXT("Graceful Offer is only a preview"),
		Fixture.Relay.BeginLeaseOffer(NewOwner, Fixture.Owner, 0.2, Offer));
	const uint64 PreviewAcceptedHash = Offer.PreviewAcceptedSnapshotHash;
	FGuLiWingmanCandidateBatch OldOwnerPacket = Fixture.MakeFlight(0u, 6u, 2u, 106u, 0.3);
	TestEqual(TEXT("Old Active owner continues while Offer waits"),
		Fixture.Relay.SubmitCandidate(
			Fixture.Owner, OldOwnerPacket, 0.3, FoundCarrier(), PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted);
	OldOwnerPacket = Fixture.MakeFlight(1u, 7u, 2u, 106u, 0.4);
	TestTrue(TEXT("Exact Ready commits the transfer"),
		Fixture.Relay.AcknowledgeLeaseOfferReady(NewOwner, Offer.OfferRevision, 0.5));
	TestTrue(TEXT("Commit permanently invalidates the prior lease tuple"),
		Fixture.Relay.GetLeaseState().OwnerPlayerGuid == NewOwner
		&& Fixture.Relay.GetLeaseState().LeaseEpoch > OldLeaseEpoch);
	TestEqual(TEXT("Old owner Candidate is rejected at the entry gate"),
		Fixture.Relay.SubmitCandidate(
			Fixture.Owner, OldOwnerPacket, 0.51, FoundCarrier(), PermitWorld()).RejectReason,
		EGuLiWingmanRejectReason::WrongLease);
	FGuLiWingmanFireIntent OldFire;
	OldFire.Group = Fixture.Relay.GetLeaseState().Group;
	OldFire.MatchEpoch = Fixture.Relay.GetMatchEpoch();
	OldFire.LeaseEpoch = OldLeaseEpoch;
	OldFire.Emitter = Fixture.Relay.GetRoster()[0].Wingman;
	OldFire.DomainFireSequence = 1u;
	TestEqual(TEXT("Old owner FireIntent is rejected after commit"),
		Fixture.Relay.SubmitFireIntent(Fixture.Owner, OldFire, 0.52).RejectReason,
		EGuLiWingmanRejectReason::WrongLease);
	Fixture.Owner = NewOwner;
	TestTrue(TEXT("Commit freezes the exact six-scope takeover cut"),
		Fixture.Relay.BuildBootstrap(Fixture.Bootstrap));
	const FGuLiWingmanActiveLeaseTransaction& Transaction =
		Fixture.Relay.GetActiveLeaseTransaction();
	TestTrue(TEXT("Commit-time accepted baseline includes the late old-owner packet"),
		Transaction.FrozenAcceptedSnapshotHash != PreviewAcceptedHash);
	TestTrue(TEXT("Roster, ability, pose and fire high-water are frozen"),
		Transaction.FrozenRosterRevision == Fixture.Bootstrap.RosterRevision
		&& Transaction.FrozenBaselineRevision == Fixture.Bootstrap.AtomicBaselineRevision
		&& Transaction.FrozenBaselineHash == Fixture.Bootstrap.AtomicBaselineHash
		&& Transaction.FrozenAbilityConfigHash == Fixture.Bootstrap.AbilityConfig.SnapshotHash
		&& Transaction.FrozenFireHighWaterHash != 0u);
	const TArray<FGuLiWingmanCandidateBatch> Flights =
		Fixture.MakeAllFlights(8u, 1u, 112u, 0.7);
	const int32 HistoryBefore = Fixture.Relay.GetAcceptedHistory().Num();
	TestEqual(TEXT("Takeover Batch cannot precede exact baseline ACK"),
		Fixture.SubmitWholeAtomic(NewOwner, Flights, 20u, 0.7).Disposition,
		EGuLiWingmanSubmissionDisposition::Rejected);
	TestEqual(TEXT("Pre-ACK rejection does not change Accepted Store"),
		Fixture.Relay.GetAcceptedHistory().Num(), HistoryBefore);
	if (!Fixture.AckFrozen(*this, NewOwner, 0.8)) return false;
	TArray<FGuLiWingmanCandidateBatch> FirstTwo;
	FirstTwo.Append(Flights.GetData(), 2);
	TArray<FGuLiWingmanCandidateBatch> LastThree;
	LastThree.Append(Flights.GetData() + 2, 3);
	TestEqual(TEXT("Partial takeover Batch waits atomically"),
		Fixture.Relay.SubmitAtomicCandidateFragment(
			NewOwner, Fixture.MakeFragment(Fixture.Bootstrap, Flights, FirstTwo, 0u, 2u, 21u),
			0.9, FoundCarrier(), PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Pending);
	TestEqual(TEXT("Partial Batch changes no pose Store"),
		Fixture.Relay.GetAcceptedHistory().Num(), HistoryBefore);
	TestEqual(TEXT("Completed takeover Batch commits all Flights"),
		Fixture.Relay.SubmitAtomicCandidateFragment(
			NewOwner, Fixture.MakeFragment(Fixture.Bootstrap, Flights, LastThree, 1u, 2u, 21u),
			0.9, FoundCarrier(), PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("Takeover becomes Active atomically"), Fixture.Relay.GetLeaseState().Lifecycle,
		EGuLiWingmanGroupLifecycle::Active);
	for (uint8 Flight = 0u; Flight < GULI_WINGMAN_FLIGHT_COUNT; ++Flight)
	{
		TestEqual(TEXT("All Flight LastValid clocks share one accepted server time"),
			Fixture.Relay.GetLastValidCandidateTimeForFlight(Flight), 0.9);
	}
	TestTrue(TEXT("First scheduled maintenance after success remains Active"),
		Fixture.Relay.RunLeaseMaintenance(1.0));
	TestEqual(TEXT("First maintenance does not falsely stale a new takeover"),
		Fixture.Relay.GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Active);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanLeaseResumeRevisionAndReplenishTest,
	"GuLiStrike.Wingman.LeaseTransactions.ResumeRevisionAndDeferredReplenish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanLeaseResumeRevisionAndReplenishTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanLeaseTransactionTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, 14u) || !Fixture.Activate(*this)) return false;
	FGuLiWingmanReplenishmentController Replenishment;
	TArray<FGuLiWingmanReplenishmentResult> Replenished;
	bool bRosterChanged = false;
	const FGuLiWingmanHandle Dead = Fixture.Relay.GetRoster()[0].Wingman;
	TestTrue(TEXT("One stable slot dies"), Fixture.Relay.MarkWingmanDead(Dead));
	Replenishment.Advance(Fixture.Relay, 0.2, Replenished, bRosterChanged, 0.5);
	TestTrue(TEXT("First watchdog remains Active"), Fixture.Relay.RunLeaseMaintenance(1.0));
	TestTrue(TEXT("Second watchdog detects Stale"), Fixture.Relay.RunLeaseMaintenance(2.0));
	TestEqual(TEXT("Stale group does not release a due replenishment"),
		Replenishment.Advance(Fixture.Relay, 2.0, Replenished, bRosterChanged, 0.5), 0);
	TestEqual(TEXT("Due ScheduleId is queued while non-Active"),
		Replenishment.GetQueuedDueSlotCount(), 1);
	const uint32 LeaseEpochBefore = Fixture.Relay.GetLeaseState().LeaseEpoch;
	TestTrue(TEXT("Original owner starts Resume without a takeover deadline"),
		Fixture.Relay.BeginResume(2.1));
	TestTrue(TEXT("Resume freezes a fresh baseline"), Fixture.Relay.BuildBootstrap(Fixture.Bootstrap));
	TestEqual(TEXT("Resume preserves the original lease epoch"),
		Fixture.Relay.GetLeaseState().LeaseEpoch, LeaseEpochBefore);
	TestEqual(TEXT("Resume has no takeover Overall deadline"),
		Fixture.Relay.GetActiveLeaseTransaction().OverallDeadlineSeconds, 0.0);
	if (!Fixture.AckFrozen(*this, Fixture.Owner, 2.2)) return false;
	TArray<FGuLiWingmanCandidateBatch> Flights =
		Fixture.MakeAllFlights(6u, 2u, 106u, 2.3);
	TArray<FGuLiWingmanCandidateBatch> FirstTwo;
	FirstTwo.Append(Flights.GetData(), 2);
	const int32 HistoryBeforePartial = Fixture.Relay.GetAcceptedHistory().Num();
	TestEqual(TEXT("Partial Resume Batch is pending"),
		Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Owner, Fixture.MakeFragment(Fixture.Bootstrap, Flights, FirstTwo, 0u, 2u, 30u),
			2.3, FoundCarrier(), PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Pending);
	Fixture.Relay.AdvancePacketDeadlines(2.35, FoundCarrier());
	const FGuLiWingmanBootstrapBundle OldBaseline = Fixture.Bootstrap;
	TestTrue(TEXT("Death while waiting Batch revises the roster"),
		Fixture.Relay.MarkWingmanDead(Fixture.Relay.GetRoster()[1].Wingman));
	TestEqual(TEXT("Revision cancels the pending assembler without committing"),
		Fixture.Relay.GetAcceptedHistory().Num(), HistoryBeforePartial);
	TestEqual(TEXT("Resume revision does not invent a takeover ACK deadline"),
		Fixture.Relay.GetActiveLeaseTransaction().BaselineAckDeadlineSeconds, 0.0);
	TestTrue(TEXT("Resume re-freezes after revision"), Fixture.Relay.BuildBootstrap(Fixture.Bootstrap));
	TestTrue(TEXT("Revised baseline identity changes"),
		Fixture.Bootstrap.AtomicBaselineHash != OldBaseline.AtomicBaselineHash);
	TestEqual(TEXT("Old baseline fragment is rejected immediately"),
		Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Owner, Fixture.MakeFragment(OldBaseline, Flights, Flights, 0u, 1u, 31u),
			2.36, FoundCarrier(), PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Rejected);
	if (!Fixture.AckFrozen(*this, Fixture.Owner, 2.4)) return false;
	Flights = Fixture.MakeAllFlights(6u, 2u, 106u, 2.5);
	TestEqual(TEXT("Revised Resume commits atomically before the original 3 second revoke"),
		Fixture.SubmitWholeAtomic(Fixture.Owner, Flights, 32u, 2.5).Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("Resume restores Active"), Fixture.Relay.GetLeaseState().Lifecycle,
		EGuLiWingmanGroupLifecycle::Active);
	TestEqual(TEXT("Queued replenishment releases exactly once after Active"),
		Replenishment.Advance(Fixture.Relay, 2.51, Replenished, bRosterChanged, 0.5), 1);
	const uint64 ReleasedScheduleId = Replenished.IsEmpty() ? 0u : Replenished[0].ScheduleId;
	TestTrue(TEXT("Released replenishment has a stable non-zero ScheduleId"), ReleasedScheduleId != 0u);
	TestEqual(TEXT("The same ScheduleId cannot release twice"),
		Replenishment.Advance(Fixture.Relay, 2.6, Replenished, bRosterChanged, 0.5), 0);
	TestEqual(TEXT("Resume/replenish paths have zero server movement writes"),
		Fixture.Relay.GetServerWingmanMovementWriteCount(), 0ull);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanLeaseTakeoverRevisionDeadlineTest,
	"GuLiStrike.Wingman.LeaseTransactions.TakeoverRevisionReplacesOldDeadline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanLeaseTakeoverRevisionDeadlineTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanLeaseTransactionTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, 15u) || !Fixture.Activate(*this)) return false;
	FGuLiWingmanPendingLeaseOffer Offer;
	TestTrue(TEXT("Takeover Offer starts"),
		Fixture.Relay.BeginLeaseOffer(Fixture.Backup, Fixture.Owner, 0.2, Offer));
	TestTrue(TEXT("Takeover Offer commits on exact Ready"),
		Fixture.Relay.AcknowledgeLeaseOfferReady(Fixture.Backup, Offer.OfferRevision, 0.3));
	Fixture.Owner = Fixture.Backup;
	TestTrue(TEXT("Takeover baseline builds"), Fixture.Relay.BuildBootstrap(Fixture.Bootstrap));
	if (!Fixture.AckFrozen(*this, Fixture.Owner, 0.4)) return false;
	const double OldCandidateDeadline =
		Fixture.Relay.GetActiveLeaseTransaction().CandidateDeadlineSeconds;
	TArray<FGuLiWingmanCandidateBatch> Flights =
		Fixture.MakeAllFlights(6u, 1u, 106u, 0.5);
	TArray<FGuLiWingmanCandidateBatch> FirstTwo;
	FirstTwo.Append(Flights.GetData(), 2);
	TestEqual(TEXT("Old takeover assembler becomes pending"),
		Fixture.Relay.SubmitAtomicCandidateFragment(
			Fixture.Owner, Fixture.MakeFragment(Fixture.Bootstrap, Flights, FirstTwo, 0u, 2u, 40u),
			0.5, FoundCarrier(), PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Pending);
	Fixture.Relay.AdvancePacketDeadlines(2.9, FoundCarrier());
	TestTrue(TEXT("Death cancels assembler and reopens revised baseline ACK"),
		Fixture.Relay.MarkWingmanDead(Fixture.Relay.GetRoster()[0].Wingman));
	const double RevisedAckDeadline =
		Fixture.Relay.GetActiveLeaseTransaction().BaselineAckDeadlineSeconds;
	TestTrue(TEXT("Revised ACK deadline replaces the old candidate deadline"),
		RevisedAckDeadline > OldCandidateDeadline);
	TestTrue(TEXT("Revised takeover baseline builds"), Fixture.Relay.BuildBootstrap(Fixture.Bootstrap));
	if (!Fixture.AckFrozen(*this, Fixture.Owner, 3.5)) return false;
	const double RevisedCandidateDeadline =
		Fixture.Relay.GetActiveLeaseTransaction().CandidateDeadlineSeconds;
	TestTrue(TEXT("Revised candidate deadline is capped by original Overall"),
		RevisedCandidateDeadline > OldCandidateDeadline
		&& RevisedCandidateDeadline <= Offer.OverallDeadlineSeconds);
	TestTrue(TEXT("Old deadline cannot revoke the revised transaction"),
		Fixture.Relay.RunLeaseMaintenance(4.0));
	TestEqual(TEXT("Revised transaction is still awaiting its Batch"),
		Fixture.Relay.GetActiveLeaseTransaction().State,
		EGuLiWingmanActiveLeaseTransactionState::AwaitingTakeoverBatch);
	Flights = Fixture.MakeAllFlights(6u, 1u, 106u, 4.1);
	TestEqual(TEXT("Revised takeover commits before its new deadline"),
		Fixture.SubmitWholeAtomic(Fixture.Owner, Flights, 41u, 4.1).Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("Revised takeover becomes Active"), Fixture.Relay.GetLeaseState().Lifecycle,
		EGuLiWingmanGroupLifecycle::Active);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanLeaseRegistryRotationNoOwnerTest,
	"GuLiStrike.Wingman.LeaseTransactions.RegistryRotationAndNoOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanLeaseRegistryRotationNoOwnerTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanLeaseTransactionTests;
	FGuLiWingmanRelayAuthorityRegistry Registry;
	const FGuLiWingmanGroupHandle Group = MakeGroup(16u);
	const FGuid Owner(100u, 101u, 102u, 103u);
	const FGuid FirstBackup(200u, 201u, 202u, 203u);
	const FGuid SecondBackup(300u, 301u, 302u, 303u);
	FGuLiWingmanRelayServer* Relay = Registry.CreateGroup(
		88u, Group, Owner, FirstBackup, MakeConfig(Group), 0.0);
	if (!TestNotNull(TEXT("Registry creates retained group"), Relay)) return false;
	FGuLiWingmanBootstrapBundle Initial;
	TestTrue(TEXT("Legacy fixture Bootstrap builds"), Relay->BuildBootstrap(Initial));
	FGuLiGroupAbilityConfigAck AbilityAck;
	AbilityAck.Group = Group;
	AbilityAck.LeaseEpoch = Relay->GetLeaseState().LeaseEpoch;
	AbilityAck.SnapshotRevision = Relay->GetAbilityConfig().SnapshotRevision;
	AbilityAck.SnapshotHash = Relay->GetAbilityConfig().SnapshotHash;
	TestTrue(TEXT("Legacy fixture ability ACK"), Relay->AcknowledgeAbilityConfig(Owner, AbilityAck, 0.01));
	TestTrue(TEXT("Legacy fixture Bootstrap ACK"),
		Relay->AcknowledgeBootstrap(Owner, Initial.Commit, nullptr, 0.02));
	const TArray<FGuLiWingmanOwnerLossAssignment> Loss = Registry.HandleOwnerDisconnected(
		Owner, {SecondBackup, FirstBackup}, 0.2);
	TestEqual(TEXT("Disconnect issues one reliable Offer"), Loss.Num(), 1);
	if (Loss.IsEmpty()) return false;
	TestEqual(TEXT("Configured backup receives the first Offer"),
		Loss[0].NewOwnerPlayerGuid, FirstBackup);
	FGuLiWingmanOwnerLossAssignment LateCommit;
	TestFalse(TEXT("Late Ready is rejected at entry without waiting for maintenance"),
		Registry.AcknowledgeLeaseOfferReady(
			Group, FirstBackup, Loss[0].OfferRevision, Loss[0].ReadyDeadlineSeconds,
			LateCommit));
	Registry.RunLeaseMaintenance(1.0);
	Registry.RunLeaseMaintenance(2.0);
	Registry.RunLeaseMaintenance(3.0);
	const TArray<FGuLiWingmanOwnerLossAssignment> Rotated = Registry.RunLeaseMaintenance(4.0);
	TestEqual(TEXT("Expired Offer rotates exactly once"), Rotated.Num(), 1);
	if (!Rotated.IsEmpty())
	{
		TestEqual(TEXT("Next deterministic backup receives the rotated Offer"),
			Rotated[0].NewOwnerPlayerGuid, SecondBackup);
	}
	Registry.RunLeaseMaintenance(5.0);
	Registry.RunLeaseMaintenance(6.0);
	Registry.RunLeaseMaintenance(7.0);
	TestEqual(TEXT("Exhausted candidates enter explicit NoOwner"),
		Relay->GetActiveLeaseTransaction().State,
		EGuLiWingmanActiveLeaseTransactionState::NoOwner);
	TestEqual(TEXT("NoOwner is Unavailable but retains the authoritative group"),
		Relay->GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Unavailable);
	TestTrue(TEXT("Registry retains the same group core"), Registry.FindGroup(Group) == Relay);
	TestEqual(TEXT("Disconnect/rotation has zero server movement writes"),
		Relay->GetServerWingmanMovementWriteCount(), 0ull);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanActiveRosterCutTest,
	"GuLiStrike.Wingman.LeaseTransactions.ActiveRosterCutPreservesFlightState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanActiveRosterCutTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanLeaseTransactionTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, 17u) || !Fixture.Activate(*this)) return false;

	const uint32 FlightZeroSequence = Fixture.Relay.GetAcceptedSequenceForFlight(0u);
	const uint32 FlightOneSequence = Fixture.Relay.GetAcceptedSequenceForFlight(1u);
	const double FlightZeroFreshness = Fixture.Relay.GetLastValidCandidateTimeForFlight(0u);
	const double FlightOneFreshness = Fixture.Relay.GetLastValidCandidateTimeForFlight(1u);
	const uint64 AcceptedHashBefore =
		GuLiWingmanRelayHash::AcceptedSnapshot(Fixture.Relay.GetAcceptedHistory());
	const FGuLiWingmanCandidateBatch OldRosterCandidate =
		Fixture.MakeFlight(0u, 20u, 2u, 106u, 0.2);
	const FGuLiWingmanHandle Dead = Fixture.Relay.GetRoster()[0].Wingman;
	TestTrue(TEXT("Active member death mutates the reliable roster"),
		Fixture.Relay.MarkWingmanDead(Dead));
	TestTrue(TEXT("Stable slot replacement advances its generation"),
		Fixture.Relay.ReplenishWingman(0u, 0u, Dead.EntityGeneration + 1u));
	const FGuLiWingmanHandle Replacement = Fixture.Relay.GetRoster()[0].Wingman;

	TestEqual(TEXT("Active roster mutation never changes Availability"),
		Fixture.Relay.GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Active);
	TestTrue(TEXT("A reliable Active roster ACK barrier is pending"),
		Fixture.Relay.IsActiveRosterCutPending());
	TestEqual(TEXT("Roster mutation preserves Flight 0 sequence"),
		Fixture.Relay.GetAcceptedSequenceForFlight(0u), FlightZeroSequence);
	TestEqual(TEXT("Roster mutation preserves Flight 1 sequence"),
		Fixture.Relay.GetAcceptedSequenceForFlight(1u), FlightOneSequence);
	TestEqual(TEXT("Roster mutation preserves Flight 0 freshness"),
		Fixture.Relay.GetLastValidCandidateTimeForFlight(0u), FlightZeroFreshness);
	TestEqual(TEXT("Roster mutation preserves Flight 1 freshness"),
		Fixture.Relay.GetLastValidCandidateTimeForFlight(1u), FlightOneFreshness);
	TestEqual(TEXT("Roster mutation preserves Accepted snapshot hash"),
		GuLiWingmanRelayHash::AcceptedSnapshot(Fixture.Relay.GetAcceptedHistory()), AcceptedHashBefore);

	TestEqual(TEXT("Old roster Candidate is rejected immediately"),
		Fixture.Relay.SubmitCandidate(
			Fixture.Owner, OldRosterCandidate, 0.2, FoundCarrier(), PermitWorld()).RejectReason,
		EGuLiWingmanRejectReason::StaleRosterRevision);
	const FGuLiWingmanCandidateBatch NewRosterCandidate =
		Fixture.MakeFlight(0u, 21u, 2u, 106u, 0.2);
	TestEqual(TEXT("New roster Candidate cannot pass before the reliable cut ACK"),
		Fixture.Relay.SubmitCandidate(
			Fixture.Owner, NewRosterCandidate, 0.2, FoundCarrier(), PermitWorld()).RejectReason,
		EGuLiWingmanRejectReason::StaleRosterRevision);

	FGuLiWingmanFireIntent PrematureFire;
	PrematureFire.MatchEpoch = Fixture.Relay.GetMatchEpoch();
	PrematureFire.Group = Fixture.Relay.GetLeaseState().Group;
	PrematureFire.LeaseEpoch = Fixture.Relay.GetLeaseState().LeaseEpoch;
	PrematureFire.DomainFireSequence = 1u;
	PrematureFire.Emitter = Replacement;
	PrematureFire.SourceAcceptedState = Fixture.Relay.GetAcceptedHistory()[0].StateRef;
	PrematureFire.ClientFireTick = PrematureFire.SourceAcceptedState.ClientSimTick + 1u;
	PrematureFire.Target.Kind = EGuLiTargetKind::CommanderSoldier;
	PrematureFire.Target.AuthorityId = FGuid(40u, 41u, 42u, 43u);
	PrematureFire.Target.Generation = 1u;
	PrematureFire.Target.LocalId = 1u;
	PrematureFire.WeaponAbilityId = Fixture.Relay.GetAbilityConfig().BasicWeaponAbilityId;
	PrematureFire.WeaponDefinitionRevision =
		Fixture.Relay.GetAbilityConfig().BasicWeaponDefinitionRevision;
	PrematureFire.AbilitySetRevision = Fixture.Relay.GetAbilityConfig().AbilitySetRevision;
	PrematureFire.AimDirectionMilli = FIntVector(1000, 0, 0);
	PrematureFire.bClientPredictedLineOfSight = true;
	TestEqual(TEXT("Replacement identity cannot fire before roster ACK"),
		Fixture.Relay.SubmitFireIntent(Fixture.Owner, PrematureFire, 0.2).RejectReason,
		EGuLiWingmanRejectReason::StaleRosterRevision);

	FGuLiWingmanBootstrapBundle RosterCut;
	TestTrue(TEXT("Server freezes a reliable Active six-scope roster cut"),
		Fixture.Relay.RefreshActiveRosterCut(0.21, RosterCut));
	TestTrue(TEXT("Roster cut is explicitly non-atomic"),
		RosterCut.bActiveRosterRefresh && !RosterCut.bRequiresAtomicCandidateBatch);
	FGuLiGroupAbilityConfigAck AbilityAck;
	AbilityAck.Group = RosterCut.Commit.Group;
	AbilityAck.LeaseEpoch = Fixture.Relay.GetLeaseState().LeaseEpoch;
	AbilityAck.SnapshotRevision = RosterCut.AbilityConfig.SnapshotRevision;
	AbilityAck.SnapshotHash = RosterCut.AbilityConfig.SnapshotHash;
	TestTrue(TEXT("Roster cut accepts the exact ability ACK"),
		Fixture.Relay.AcknowledgeAbilityConfig(Fixture.Owner, AbilityAck, 0.22));
	TestTrue(TEXT("Roster cut accepts the exact six-scope ACK"),
		Fixture.Relay.AcknowledgeBootstrap(Fixture.Owner, RosterCut.Commit, nullptr, 0.221));
	TestFalse(TEXT("ACK releases the Active roster barrier"),
		Fixture.Relay.IsActiveRosterCutPending());

	const FGuLiWingmanSubmissionResult Accepted = Fixture.Relay.SubmitCandidate(
		Fixture.Owner, NewRosterCandidate, 0.23, FoundCarrier(), PermitWorld());
	TestEqual(TEXT("Affected Flight resumes through one ordinary Candidate"),
		Accepted.Disposition, EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("Only affected Flight sequence advances"),
		Fixture.Relay.GetAcceptedSequenceForFlight(0u), FlightZeroSequence + 1u);
	TestEqual(TEXT("Unrelated Flight sequence is unchanged"),
		Fixture.Relay.GetAcceptedSequenceForFlight(1u), FlightOneSequence);
	TestEqual(TEXT("Unrelated Flight freshness is unchanged"),
		Fixture.Relay.GetLastValidCandidateTimeForFlight(1u), FlightOneFreshness);
	TestEqual(TEXT("Active roster refresh writes no server movement"),
		Fixture.Relay.GetServerWingmanMovementWriteCount(), 0ull);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanReplenishedFlightFreshnessGraceTest,
	"GuLiStrike.Wingman.LeaseTransactions.ReplenishedFlightFreshnessGrace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanReplenishedFlightFreshnessGraceTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanLeaseTransactionTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, 18u) || !Fixture.Activate(*this)) return false;

	auto AcknowledgeActiveCut = [this, &Fixture](
		const FGuLiWingmanBootstrapBundle& Cut, const double NowSeconds)
	{
		FGuLiGroupAbilityConfigAck AbilityAck;
		AbilityAck.Group = Cut.Commit.Group;
		AbilityAck.LeaseEpoch = Fixture.Relay.GetLeaseState().LeaseEpoch;
		AbilityAck.SnapshotRevision = Cut.AbilityConfig.SnapshotRevision;
		AbilityAck.SnapshotHash = Cut.AbilityConfig.SnapshotHash;
		return TestTrue(TEXT("Active roster Cut accepts its ability ACK"),
			Fixture.Relay.AcknowledgeAbilityConfig(Fixture.Owner, AbilityAck, NowSeconds))
			&& TestTrue(TEXT("Active roster Cut accepts its six-scope ACK"),
				Fixture.Relay.AcknowledgeBootstrap(
					Fixture.Owner, Cut.Commit, nullptr, NowSeconds + 0.001));
	};

	TArray<FGuLiWingmanHandle> OriginalHandles;
	for (const FGuLiWingmanRosterEntry& Entry : Fixture.Relay.GetRoster())
	{
		if (Entry.Wingman.Flight.FlightIndex == 0u)
		{
			OriginalHandles.Add(Entry.Wingman);
		}
	}
	for (const FGuLiWingmanHandle& Handle : OriginalHandles)
	{
		if (!TestTrue(TEXT("Every member of one Flight can enter the authoritative dead roster"),
			Fixture.Relay.MarkWingmanDead(Handle)))
		{
			return false;
		}
	}
	TestEqual(TEXT("A fully dead Flight is excluded from required coverage"),
		Fixture.MakeFlight(0u, 20u, 2u, 106u, 0.2).RequiredMemberMask,
		static_cast<uint8>(0u));

	FGuLiWingmanBootstrapBundle DeadRosterCut;
	if (!TestTrue(TEXT("The dead Flight freezes one reliable Active Cut"),
		Fixture.Relay.RefreshActiveRosterCut(0.2, DeadRosterCut))
		|| !AcknowledgeActiveCut(DeadRosterCut, 0.21))
	{
		return false;
	}
	for (uint8 FlightIndex = 1u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		const FGuLiWingmanSubmissionResult Refreshed = Fixture.Relay.SubmitCandidate(
			Fixture.Owner,
			Fixture.MakeFlight(
				FlightIndex, 20u + FlightIndex, 2u, 106u, 14.8),
			14.8,
			FoundCarrier(),
			PermitWorld());
		if (!TestEqual(TEXT("Unaffected Flight can refresh before replenishment"),
			Refreshed.Disposition, EGuLiWingmanSubmissionDisposition::Accepted))
		{
			return false;
		}
	}
	const double HistoricalAcceptedFreshness =
		Fixture.Relay.GetLastValidCandidateTimeForFlight(0u);
	const uint32 HistoricalAcceptedSequence =
		Fixture.Relay.GetAcceptedSequenceForFlight(0u);
	const uint64 HistoricalAcceptedHash =
		GuLiWingmanRelayHash::AcceptedSnapshot(Fixture.Relay.GetAcceptedHistory());

	TestTrue(TEXT("Authority time advances without inventing movement"),
		Fixture.Relay.RecordLeaseHeartbeat(
			Fixture.Owner,
			Fixture.Relay.GetConnectionGeneration(),
			Fixture.Relay.GetLeaseState().LeaseEpoch,
			15.0));
	for (uint8 MemberIndex = 0u; MemberIndex < GULI_WINGMAN_MEMBERS_PER_FLIGHT; ++MemberIndex)
	{
		if (MemberIndex == 1u)
		{
			TestTrue(TEXT("Later same-batch replacements observe a newer authority time"),
				Fixture.Relay.RecordLeaseHeartbeat(
					Fixture.Owner,
					Fixture.Relay.GetConnectionGeneration(),
					Fixture.Relay.GetLeaseState().LeaseEpoch,
					15.5));
		}
		const FGuLiWingmanHandle Previous = Fixture.Relay.GetRoster()[MemberIndex].Wingman;
		if (!TestTrue(TEXT("A dead stable slot replenishes with a new generation"),
			Fixture.Relay.ReplenishWingman(
				0u, MemberIndex, Previous.EntityGeneration + 1u)))
		{
			return false;
		}
	}
	TestEqual(TEXT("Replenishment restores full Flight coverage"),
		Fixture.MakeFlight(0u, 21u, 2u, 106u, 15.5).RequiredMemberMask,
		static_cast<uint8>((1u << GULI_WINGMAN_MEMBERS_PER_FLIGHT) - 1u));
	TestEqual(TEXT("Freshness grace does not forge a valid Candidate timestamp"),
		Fixture.Relay.GetLastValidCandidateTimeForFlight(0u), HistoricalAcceptedFreshness);
	TestEqual(TEXT("Freshness grace does not advance the Accepted sequence"),
		Fixture.Relay.GetAcceptedSequenceForFlight(0u), HistoricalAcceptedSequence);
	TestEqual(TEXT("Freshness grace does not mutate the Accepted snapshot"),
		GuLiWingmanRelayHash::AcceptedSnapshot(Fixture.Relay.GetAcceptedHistory()),
		HistoricalAcceptedHash);

	FGuLiWingmanBootstrapBundle ReplenishedRosterCut;
	if (!TestTrue(TEXT("The replenished Flight freezes one reliable Active Cut"),
		Fixture.Relay.RefreshActiveRosterCut(15.51, ReplenishedRosterCut))
		|| !AcknowledgeActiveCut(ReplenishedRosterCut, 15.52))
	{
		return false;
	}
	for (uint8 FlightIndex = 1u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		const FGuLiWingmanSubmissionResult Refreshed = Fixture.Relay.SubmitCandidate(
			Fixture.Owner,
			Fixture.MakeFlight(
				FlightIndex, 30u + FlightIndex, 3u, 112u, 15.55),
			15.55,
			FoundCarrier(),
			PermitWorld());
		if (!TestEqual(TEXT("Unaffected Flight stays fresh across the grace boundary"),
			Refreshed.Disposition, EGuLiWingmanSubmissionDisposition::Accepted))
		{
			return false;
		}
	}
	TestTrue(TEXT("The first watchdog inside replenishment grace executes"),
		Fixture.Relay.RunLeaseMaintenance(15.6));
	TestEqual(TEXT("A newly required Flight is not immediately made Unavailable"),
		Fixture.Relay.GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Active);
	TestTrue(TEXT("The next one-second watchdog boundary executes"),
		Fixture.Relay.RunLeaseMaintenance(16.1));
	TestEqual(TEXT("Later replacements cannot slide the bounded freshness grace"),
		Fixture.Relay.GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Stale);
	TestEqual(TEXT("Freshness grace writes no server movement"),
		Fixture.Relay.GetServerWingmanMovementWriteCount(), 0ull);
	return true;
}
#endif
