// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Relay/GuLiWingmanRelayServer.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Misc/AutomationTest.h"

namespace GuLiWingmanRelayTests
{
	FGuLiWingmanGroupHandle MakeGroup()
	{
		FGuLiWingmanGroupHandle Group;
		Group.ShipInstanceId = FGuid(0x13572468u, 0x24681357u, 0xabcdef01u, 0x10293847u);
		Group.ShipGeneration = 2u;
		Group.GroupGeneration = 4u;
		return Group;
	}

	FGuLiGroupAbilityConfigSnapshot MakeAbilityConfig(
		const FGuLiWingmanGroupHandle& Group, const uint32 SnapshotRevision = 3u)
	{
		FGuLiGroupAbilityConfigSnapshot Config;
		Config.ShipInstanceId = Group.ShipInstanceId;
		Config.ShipGeneration = Group.ShipGeneration;
		Config.GroupGeneration = Group.GroupGeneration;
		Config.AbilitySetRevision = 7u;
		Config.SnapshotRevision = SnapshotRevision;
		Config.bGroupAbilitiesValid = true;
		Config.FormationAbilityId = TAG_GuLi_ShipAbility_Formation_DoubleRing;
		Config.BasicWeaponAbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
		Config.MissileAbilityId = TAG_GuLi_ShipAbility_Weapon_Missile_Salvo;
		Config.FormationDefinitionRevision = 5u;
		Config.FormationDefinitionChecksum = 0x1111222233334444ull;
		Config.BasicWeaponDefinitionRevision = 6u;
		Config.BasicWeaponDefinitionChecksum = 0x2222333344445555ull;
		Config.MissileDefinitionRevision = 7u;
		Config.MissileDefinitionChecksum = 0x3333444455556666ull;
		Config.FormationCommandRevision = 9u;
		Config.EffectiveClientSimTick = 100u;
		Config.RefreshHash();
		return Config;
	}

	FGuLiWingmanCandidateBatch MakeCandidate(
		const FGuLiWingmanRelayServer& Relay, const uint32 Sequence = 1u, const uint32 ClientTick = 100u)
	{
		const FGuLiGroupAbilityConfigSnapshot& Config = Relay.GetAbilityConfig();
		const FGuLiWingmanLeaseState& Lease = Relay.GetLeaseState();
		FGuLiWingmanCandidateBatch Candidate;
		Candidate.MatchEpoch = 11u;
		Candidate.Group = Lease.Group;
		Candidate.LeaseEpoch = Lease.LeaseEpoch;
		Candidate.CandidateSequence = Sequence;
		Candidate.ClientSimTick = ClientTick;
		Candidate.CarrierSource.CanonicalEpoch = 8u;
		Candidate.CarrierSource.MoveRevision = Sequence + 20u;
		Candidate.AbilitySetRevision = Config.AbilitySetRevision;
		Candidate.FormationCommandRevision = Config.FormationCommandRevision;
		Candidate.FormationDefinitionChecksum = Config.FormationDefinitionChecksum;
		for (const FGuLiWingmanRosterEntry& Entry : Relay.GetRoster())
		{
			FGuLiWingmanCandidateSample& Sample = Candidate.Samples.AddDefaulted_GetRef();
			Sample.Wingman = Entry.Wingman;
			const int32 GroupMemberIndex = Entry.Wingman.GetGroupMemberIndex();
			Sample.PositionCentimeters = FIntVector(1000 + GroupMemberIndex * 100, GroupMemberIndex * 50, 5000);
			Sample.VelocityCentimetersPerSecond = FIntVector(4500, GroupMemberIndex, 0);
			Sample.RotationCentiDegrees = FIntVector(0, GroupMemberIndex * 100, 0);
			Sample.FlightMode = 1u;
		}
		return Candidate;
	}

	FGuLiWingmanCandidateBatch MakeKinematicCandidate(
		const FGuLiWingmanRelayServer& Relay,
		const FGuLiWingmanAcceptedBatch& Previous,
		const uint32 Sequence,
		const uint32 ClientTick,
		const FVector& DisplacementCentimeters,
		const FVector& VelocityCentimetersPerSecond)
	{
		FGuLiWingmanCandidateBatch Candidate = MakeCandidate(Relay, Sequence, ClientTick);
		const FIntVector QuantizedDisplacement(
			FMath::RoundToInt(DisplacementCentimeters.X),
			FMath::RoundToInt(DisplacementCentimeters.Y),
			FMath::RoundToInt(DisplacementCentimeters.Z));
		const FIntVector QuantizedVelocity(
			FMath::RoundToInt(VelocityCentimetersPerSecond.X),
			FMath::RoundToInt(VelocityCentimetersPerSecond.Y),
			FMath::RoundToInt(VelocityCentimetersPerSecond.Z));
		const FRotator Rotation = VelocityCentimetersPerSecond.Rotation().GetNormalized();
		const FIntVector QuantizedRotation(
			FMath::RoundToInt(Rotation.Pitch * 100.0),
			FMath::RoundToInt(Rotation.Yaw * 100.0),
			FMath::RoundToInt(Rotation.Roll * 100.0));
		for (FGuLiWingmanCandidateSample& Sample : Candidate.Samples)
		{
			const FGuLiWingmanCandidateSample* PreviousSample = Previous.FindSample(Sample.Wingman);
			if (PreviousSample)
			{
				Sample.PositionCentimeters = PreviousSample->PositionCentimeters + QuantizedDisplacement;
			}
			Sample.VelocityCentimetersPerSecond = QuantizedVelocity;
			Sample.RotationCentiDegrees = QuantizedRotation;
		}
		return Candidate;
	}

	bool ActivateRelay(FAutomationTestBase& Test, FGuLiWingmanRelayServer& Relay,
		const FGuid& Owner, FGuLiWingmanBootstrapBundle* OutBootstrap = nullptr)
	{
		FGuLiWingmanBootstrapBundle Bootstrap;
		if (!Test.TestTrue(TEXT("The server builds one complete six-scope bootstrap"), Relay.BuildBootstrap(Bootstrap)))
		{
			return false;
		}
		Test.TestTrue(TEXT("The bootstrap contains exactly six mutually required scopes"), Bootstrap.IsWellFormed());
		FGuLiGroupAbilityConfigAck AbilityAck;
		AbilityAck.Group = Relay.GetLeaseState().Group;
		AbilityAck.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
		AbilityAck.SnapshotRevision = Relay.GetAbilityConfig().SnapshotRevision;
		AbilityAck.SnapshotHash = Relay.GetAbilityConfig().SnapshotHash;
		Test.TestTrue(TEXT("The exact ability projection ACK is accepted"),
			Relay.AcknowledgeAbilityConfig(Owner, AbilityAck, 0.01));
		const FGuLiWingmanTransferBaseline* Baseline = Bootstrap.bHasTransferBaseline
			? &Bootstrap.TransferBaseline : nullptr;
		Test.TestTrue(TEXT("The exact atomic bootstrap ACK is accepted"),
			Relay.AcknowledgeBootstrap(Owner, Bootstrap.Commit, Baseline, 0.02));
		Test.TestTrue(TEXT("Both acknowledgements atomically activate the group"),
			Relay.GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Active);
		if (OutBootstrap)
		{
			*OutBootstrap = Bootstrap;
		}
		return Relay.GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Active;
	}

	bool InitializeRelay(FAutomationTestBase& Test, FGuLiWingmanRelayServer& Relay,
		FGuid& OutOwner, FGuid& OutBackup)
	{
		OutOwner = FGuid(1u, 2u, 3u, 4u);
		OutBackup = FGuid(5u, 6u, 7u, 8u);
		const FGuLiWingmanGroupHandle Group = MakeGroup();
		const bool bInitialized = Relay.InitializeGroup(11u, Group, OutOwner, OutBackup,
			MakeAbilityConfig(Group), 0.0);
		Test.TestTrue(TEXT("A valid authority-owned group initializes"), bInitialized);
		Test.TestEqual(TEXT("A group always initializes all 25 deterministic roster slots"),
			Relay.GetRoster().Num(), static_cast<int32>(GULI_WINGMAN_GROUP_SIZE));
		return bInitialized;
	}

	FGuLiCarrierSourceResolver FoundCarrier()
	{
		return [](const FGuLiCarrierSourceRef&, FGuLiRelayCarrierState& OutState)
		{
			OutState.Transform = FTransform(FVector::ZeroVector);
			OutState.Velocity = FVector(1000.0, 0.0, 0.0);
			OutState.ServerWorldTimeSeconds = 0.1;
			return EGuLiRelayCarrierLookupResult::Found;
		};
	}

	FGuLiCandidateWorldValidator PermitWorld()
	{
		return [](const FGuLiWingmanCandidateWorldValidationContext& Context)
		{
			return Context.IsWellFormed()
				? EGuLiWingmanRejectReason::None
				: EGuLiWingmanRejectReason::InvalidIdentity;
		};
	}

	FGuLiWingmanFireIntent MakeFireIntent(
		const FGuLiWingmanRelayServer& Relay, const FGuLiWingmanAcceptedBatch& SourceBatch,
		const uint32 Sequence = 1u)
	{
		FGuLiWingmanFireIntent Intent;
		Intent.MatchEpoch = SourceBatch.StateRef.MatchEpoch;
		Intent.Group = Relay.GetLeaseState().Group;
		Intent.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
		Intent.DomainFireSequence = Sequence;
		Intent.Emitter = SourceBatch.Samples[0].Wingman;
		Intent.SourceAcceptedState = SourceBatch.StateRef;
		Intent.ClientFireTick = SourceBatch.StateRef.ClientSimTick + 1u;
		Intent.Target.Kind = EGuLiTargetKind::CommanderSoldier;
		Intent.Target.AuthorityId = FGuid(20u, 21u, 22u, 23u);
		Intent.Target.Generation = 1u;
		Intent.Target.LocalId = 7u;
		Intent.WeaponAbilityId = Relay.GetAbilityConfig().BasicWeaponAbilityId;
		Intent.WeaponDefinitionRevision = Relay.GetAbilityConfig().BasicWeaponDefinitionRevision;
		Intent.AbilitySetRevision = Relay.GetAbilityConfig().AbilitySetRevision;
		Intent.AimDirectionMilli = FIntVector(1000, 0, 0);
		Intent.bClientPredictedLineOfSight = true;
		return Intent;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanRelayZeroAuthorityMotionTest,
	"GuLiStrike.Wingman.Relay.SyntheticCandidate.ZeroAuthorityMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanRelayZeroAuthorityMotionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTests;
	FGuLiWingmanRelayServer Relay;
	FGuid Owner;
	FGuid Backup;
	if (!InitializeRelay(*this, Relay, Owner, Backup) || !ActivateRelay(*this, Relay, Owner))
	{
		return false;
	}

	const FGuLiWingmanCandidateBatch Candidate = MakeCandidate(Relay);
	const TArray<FGuLiWingmanCandidateSample> OriginalSamples = Candidate.Samples;
	const FGuLiWingmanSubmissionResult Result = Relay.SubmitCandidate(
		Owner, Candidate, 0.1, FoundCarrier(), PermitWorld());
	TestTrue(TEXT("A synthetic owner Candidate is accepted"),
		Result.Disposition == EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("Acceptance stores one atomic batch"), Relay.GetAcceptedHistory().Num(), 1);
	TestEqual(TEXT("The server has no Wingman movement write path"),
		Relay.GetServerWingmanMovementWriteCount(), static_cast<uint64>(0u));
	TestEqual(TEXT("Every candidate sample is retained"), Result.AcceptedBatch.Samples.Num(), OriginalSamples.Num());
	for (int32 Index = 0; Index < OriginalSamples.Num() && Index < Result.AcceptedBatch.Samples.Num(); ++Index)
	{
		const FGuLiWingmanCandidateSample& Before = OriginalSamples[Index];
		const FGuLiWingmanCandidateSample& After = Result.AcceptedBatch.Samples[Index];
		TestTrue(TEXT("Stable Wingman identity remains byte-semantic equivalent"), Before.Wingman == After.Wingman);
		TestEqual(TEXT("Server never integrates the candidate position"), After.PositionCentimeters, Before.PositionCentimeters);
		TestEqual(TEXT("Server never integrates the candidate velocity"), After.VelocityCentimetersPerSecond,
			Before.VelocityCentimetersPerSecond);
		TestEqual(TEXT("Server never synthesizes a candidate rotation"), After.RotationCentiDegrees,
			Before.RotationCentiDegrees);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanRelayCarrierPendingTest,
	"GuLiStrike.Wingman.Relay.CarrierSource.PendingThenAcceptOrTimeout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanRelayCarrierPendingTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTests;
	FGuLiWingmanRelayServer Relay;
	FGuid Owner;
	FGuid Backup;
	if (!InitializeRelay(*this, Relay, Owner, Backup) || !ActivateRelay(*this, Relay, Owner))
	{
		return false;
	}

	const FGuLiCarrierSourceResolver PendingCarrier = [](const FGuLiCarrierSourceRef&, FGuLiRelayCarrierState&)
	{
		return EGuLiRelayCarrierLookupResult::Pending;
	};
	FGuLiWingmanSubmissionResult Result = Relay.SubmitCandidate(
		Owner, MakeCandidate(Relay, 1u, 100u), 0.1, PendingCarrier, PermitWorld());
	TestTrue(TEXT("A future CMC revision is queued instead of rejected"),
		Result.Disposition == EGuLiWingmanSubmissionDisposition::Pending);
	TestEqual(TEXT("A pending cross-channel reference does not reserve the sequence"),
		Relay.GetLastAcceptedCandidateSequence(), 0u);

	Relay.AdvanceTime(0.2, FoundCarrier());
	TArray<FGuLiWingmanSubmissionResult> Deferred;
	Relay.DrainDeferredCandidateResults(Deferred);
	TestEqual(TEXT("The history-advance event resolves one pending Candidate"), Deferred.Num(), 1);
	TestTrue(TEXT("Resolution before 0.25 seconds accepts the exact Candidate"),
		Deferred.Num() == 1 && Deferred[0].Disposition == EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("Only successful resolution advances the sequence"), Relay.GetLastAcceptedCandidateSequence(), 1u);

	Result = Relay.SubmitCandidate(
		Owner, MakeCandidate(Relay, 2u, 103u), 0.21, PendingCarrier, PermitWorld());
	TestTrue(TEXT("A second unresolved source enters Pending"),
		Result.Disposition == EGuLiWingmanSubmissionDisposition::Pending);
	Relay.AdvanceTime(0.47, PendingCarrier);
	Deferred.Reset();
	Relay.DrainDeferredCandidateResults(Deferred);
	TestTrue(TEXT("A source missing for more than 0.25 seconds is rejected explicitly"),
		Deferred.Num() == 1 && Deferred[0].RejectReason == EGuLiWingmanRejectReason::CarrierMovePendingTimeout);
	TestEqual(TEXT("Timed-out Candidate still cannot advance the sequence"), Relay.GetLastAcceptedCandidateSequence(), 1u);

	Result = Relay.SubmitCandidate(
		Owner, MakeCandidate(Relay, 2u, 106u), 0.48, PendingCarrier, PermitWorld());
	TestTrue(TEXT("An exact-deadline fixture enters Pending"),
		Result.Disposition == EGuLiWingmanSubmissionDisposition::Pending);
	Relay.AdvanceTime(0.73, FoundCarrier());
	Deferred.Reset();
	Relay.DrainDeferredCandidateResults(Deferred);
	TestTrue(TEXT("Deadline wins even when the Carrier resolver reports Found at exactly 0.25 seconds"),
		Deferred.Num() == 1
			&& Deferred[0].RejectReason == EGuLiWingmanRejectReason::CarrierMovePendingTimeout);
	TestEqual(TEXT("Exact-deadline rejection cannot advance Candidate state"),
		Relay.GetLastAcceptedCandidateSequence(), 1u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanRelayWorldValidatorCommitGateTest,
	"GuLiStrike.Wingman.Relay.Validator.WorldGateFailClosedBeforeCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanRelayWorldValidatorCommitGateTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTests;
	FGuLiWingmanRelayServer Relay;
	FGuid Owner;
	FGuid Backup;
	if (!InitializeRelay(*this, Relay, Owner, Backup) || !ActivateRelay(*this, Relay, Owner))
	{
		return false;
	}

	const FGuLiWingmanCandidateBatch Candidate = MakeCandidate(Relay);
	FGuLiWingmanSubmissionResult Result = Relay.SubmitCandidate(
		Owner, Candidate, 0.1, FoundCarrier());
	TestTrue(TEXT("A production Candidate without an installed World validator fails closed"),
		Result.Disposition == EGuLiWingmanSubmissionDisposition::Rejected
			&& Result.RejectReason == EGuLiWingmanRejectReason::InvalidIdentity);
	TestEqual(TEXT("Missing validator does not append history"), Relay.GetAcceptedHistory().Num(), 0);
	TestEqual(TEXT("Missing validator does not reserve CandidateSequence"),
		Relay.GetLastAcceptedCandidateSequence(), 0u);

	int32 CallbackCount = 0;
	const FGuLiCandidateWorldValidator RejectWorld = [&CallbackCount](
		const FGuLiWingmanCandidateWorldValidationContext& Context)
	{
		++CallbackCount;
		return Context.IsWellFormed()
			? EGuLiWingmanRejectReason::InvalidIdentity
			: EGuLiWingmanRejectReason::CarrierMoveExpired;
	};
	Result = Relay.SubmitCandidate(Owner, Candidate, 0.11, FoundCarrier(), RejectWorld);
	TestEqual(TEXT("The World callback executes after motion validation"), CallbackCount, 1);
	TestTrue(TEXT("A World rejection is returned verbatim"),
		Result.Disposition == EGuLiWingmanSubmissionDisposition::Rejected
			&& Result.RejectReason == EGuLiWingmanRejectReason::InvalidIdentity);
	TestEqual(TEXT("World rejection still does not append history"), Relay.GetAcceptedHistory().Num(), 0);
	TestEqual(TEXT("World rejection still does not reserve CandidateSequence"),
		Relay.GetLastAcceptedCandidateSequence(), 0u);

	Result = Relay.SubmitCandidate(Owner, Candidate, 0.12, FoundCarrier(), PermitWorld());
	TestTrue(TEXT("The same unreserved sequence can commit after every gate passes"),
		Result.Disposition == EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("Only the permitted Candidate commits"), Relay.GetAcceptedHistory().Num(), 1);
	TestEqual(TEXT("Authority movement writer remains absent"),
		Relay.GetServerWingmanMovementWriteCount(), static_cast<uint64>(0u));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanRelayPendingStateGateTest,
	"GuLiStrike.Wingman.Relay.Validator.PendingHealthAbilityAndModeGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanRelayPendingStateGateTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTests;
	const FGuLiCarrierSourceResolver PendingCarrier = [](
		const FGuLiCarrierSourceRef&, FGuLiRelayCarrierState&)
	{
		return EGuLiRelayCarrierLookupResult::Pending;
	};

	FGuLiWingmanRelayServer HealthRelay;
	FGuid HealthOwner;
	FGuid HealthBackup;
	if (!InitializeRelay(*this, HealthRelay, HealthOwner, HealthBackup)
		|| !ActivateRelay(*this, HealthRelay, HealthOwner))
	{
		return false;
	}
	const FGuLiWingmanCandidateBatch HealthCandidate = MakeCandidate(HealthRelay);
	TestEqual(TEXT("Health fixture enters Pending"),
		HealthRelay.SubmitCandidate(
			HealthOwner, HealthCandidate, 0.1, PendingCarrier, PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Pending);
	TestTrue(TEXT("Reliable health can reach zero before deferred resolution"),
		HealthRelay.SetWingmanHealthPermille(HealthCandidate.Samples[0].Wingman, 0u));
	HealthRelay.AdvanceTime(0.2, FoundCarrier());
	TArray<FGuLiWingmanSubmissionResult> Deferred;
	HealthRelay.DrainDeferredCandidateResults(Deferred);
	TestTrue(TEXT("Health=0 invalidates a Pending Candidate before its resolver can accept it"),
		Deferred.Num() == 1 && Deferred[0].RejectReason == EGuLiWingmanRejectReason::EmitterDead);
	TestEqual(TEXT("Health invalidation does not reserve CandidateSequence"),
		HealthRelay.GetLastAcceptedCandidateSequence(), 0u);

	FGuLiWingmanRelayServer AbilityRelay;
	FGuid AbilityOwner;
	FGuid AbilityBackup;
	if (!InitializeRelay(*this, AbilityRelay, AbilityOwner, AbilityBackup)
		|| !ActivateRelay(*this, AbilityRelay, AbilityOwner))
	{
		return false;
	}
	TestEqual(TEXT("Ability fixture enters Pending"),
		AbilityRelay.SubmitCandidate(
			AbilityOwner, MakeCandidate(AbilityRelay), 0.1, PendingCarrier, PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Pending);
	FGuLiGroupAbilityConfigSnapshot NewConfig = MakeAbilityConfig(
		AbilityRelay.GetLeaseState().Group,
		AbilityRelay.GetAbilityConfig().SnapshotRevision + 1u);
	++NewConfig.AbilitySetRevision;
	NewConfig.RefreshHash();
	TestTrue(TEXT("A newer reliable ability projection publishes"),
		AbilityRelay.PublishAbilityConfig(NewConfig, 0.15));
	Deferred.Reset();
	AbilityRelay.DrainDeferredCandidateResults(Deferred);
	TestTrue(TEXT("Ability publication rejects the old Pending Candidate"),
		Deferred.Num() == 1
			&& Deferred[0].RejectReason == EGuLiWingmanRejectReason::MissingAbilityConfig);
	TestEqual(TEXT("Ability invalidation does not reserve CandidateSequence"),
		AbilityRelay.GetLastAcceptedCandidateSequence(), 0u);

	FGuLiWingmanRelayServer ModeRelay;
	FGuid ModeOwner;
	FGuid ModeBackup;
	if (!InitializeRelay(*this, ModeRelay, ModeOwner, ModeBackup)
		|| !ActivateRelay(*this, ModeRelay, ModeOwner))
	{
		return false;
	}
	FGuLiWingmanCandidateBatch Initial = MakeCandidate(ModeRelay, 1u, 100u);
	for (FGuLiWingmanCandidateSample& Sample : Initial.Samples)
	{
		Sample.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Orbit);
	}
	const FGuLiWingmanSubmissionResult InitialAccepted = ModeRelay.SubmitCandidate(
		ModeOwner, Initial, 0.1, FoundCarrier(), PermitWorld());
	if (!TestEqual(TEXT("Flight-mode fixture establishes an Orbit baseline"),
		InitialAccepted.Disposition, EGuLiWingmanSubmissionDisposition::Accepted))
	{
		return false;
	}
	FGuLiWingmanCandidateBatch IllegalMode = MakeKinematicCandidate(
		ModeRelay,
		InitialAccepted.AcceptedBatch,
		2u,
		106u,
		FVector(900.0, 0.0, 0.0),
		FVector(4500.0, 0.0, 0.0));
	for (FGuLiWingmanCandidateSample& Sample : IllegalMode.Samples)
	{
		Sample.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Recover);
	}
	const FGuLiWingmanSubmissionResult ModeRejected = ModeRelay.SubmitCandidate(
		ModeOwner, IllegalMode, 0.2, FoundCarrier(), PermitWorld());
	TestTrue(TEXT("Orbit cannot jump directly to Recover between Candidate packets"),
		ModeRejected.Disposition == EGuLiWingmanSubmissionDisposition::Rejected
			&& ModeRejected.RejectReason == EGuLiWingmanRejectReason::InvalidIdentity);
	TestEqual(TEXT("Illegal mode transition does not reserve CandidateSequence"),
		ModeRelay.GetLastAcceptedCandidateSequence(), 1u);
	TestEqual(TEXT("All rejection paths preserve server zero-movement"),
		ModeRelay.GetServerWingmanMovementWriteCount(), static_cast<uint64>(0u));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanRelayMotionEnvelopeTest,
	"GuLiStrike.Wingman.Relay.Validator.MotionEnvelopeBeforeAcceptedCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanRelayMotionEnvelopeTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTests;
	FGuLiWingmanRelayServer Relay;
	FGuid Owner;
	FGuid Backup;
	if (!InitializeRelay(*this, Relay, Owner, Backup) || !ActivateRelay(*this, Relay, Owner))
	{
		return false;
	}

	FGuLiWingmanCandidateBatch Initial = MakeCandidate(Relay, 1u, 100u);
	for (FGuLiWingmanCandidateSample& Sample : Initial.Samples)
	{
		Sample.VelocityCentimetersPerSecond = FIntVector(4500, 0, 0);
		Sample.RotationCentiDegrees = FIntVector::ZeroValue;
	}
	const FGuLiWingmanSubmissionResult InitialResult = Relay.SubmitCandidate(
		Owner, Initial, 0.1, FoundCarrier(), PermitWorld());
	if (!TestTrue(TEXT("A valid initial sample establishes the trusted kinematic baseline"),
		InitialResult.Disposition == EGuLiWingmanSubmissionDisposition::Accepted))
	{
		return false;
	}

	const auto TestRejectedWithoutCommit = [this, &Relay, &Owner](
		const TCHAR* Description,
		const FGuLiWingmanCandidateBatch& Candidate,
		const double NowSeconds,
		const EGuLiWingmanRejectReason ExpectedReason)
	{
		const int32 HistoryBefore = Relay.GetAcceptedHistory().Num();
		const uint32 SequenceBefore = Relay.GetLastAcceptedCandidateSequence();
		const FGuLiWingmanSubmissionResult Result = Relay.SubmitCandidate(
			Owner, Candidate, NowSeconds, FoundCarrier(), PermitWorld());
		TestTrue(Description, Result.Disposition == EGuLiWingmanSubmissionDisposition::Rejected
			&& Result.RejectReason == ExpectedReason);
		TestEqual(TEXT("A rejected motion packet cannot append accepted history"),
			Relay.GetAcceptedHistory().Num(), HistoryBefore);
		TestEqual(TEXT("A rejected motion packet cannot reserve CandidateSequence"),
			Relay.GetLastAcceptedCandidateSequence(), SequenceBefore);
	};

	FGuLiWingmanCandidateBatch OldAbility = MakeKinematicCandidate(
		Relay, InitialResult.AcceptedBatch, 2u, 106u, FVector(900.0, 0.0, 0.0), FVector(4500.0, 0.0, 0.0));
	--OldAbility.AbilitySetRevision;
	TestRejectedWithoutCommit(TEXT("An old ability projection is rejected before motion state"),
		OldAbility, 0.11, EGuLiWingmanRejectReason::StaleAbilitySetRevision);

	const FGuLiWingmanCandidateBatch OutOfOrder = MakeKinematicCandidate(
		Relay, InitialResult.AcceptedBatch, 2u, 100u, FVector::ZeroVector, FVector(4500.0, 0.0, 0.0));
	TestRejectedWithoutCommit(TEXT("A non-advancing client simulation tick is rejected"),
		OutOfOrder, 0.12, EGuLiWingmanRejectReason::StaleSourceState);

	const FGuLiWingmanCandidateBatch ExcessiveTickLead = MakeKinematicCandidate(
		Relay, InitialResult.AcceptedBatch, 2u, 1000u, FVector(135000.0, 0.0, 0.0), FVector(4500.0, 0.0, 0.0));
	TestRejectedWithoutCommit(TEXT("A forged large client tick delta cannot widen the position envelope"),
		ExcessiveTickLead, 0.13, EGuLiWingmanRejectReason::StaleSourceState);

	const FGuLiWingmanCandidateBatch ExcessiveSpeed = MakeKinematicCandidate(
		Relay, InitialResult.AcceptedBatch, 2u, 106u, FVector(900.0, 0.0, 0.0), FVector(8000.0, 0.0, 0.0));
	TestRejectedWithoutCommit(TEXT("Speed above the projected CatchUpSpeed is rejected"),
		ExcessiveSpeed, 0.14, EGuLiWingmanRejectReason::InvalidIdentity);

	const FGuLiWingmanCandidateBatch ExcessiveAcceleration = MakeKinematicCandidate(
		Relay, InitialResult.AcceptedBatch, 2u, 106u, FVector(950.0, 0.0, 0.0), FVector(5000.0, 0.0, 0.0));
	TestRejectedWithoutCommit(TEXT("Endpoint speed cannot exceed the projected acceleration envelope"),
		ExcessiveAcceleration, 0.15, EGuLiWingmanRejectReason::InvalidIdentity);

	const FVector ThirtyDegreeVelocity = FVector(4500.0, 0.0, 0.0).RotateAngleAxis(30.0, FVector::UpVector);
	const FGuLiWingmanCandidateBatch ExcessiveTurn = MakeKinematicCandidate(
		Relay, InitialResult.AcceptedBatch, 2u, 106u, FVector(840.0, 225.0, 0.0), ThirtyDegreeVelocity);
	TestRejectedWithoutCommit(TEXT("Endpoint direction cannot exceed the projected turn-rate envelope"),
		ExcessiveTurn, 0.16, EGuLiWingmanRejectReason::InvalidIdentity);

	const FGuLiWingmanCandidateBatch Teleport = MakeKinematicCandidate(
		Relay, InitialResult.AcceptedBatch, 2u, 106u, FVector(20000.0, 0.0, 0.0), FVector(4500.0, 0.0, 0.0));
	TestRejectedWithoutCommit(TEXT("A position teleport is rejected even when endpoint velocity is legal"),
		Teleport, 0.17, EGuLiWingmanRejectReason::InvalidIdentity);

	constexpr double LegalTurnDegrees = 4.0;
	const FVector LegalVelocity = FVector(4700.0, 0.0, 0.0).RotateAngleAxis(
		LegalTurnDegrees, FVector::UpVector);
	const FVector LegalDisplacement = (FVector(4500.0, 0.0, 0.0) + LegalVelocity) * 0.1;
	const FGuLiWingmanCandidateBatch LegalCatchUp = MakeKinematicCandidate(
		Relay, InitialResult.AcceptedBatch, 2u, 106u, LegalDisplacement, LegalVelocity);
	const FGuLiWingmanSubmissionResult LegalResult = Relay.SubmitCandidate(
		Owner, LegalCatchUp, 0.2, FoundCarrier(), PermitWorld());
	TestTrue(TEXT("A pursuit step at the exact acceleration and turn limits remains accepted"),
		LegalResult.Disposition == EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("The first reusable sequence commits only for the legal pursuit packet"),
		Relay.GetLastAcceptedCandidateSequence(), 2u);
	TestEqual(TEXT("Only the initial and legal pursuit batches enter accepted history"),
		Relay.GetAcceptedHistory().Num(), 2);

	const FGuLiWingmanCandidateBatch SameTimeBurst = MakeKinematicCandidate(
		Relay, LegalResult.AcceptedBatch, 3u, 112u, LegalVelocity * 0.2, LegalVelocity);
	TestRejectedWithoutCommit(TEXT("Repeated same-time packets cannot accumulate client clock lead"),
		SameTimeBurst, 0.2, EGuLiWingmanRejectReason::StaleSourceState);
	TestEqual(TEXT("The clock-lead rejection leaves the last accepted sequence unchanged"),
		Relay.GetLastAcceptedCandidateSequence(), 2u);
	TestEqual(TEXT("Server validation still never writes a Wingman movement transform"),
		Relay.GetServerWingmanMovementWriteCount(), static_cast<uint64>(0u));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanRelayAbilityAndBootstrapGateTest,
	"GuLiStrike.Wingman.Relay.AbilityConfigAndSixScopeGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanRelayAbilityAndBootstrapGateTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTests;
	FGuLiWingmanRelayServer Relay;
	FGuid Owner;
	FGuid Backup;
	if (!InitializeRelay(*this, Relay, Owner, Backup))
	{
		return false;
	}
	FGuLiWingmanBootstrapBundle Bootstrap;
	TestTrue(TEXT("Initial bootstrap is generated"), Relay.BuildBootstrap(Bootstrap));
	FGuLiWingmanBootstrapCommit TamperedCommit = Bootstrap.Commit;
	for (FGuLiWingmanBootstrapScopeState& State : TamperedCommit.Scopes)
	{
		if (State.Scope == EGuLiWingmanBootstrapScope::GroupAbilityConfig)
		{
			++State.Hash;
			break;
		}
	}
	TestFalse(TEXT("A mismatched AbilityConfig scope hash cannot activate the group"),
		Relay.AcknowledgeBootstrap(Owner, TamperedCommit, nullptr, 0.01));

	FGuLiGroupAbilityConfigAck AbilityAck;
	AbilityAck.Group = Relay.GetLeaseState().Group;
	AbilityAck.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
	AbilityAck.SnapshotRevision = Relay.GetAbilityConfig().SnapshotRevision;
	AbilityAck.SnapshotHash = Relay.GetAbilityConfig().SnapshotHash;
	TestTrue(TEXT("Ability ACK succeeds"), Relay.AcknowledgeAbilityConfig(Owner, AbilityAck, 0.02));
	TestTrue(TEXT("Ability ACK alone leaves the group Initializing"),
		Relay.GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Initializing);
	TestTrue(TEXT("The untampered six-scope cut completes activation"),
		Relay.AcknowledgeBootstrap(Owner, Bootstrap.Commit, nullptr, 0.03));

	FGuLiWingmanCandidateBatch StaleCandidate = MakeCandidate(Relay);
	StaleCandidate.AbilitySetRevision--;
	const FGuLiWingmanSubmissionResult Result = Relay.SubmitCandidate(
		Owner, StaleCandidate, 0.1, FoundCarrier(), PermitWorld());
	TestTrue(TEXT("An old ability revision is rejected before accepted-sequence reservation"),
		Result.RejectReason == EGuLiWingmanRejectReason::StaleAbilitySetRevision);
	TestEqual(TEXT("Rejected ability versions never advance Candidate state"),
		Relay.GetLastAcceptedCandidateSequence(), 0u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanRelayTakeoverFreezeTest,
	"GuLiStrike.Wingman.Relay.Takeover.FrozenAbilityBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanRelayTakeoverFreezeTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTests;
	FGuLiWingmanRelayServer Relay;
	FGuid Owner;
	FGuid Backup;
	if (!InitializeRelay(*this, Relay, Owner, Backup) || !ActivateRelay(*this, Relay, Owner))
	{
		return false;
	}
	TestTrue(TEXT("An accepted pose exists before handoff"),
		Relay.SubmitCandidate(
			Owner, MakeCandidate(Relay), 0.1, FoundCarrier(), PermitWorld()).Disposition
			== EGuLiWingmanSubmissionDisposition::Accepted);

	const FGuid NewOwner(9u, 10u, 11u, 12u);
	TestTrue(TEXT("Takeover enters a new lease generation"), Relay.BeginTakeover(NewOwner, Owner, 0.2));
	FGuLiWingmanBootstrapBundle TakeoverBootstrap;
	TestTrue(TEXT("Takeover emits a frozen baseline"), Relay.BuildBootstrap(TakeoverBootstrap));
	TestTrue(TEXT("The baseline freezes both ability and accepted hashes"),
		TakeoverBootstrap.bHasTransferBaseline && TakeoverBootstrap.TransferBaseline.IsWellFormed());

	FGuLiGroupAbilityConfigSnapshot NewConfig = MakeAbilityConfig(Relay.GetLeaseState().Group, 4u);
	NewConfig.AbilitySetRevision++;
	NewConfig.RefreshHash();
	TestFalse(TEXT("Ability projection cannot change underneath an outstanding transfer baseline"),
		Relay.PublishAbilityConfig(NewConfig, 0.21));
	TestFalse(TEXT("Takeover cannot omit its frozen baseline"),
		Relay.AcknowledgeBootstrap(NewOwner, TakeoverBootstrap.Commit, nullptr, 0.22));

	FGuLiGroupAbilityConfigAck AbilityAck;
	AbilityAck.Group = Relay.GetLeaseState().Group;
	AbilityAck.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
	AbilityAck.SnapshotRevision = Relay.GetAbilityConfig().SnapshotRevision;
	AbilityAck.SnapshotHash = Relay.GetAbilityConfig().SnapshotHash;
	TestTrue(TEXT("Backup consumes the projection without an ASC/AbilitySpec"),
		Relay.AcknowledgeAbilityConfig(NewOwner, AbilityAck, 0.23));
	TestTrue(TEXT("Exact frozen baseline commits takeover"), Relay.AcknowledgeBootstrap(NewOwner,
		TakeoverBootstrap.Commit, &TakeoverBootstrap.TransferBaseline, 0.24));
	TestTrue(TEXT("The exact ACK opens, but cannot bypass, the required Takeover batch"),
		Relay.GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Unavailable
		&& Relay.GetActiveLeaseTransaction().State
			== EGuLiWingmanActiveLeaseTransactionState::AwaitingTakeoverBatch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanRelayTokenBucketTest,
	"GuLiStrike.Wingman.Relay.TokenBucket.MonotonicBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanRelayTokenBucketTest::RunTest(const FString& Parameters)
{
	FGuLiWingmanTokenBucket Bucket;
	Bucket.Reset(10.0, 2.0, 1.0);
	TestTrue(TEXT("First burst token is available"), Bucket.Consume(10.0));
	TestTrue(TEXT("Second burst token is available"), Bucket.Consume(10.0));
	TestFalse(TEXT("Capacity bounds a same-time burst"), Bucket.Consume(10.0));
	TestTrue(TEXT("One second refills one token"), Bucket.Consume(11.0));
	const double BeforeBackwardClock = Bucket.GetAvailableTokens();
	TestFalse(TEXT("A backward clock fails closed"), Bucket.Consume(9.0));
	TestEqual(TEXT("A backward clock does not mutate budget"), Bucket.GetAvailableTokens(), BeforeBackwardClock);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanRelayFireValidatorInjectionTest,
	"GuLiStrike.Wingman.Relay.FireIntent.AdditionalValidatorBeforeSequenceCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanRelayFireValidatorInjectionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTests;
	FGuLiWingmanRelayServer Relay;
	FGuid Owner;
	FGuid Backup;
	if (!InitializeRelay(*this, Relay, Owner, Backup) || !ActivateRelay(*this, Relay, Owner))
	{
		return false;
	}
	const FGuLiWingmanSubmissionResult CandidateResult = Relay.SubmitCandidate(
		Owner, MakeCandidate(Relay), 0.1, FoundCarrier(), PermitWorld());
	if (!TestTrue(TEXT("Fire test has a fresh accepted source"),
		CandidateResult.Disposition == EGuLiWingmanSubmissionDisposition::Accepted))
	{
		return false;
	}
	const FGuLiWingmanFireIntent Intent = MakeFireIntent(Relay, CandidateResult.AcceptedBatch);
	const FGuLiFireIntentServerValidator RejectLos = [](
		const FGuLiWingmanFireIntent&, const FGuLiWingmanAcceptedBatch&)
	{
		return EGuLiWingmanRejectReason::NoLineOfSight;
	};
	const FGuLiWingmanSubmissionResult Rejected = Relay.SubmitFireIntent(Owner, Intent, 0.15, RejectLos);
	TestTrue(TEXT("Combat can inject target/LOS/cooldown validation"),
		Rejected.RejectReason == EGuLiWingmanRejectReason::NoLineOfSight);
	const FGuLiWingmanSubmissionResult Accepted = Relay.SubmitFireIntent(Owner, Intent, 0.16);
	TestTrue(TEXT("Injected rejection does not reserve DomainFireSequence"),
		Accepted.Disposition == EGuLiWingmanSubmissionDisposition::Accepted);
	const FGuLiWingmanSubmissionResult Duplicate = Relay.SubmitFireIntent(Owner, Intent, 0.17);
	TestTrue(TEXT("A committed DomainFireSequence is idempotently rejected"),
		Duplicate.RejectReason == EGuLiWingmanRejectReason::Duplicate);
	return true;
}
#endif
