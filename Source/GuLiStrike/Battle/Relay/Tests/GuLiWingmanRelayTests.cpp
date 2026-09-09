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
		Config.MatchEpoch = 11u;
		Config.Team = EGuLiTeam::Red;
		Config.OwnerPlayerGuid = FGuid(1u, 2u, 3u, 4u);
		Config.WingmanTypeId = TEXT("TestWingman");
		Config.ShipGeneration = Group.ShipGeneration;
		Config.GroupGeneration = Group.GroupGeneration;
		Config.AbilitySetRevision = 7u;
		Config.LoadoutRevision = 9u;
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

		FGuLiWingmanWeaponChannelConfig& Basic = Config.WeaponChannels.AddDefaulted_GetRef();
		Basic.Binding = FGuLiWeaponBindingKey::Wingman(
			Config.MatchEpoch, Config.Team, Config.OwnerPlayerGuid, Config.WingmanTypeId, TEXT("BasicWeapon"));
		Basic.SkillId = TEXT("Test.Basic.Auto");
		Basic.AbilityId = Config.BasicWeaponAbilityId;
		Basic.Kind = EGuLiWingmanWeaponKind::BasicAutomatic;
		Basic.bEnabled = true;
		Basic.ProfileRevision = 4u;
		Basic.DefinitionRevision = Config.BasicWeaponDefinitionRevision;
		Basic.DefinitionChecksum = Config.BasicWeaponDefinitionChecksum;
		Basic.Runtime.CooldownSeconds = 2.0f;
		Config.BasicWeaponRuntime = Basic.Runtime;

		FGuLiWingmanWeaponChannelConfig& Missile = Config.WeaponChannels.AddDefaulted_GetRef();
		Missile.Binding = FGuLiWeaponBindingKey::Wingman(
			Config.MatchEpoch, Config.Team, Config.OwnerPlayerGuid, Config.WingmanTypeId, TEXT("Missile"));
		Missile.SkillId = TEXT("Test.Missile.Salvo");
		Missile.AbilityId = Config.MissileAbilityId;
		Missile.Kind = EGuLiWingmanWeaponKind::Missile;
		Missile.CooldownGroupId = TEXT("WingmanMissileSalvo");
		Missile.bEnabled = true;
		Missile.ProfileRevision = 5u;
		Missile.DefinitionRevision = Config.MissileDefinitionRevision;
		Missile.DefinitionChecksum = Config.MissileDefinitionChecksum;
		Missile.Runtime.Damage = 100.0f;
		Missile.Runtime.RangeCentimeters = 250000.0f;
		Missile.Runtime.CooldownSeconds = 8.0f;
		Missile.Runtime.ProjectileSpeedCentimetersPerSecond = 45000.0f;
		Missile.Runtime.ProjectileLifetimeSeconds = 8.0f;
		Missile.Runtime.SweepRadiusCentimeters = 150.0f;
		Missile.Runtime.TargetConeHalfAngleDegrees = 8.0f;
		Missile.Runtime.MaximumHomingTurnRateDegreesPerSecond = 45.0f;
		Config.MissileRuntime = Missile.Runtime;
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

	FGuLiWingmanCandidateBatch MakeStrictSlowFlightCandidate(
		const FGuLiWingmanRelayServer& Relay,
		const uint8 FlightIndex,
		const uint32 CandidateSequence,
		const uint32 FrameSequence,
		const uint32 BaseAcceptedSequence,
		const uint32 ClientSimTick,
		const double CaptureServerTime)
	{
		const FGuLiGroupAbilityConfigSnapshot& Config = Relay.GetAbilityConfig();
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
		Candidate.CaptureEstimatedServerTimeSeconds = CaptureServerTime;
		Candidate.NavSchemaRevision = Relay.GetValidationRevisions().NavSchemaRevision;
		Candidate.NavDataChecksum = Relay.GetValidationRevisions().NavDataChecksum;
		Candidate.TuningRevision = Relay.GetValidationRevisions().TuningRevision;
		Candidate.ObstacleRevision = Relay.GetValidationRevisions().ObstacleRevision;
		Candidate.CarrierSource.CanonicalEpoch = 8u;
		Candidate.CarrierSource.MoveRevision = CandidateSequence + 20u;
		Candidate.AbilitySetRevision = Config.AbilitySetRevision;
		Candidate.FormationCommandRevision = Config.FormationCommandRevision;
		Candidate.FormationDefinitionChecksum = Config.FormationDefinitionChecksum;
		for (const FGuLiWingmanRosterEntry& Entry : Relay.GetRoster())
		{
			if (Entry.bDead || Entry.Wingman.Flight.FlightIndex != FlightIndex)
			{
				continue;
			}
			Candidate.RequiredMemberMask |= static_cast<uint8>(1u << Entry.Wingman.MemberIndex);
			FGuLiWingmanCandidateSample& Sample = Candidate.Samples.AddDefaulted_GetRef();
			Sample.Wingman = Entry.Wingman;
			Sample.PositionCentimeters = FIntVector(
				50000, static_cast<int32>(Entry.Wingman.MemberIndex) * 4000, 10000);
			Sample.VelocityCentimetersPerSecond = FIntVector::ZeroValue;
			Sample.RotationCentiDegrees = FIntVector::ZeroValue;
			Sample.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Orbit);
		}
		return Candidate;
	}

	FGuLiWingmanEmergencyRebaseRequest MakeRebaseRequest(
		const FGuLiWingmanRelayServer& Relay,
		const FGuLiWingmanHandle& Wingman,
		const uint32 RequestSequence,
		const uint32 BaselineAcceptedSequence)
	{
		FGuLiWingmanEmergencyRebaseRequest Request;
		Request.MatchEpoch = Relay.GetMatchEpoch();
		Request.ConnectionGeneration = Relay.GetConnectionGeneration();
		Request.Wingman = Wingman;
		Request.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
		Request.RosterRevision = Relay.GetRosterRevision();
		Request.RequestSequence = RequestSequence;
		Request.BaselineAcceptedSequence = BaselineAcceptedSequence;
		Request.Reason = EGuLiWingmanEmergencyRebaseReason::PhysicalObstacleDeadlock;
		return Request;
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
		Intent.TargetAssignmentRevision = 1u;
		const FGuLiGroupAbilityConfigSnapshot& Config = Relay.GetAbilityConfig();
		const FGuLiWingmanWeaponChannelConfig* Channel =
			Config.FindFirstWeaponChannel(EGuLiWingmanWeaponKind::BasicAutomatic);
		if (!Channel) return FGuLiWingmanFireIntent{};
		Intent.Binding = Channel->Binding;
		Intent.WeaponAbilityId = Channel->AbilityId;
		Intent.SkillId = Channel->SkillId;
		Intent.LoadoutRevision = Config.LoadoutRevision;
		Intent.ProfileRevision = Channel->ProfileRevision;
		Intent.WeaponDefinitionRevision = Channel->DefinitionRevision;
		Intent.AbilitySetRevision = Config.AbilitySetRevision;
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
	"GuLiStrike.Wingman.Relay.CarrierSource.MetadataDoesNotBlockPoseRelay",
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

	int32 CarrierResolverCalls = 0;
	const FGuLiCarrierSourceResolver PendingCarrier = [&CarrierResolverCalls](
		const FGuLiCarrierSourceRef&, FGuLiRelayCarrierState&)
	{
		++CarrierResolverCalls;
		return EGuLiRelayCarrierLookupResult::Pending;
	};
	int32 WorldValidatorCalls = 0;
	const FGuLiCandidateWorldValidator RejectWorld = [&WorldValidatorCalls](
		const FGuLiWingmanCandidateWorldValidationContext&)
	{
		++WorldValidatorCalls;
		return EGuLiWingmanRejectReason::InvalidIdentity;
	};

	const FGuLiWingmanSubmissionResult First = Relay.SubmitCandidate(
		Owner, MakeCandidate(Relay, 1u, 100u), 0.1, PendingCarrier, RejectWorld);
	TestEqual(TEXT("Unresolved carrier metadata cannot delay a client-authored pose"),
		First.Disposition, EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("Pose relay never invokes the carrier resolver"), CarrierResolverCalls, 0);
	TestEqual(TEXT("Pose relay never invokes the World validator"), WorldValidatorCalls, 0);
	TestEqual(TEXT("The accepted pose advances immediately"),
		Relay.GetLastAcceptedCandidateSequence(), 1u);

	Relay.AdvanceTime(0.5, PendingCarrier);
	TArray<FGuLiWingmanSubmissionResult> Deferred;
	Relay.DrainDeferredCandidateResults(Deferred);
	TestTrue(TEXT("Pose relay creates no deferred carrier work"), Deferred.IsEmpty());

	FGuLiWingmanCandidateBatch SecondCandidate = MakeCandidate(Relay, 2u, 103u);
	SecondCandidate.CarrierSource.CanonicalEpoch = 99u;
	SecondCandidate.CarrierSource.MoveRevision = 999u;
	const FGuLiWingmanSubmissionResult Second = Relay.SubmitCandidate(
		Owner, SecondCandidate, 0.51, PendingCarrier, RejectWorld);
	TestEqual(TEXT("A changed but well-formed carrier reference is relayed as metadata"),
		Second.Disposition, EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("Both snapshots are stored without a movement-authority callback"),
		Relay.GetAcceptedHistory().Num(), 2);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanRelayWorldValidatorCommitGateTest,
	"GuLiStrike.Wingman.Relay.Validator.PoseRelayBypassesWorldValidation",
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

	const FGuLiWingmanSubmissionResult WithoutValidator = Relay.SubmitCandidate(
		Owner, MakeCandidate(Relay, 1u, 100u), 0.1, FoundCarrier());
	TestEqual(TEXT("Pose relay does not require a server World validator"),
		WithoutValidator.Disposition, EGuLiWingmanSubmissionDisposition::Accepted);

	int32 CallbackCount = 0;
	const FGuLiCandidateWorldValidator RejectWorld = [&CallbackCount](
		const FGuLiWingmanCandidateWorldValidationContext&)
	{
		++CallbackCount;
		return EGuLiWingmanRejectReason::InvalidIdentity;
	};
	const FGuLiWingmanCandidateBatch ClientAuthored = MakeCandidate(Relay, 2u, 103u);
	const FGuLiWingmanSubmissionResult WithRejectingValidator = Relay.SubmitCandidate(
		Owner, ClientAuthored, 0.2, FoundCarrier(), RejectWorld);
	TestEqual(TEXT("A server World callback cannot veto client-authored presentation motion"),
		WithRejectingValidator.Disposition, EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("The World callback is not invoked for pose relay"), CallbackCount, 0);
	TestEqual(TEXT("The server stores both complete client snapshots"),
		Relay.GetAcceptedHistory().Num(), 2);
	TestEqual(TEXT("The relayed position remains byte-semantic equivalent"),
		WithRejectingValidator.AcceptedBatch.Samples[0].PositionCentimeters,
		ClientAuthored.Samples[0].PositionCentimeters);

	FGuLiWingmanCandidateBatch WrongGeneration = MakeCandidate(Relay, 3u, 106u);
	++WrongGeneration.Samples[0].Wingman.EntityGeneration;
	const FGuLiWingmanSubmissionResult IdentityRejected = Relay.SubmitCandidate(
		Owner, WrongGeneration, 0.3, FoundCarrier(), RejectWorld);
	TestEqual(TEXT("A non-roster member generation is still rejected"),
		IdentityRejected.RejectReason, EGuLiWingmanRejectReason::EmitterDead);
	TestEqual(TEXT("Identity rejection cannot append a remote snapshot"),
		Relay.GetAcceptedHistory().Num(), 2);
	TestEqual(TEXT("Authority movement writer remains absent"),
		Relay.GetServerWingmanMovementWriteCount(), static_cast<uint64>(0u));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanRelayPendingStateGateTest,
	"GuLiStrike.Wingman.Relay.Validator.StructuralIdentityAndClientMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanRelayPendingStateGateTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTests;

	FGuLiWingmanRelayServer HealthRelay;
	FGuid HealthOwner;
	FGuid HealthBackup;
	if (!InitializeRelay(*this, HealthRelay, HealthOwner, HealthBackup)
		|| !ActivateRelay(*this, HealthRelay, HealthOwner))
	{
		return false;
	}
	const FGuLiWingmanCandidateBatch HealthBaseline = MakeCandidate(HealthRelay, 1u, 100u);
	TestEqual(TEXT("A live roster pose is relayed immediately"),
		HealthRelay.SubmitCandidate(
			HealthOwner, HealthBaseline, 0.1, FoundCarrier(), PermitWorld()).Disposition,
		EGuLiWingmanSubmissionDisposition::Accepted);
	TestTrue(TEXT("Reliable health can remove one member"),
		HealthRelay.SetWingmanHealthPermille(HealthBaseline.Samples[0].Wingman, 0u));
	const FGuLiWingmanSubmissionResult DeadMember = HealthRelay.SubmitCandidate(
		HealthOwner, MakeCandidate(HealthRelay, 2u, 103u), 0.2,
		FoundCarrier(), PermitWorld());
	TestEqual(TEXT("A reliable death Cut cannot discard live sibling poses"),
		DeadMember.Disposition, EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("The exact server-confirmed dead identity is filtered"),
		DeadMember.AcceptedBatch.Samples.Num(), GULI_WINGMAN_GROUP_SIZE - 1);
	TestFalse(TEXT("The dead identity is never republished"),
		DeadMember.AcceptedBatch.Samples.ContainsByPredicate(
			[&HealthBaseline](const FGuLiWingmanCandidateSample& Sample)
			{
				return Sample.Wingman == HealthBaseline.Samples[0].Wingman;
			}));

	FGuLiWingmanRelayServer AbilityRelay;
	FGuid AbilityOwner;
	FGuid AbilityBackup;
	if (!InitializeRelay(*this, AbilityRelay, AbilityOwner, AbilityBackup)
		|| !ActivateRelay(*this, AbilityRelay, AbilityOwner))
	{
		return false;
	}
	FGuLiWingmanCandidateBatch OldProjection = MakeCandidate(AbilityRelay, 1u, 100u);
	--OldProjection.AbilitySetRevision;
	const FGuLiWingmanSubmissionResult AbilityResult = AbilityRelay.SubmitCandidate(
		AbilityOwner, OldProjection, 0.1, FoundCarrier(), PermitWorld());
	TestEqual(TEXT("A stale ability projection cannot interrupt position relay"),
		AbilityResult.Disposition, EGuLiWingmanSubmissionDisposition::Accepted);

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
	if (!TestEqual(TEXT("The client publishes its Orbit baseline"),
		InitialAccepted.Disposition, EGuLiWingmanSubmissionDisposition::Accepted))
	{
		return false;
	}
	FGuLiWingmanCandidateBatch Recovery = MakeKinematicCandidate(
		ModeRelay, InitialAccepted.AcceptedBatch, 2u, 106u,
		FVector(20000.0, 0.0, 5000.0), FVector::ZeroVector);
	for (FGuLiWingmanCandidateSample& Sample : Recovery.Samples)
	{
		Sample.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Recover);
	}
	const FGuLiWingmanSubmissionResult RecoveryAccepted = ModeRelay.SubmitCandidate(
		ModeOwner, Recovery, 0.2, FoundCarrier(), PermitWorld());
	TestEqual(TEXT("The server relays a client-selected recovery mode and reposition"),
		RecoveryAccepted.Disposition, EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("All branches preserve server zero-movement"),
		ModeRelay.GetServerWingmanMovementWriteCount(), static_cast<uint64>(0u));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanRelayMotionEnvelopeTest,
	"GuLiStrike.Wingman.Relay.Validator.ClientAuthoredPoseRelay",
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
	const FGuLiWingmanSubmissionResult InitialResult = Relay.SubmitCandidate(
		Owner, Initial, 0.1, FoundCarrier(), PermitWorld());
	if (!TestEqual(TEXT("A complete client pose establishes the relay baseline"),
		InitialResult.Disposition, EGuLiWingmanSubmissionDisposition::Accepted))
	{
		return false;
	}

	int32 CarrierResolverCalls = 0;
	const FGuLiCarrierSourceResolver RejectCarrier = [&CarrierResolverCalls](
		const FGuLiCarrierSourceRef&, FGuLiRelayCarrierState&)
	{
		++CarrierResolverCalls;
		return EGuLiRelayCarrierLookupResult::Expired;
	};
	int32 WorldValidatorCalls = 0;
	const FGuLiCandidateWorldValidator RejectWorld = [&WorldValidatorCalls](
		const FGuLiWingmanCandidateWorldValidationContext&)
	{
		++WorldValidatorCalls;
		return EGuLiWingmanRejectReason::InvalidIdentity;
	};

	FGuLiWingmanCandidateBatch ClientRecovery = MakeKinematicCandidate(
		Relay, InitialResult.AcceptedBatch, 2u, 100u,
		FVector(20000.0, -30000.0, 15000.0), FVector(8000.0, 4000.0, -1000.0));
	for (FGuLiWingmanCandidateSample& Sample : ClientRecovery.Samples)
	{
		Sample.FlightMode = static_cast<uint8>(EGuLiWingmanFlightMode::Recover);
	}
	const FGuLiWingmanSubmissionResult RecoveryResult = Relay.SubmitCandidate(
		Owner, ClientRecovery, 0.11, RejectCarrier, RejectWorld);
	if (!TestEqual(TEXT("Client recovery may reposition and rotate without server kinematic approval"),
		RecoveryResult.Disposition, EGuLiWingmanSubmissionDisposition::Accepted))
	{
		return false;
	}
	TestEqual(TEXT("Carrier resolver is outside normal pose acceptance"), CarrierResolverCalls, 0);
	TestEqual(TEXT("World validator is outside normal pose acceptance"), WorldValidatorCalls, 0);
	TestEqual(TEXT("Recovery position is relayed exactly"),
		RecoveryResult.AcceptedBatch.Samples[0].PositionCentimeters,
		ClientRecovery.Samples[0].PositionCentimeters);
	TestEqual(TEXT("Recovery velocity is relayed exactly"),
		RecoveryResult.AcceptedBatch.Samples[0].VelocityCentimetersPerSecond,
		ClientRecovery.Samples[0].VelocityCentimetersPerSecond);
	TestEqual(TEXT("Recovery rotation is relayed exactly"),
		RecoveryResult.AcceptedBatch.Samples[0].RotationCentiDegrees,
		ClientRecovery.Samples[0].RotationCentiDegrees);

	const FGuLiWingmanSubmissionResult Replay = Relay.SubmitCandidate(
		Owner, ClientRecovery, 0.12, RejectCarrier, RejectWorld);
	TestEqual(TEXT("An already relayed CandidateSequence is still rejected as duplicate"),
		Replay.RejectReason, EGuLiWingmanRejectReason::Duplicate);
	TestEqual(TEXT("Duplicate rejection preserves the two accepted snapshots"),
		Relay.GetAcceptedHistory().Num(), 2);

	const FGuLiWingmanSubmissionResult WrongOwner = Relay.SubmitCandidate(
		Backup, MakeCandidate(Relay, 3u, 103u), 0.13, RejectCarrier, RejectWorld);
	TestEqual(TEXT("A non-owner cannot publish poses for this lease"),
		WrongOwner.RejectReason, EGuLiWingmanRejectReason::WrongLease);

	FGuLiWingmanCandidateBatch WrongGeneration = MakeCandidate(Relay, 3u, 103u);
	++WrongGeneration.Samples[0].Wingman.EntityGeneration;
	const FGuLiWingmanSubmissionResult IdentityRejected = Relay.SubmitCandidate(
		Owner, WrongGeneration, 0.14, RejectCarrier, RejectWorld);
	TestEqual(TEXT("A forged member generation is rejected"),
		IdentityRejected.RejectReason, EGuLiWingmanRejectReason::EmitterDead);

	FGuLiWingmanCandidateBatch InvalidDto = MakeCandidate(Relay, 3u, 103u);
	InvalidDto.Samples[0].VelocityCentimetersPerSecond.X = 200001;
	const FGuLiWingmanSubmissionResult DtoRejected = Relay.SubmitCandidate(
		Owner, InvalidDto, 0.15, RejectCarrier, RejectWorld);
	TestEqual(TEXT("An out-of-domain serialized velocity remains structurally invalid"),
		DtoRejected.RejectReason, EGuLiWingmanRejectReason::InvalidIdentity);
	TestEqual(TEXT("Structural rejects keep the last good remote pose"),
		Relay.GetAcceptedHistory().Num(), 2);
	TestEqual(TEXT("Server pose relay never writes a Wingman Transform"),
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
	TestEqual(TEXT("An old ability projection does not interrupt presentation pose relay"),
		Result.Disposition, EGuLiWingmanSubmissionDisposition::Accepted);
	TestEqual(TEXT("The stale projection still advances only the pose sequence"),
		Relay.GetLastAcceptedCandidateSequence(), 1u);
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
	Relay.AttackState.Revision = 3u;
	FGuLiWingmanAutoTargetAssignment& AutomaticTarget =
		Relay.AttackState.AutomaticTargets.AddDefaulted_GetRef();
	AutomaticTarget.Emitter = Relay.GetRoster()[0].Wingman;
	AutomaticTarget.Target.Target.Kind = EGuLiTargetKind::Ship;
	AutomaticTarget.Target.Target.AuthorityId = FGuid(21u, 22u, 23u, 24u);
	AutomaticTarget.Target.Target.Generation = 1u;
	AutomaticTarget.Target.Target.LocalId = 9u;
	AutomaticTarget.Target.Location = FVector(50000.0, 1000.0, 2000.0);
	AutomaticTarget.Target.Radius = 1500.0f;
	AutomaticTarget.Target.Revision = 4u;
	AutomaticTarget.Target.ServerTime = 0.1;
	TestTrue(TEXT("The live per-member automatic target state is valid before handoff"),
		Relay.AttackState.IsWellFormed(Relay.GetLeaseState().Group));
	const uint64 AutomaticTargetHash = Relay.AttackState.ComputeStableHash();
	const FGuLiWingmanAutoTargetAssignment ExpectedAutomaticTarget = AutomaticTarget;

	const FGuid NewOwner(9u, 10u, 11u, 12u);
	TestTrue(TEXT("Takeover enters a new lease generation"), Relay.BeginTakeover(NewOwner, Owner, 0.2));
	FGuLiWingmanBootstrapBundle TakeoverBootstrap;
	TestTrue(TEXT("Takeover emits a frozen baseline"), Relay.BuildBootstrap(TakeoverBootstrap));
	TestTrue(TEXT("The baseline freezes both ability and accepted hashes"),
		TakeoverBootstrap.bHasTransferBaseline && TakeoverBootstrap.TransferBaseline.IsWellFormed());
	TestTrue(TEXT("Takeover preserves the exact per-member automatic target table"),
		TakeoverBootstrap.AttackStateHash == AutomaticTargetHash
		&& TakeoverBootstrap.AttackState.AutomaticTargets.Num() == 1
		&& TakeoverBootstrap.AttackState.AutomaticTargets[0].Emitter == ExpectedAutomaticTarget.Emitter
		&& TakeoverBootstrap.AttackState.AutomaticTargets[0].Target.Target
			== ExpectedAutomaticTarget.Target.Target);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanWeaponChannelBootstrapRecoveryTest,
	"GuLiStrike.Wingman.Relay.WeaponChannelBootstrapRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanWeaponChannelBootstrapRecoveryTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTests;
	FGuLiWingmanRelayServer Relay;
	FGuid Owner;
	FGuid Backup;
	if (!InitializeRelay(*this, Relay, Owner, Backup))
	{
		return false;
	}
	const FGuLiGroupAbilityConfigSnapshot InitialConfig = Relay.GetAbilityConfig();
	if (!TestTrue(TEXT("Initial protocol-v9 weapon projection is usable"),
		InitialConfig.IsUsableByLeaseOwner())
		|| !TestEqual(TEXT("Initial projection carries its complete channel array"),
			InitialConfig.WeaponChannels.Num(), 2))
	{
		return false;
	}

	FGuLiGroupAbilityConfigSnapshot ChannelChanged = InitialConfig;
	++ChannelChanged.WeaponChannels[1].ProfileRevision;
	ChannelChanged.RefreshHash();
	TestTrue(TEXT("A valid channel payload mutation remains structurally valid"), ChannelChanged.IsWellFormed());
	TestTrue(TEXT("Every weapon-channel field participates in the snapshot hash"),
		ChannelChanged.SnapshotHash != InitialConfig.SnapshotHash);

	FGuLiGroupAbilityConfigSnapshot LoadoutChanged = InitialConfig;
	++LoadoutChanged.LoadoutRevision;
	LoadoutChanged.RefreshHash();
	TestTrue(TEXT("LoadoutRevision participates in the snapshot hash"),
		LoadoutChanged.IsWellFormed() && LoadoutChanged.SnapshotHash != InitialConfig.SnapshotHash);

	FGuLiGroupAbilityConfigSnapshot OwnerChanged = InitialConfig;
	OwnerChanged.OwnerPlayerGuid = FGuid(9u, 10u, 11u, 12u);
	for (FGuLiWingmanWeaponChannelConfig& Channel : OwnerChanged.WeaponChannels)
	{
		Channel.Binding.OwnerPlayerGuid = OwnerChanged.OwnerPlayerGuid;
	}
	OwnerChanged.RefreshHash();
	TestTrue(TEXT("Stable growth owner context participates in the snapshot hash"),
		OwnerChanged.IsWellFormed() && OwnerChanged.SnapshotHash != InitialConfig.SnapshotHash);

	FGuLiGroupAbilityConfigSnapshot TypeChanged = InitialConfig;
	TypeChanged.WingmanTypeId = TEXT("AlternateWingman");
	for (FGuLiWingmanWeaponChannelConfig& Channel : TypeChanged.WeaponChannels)
	{
		Channel.Binding.SubjectId = TypeChanged.WingmanTypeId;
	}
	TypeChanged.RefreshHash();
	TestTrue(TEXT("Wingman type context participates in the snapshot hash"),
		TypeChanged.IsWellFormed() && TypeChanged.SnapshotHash != InitialConfig.SnapshotHash);

	FGuLiGroupAbilityConfigSnapshot OldProtocol = InitialConfig;
	OldProtocol.ProtocolVersion = GULI_WINGMAN_PROTOCOL_VERSION - 1u;
	++OldProtocol.SnapshotRevision;
	OldProtocol.RefreshHash();
	TestFalse(TEXT("A prior protocol snapshot cannot masquerade as current"), OldProtocol.IsWellFormed());
	TestFalse(TEXT("Relay refuses to publish a prior protocol as a newer current snapshot"),
		Relay.PublishAbilityConfig(OldProtocol, 0.005));

	FGuLiWingmanBootstrapBundle InitialBootstrap;
	if (!TestTrue(TEXT("Reliable bootstrap is built from the current complete projection"),
		Relay.BuildBootstrap(InitialBootstrap)))
	{
		return false;
	}
	const FGuLiWingmanBootstrapScopeState* AbilityScope =
		InitialBootstrap.Commit.FindScope(EGuLiWingmanBootstrapScope::GroupAbilityConfig);
	if (!TestNotNull(TEXT("Bootstrap contains the GroupAbilityConfig scope"), AbilityScope))
	{
		return false;
	}
	TestEqual(TEXT("Bootstrap ability scope freezes SnapshotRevision"),
		AbilityScope->Revision, InitialConfig.SnapshotRevision);
	TestEqual(TEXT("Bootstrap ability scope freezes the full projection hash"),
		AbilityScope->Hash, InitialConfig.SnapshotHash);
	TestEqual(TEXT("Bootstrap preserves LoadoutRevision"),
		InitialBootstrap.AbilityConfig.LoadoutRevision, InitialConfig.LoadoutRevision);
	TestEqual(TEXT("Bootstrap preserves stable growth owner"),
		InitialBootstrap.AbilityConfig.OwnerPlayerGuid, InitialConfig.OwnerPlayerGuid);
	TestEqual(TEXT("Bootstrap preserves WingmanTypeId"),
		InitialBootstrap.AbilityConfig.WingmanTypeId, InitialConfig.WingmanTypeId);
	TestEqual(TEXT("Bootstrap preserves every channel"),
		InitialBootstrap.AbilityConfig.WeaponChannels.Num(), InitialConfig.WeaponChannels.Num());
	for (int32 Index = 0; Index < InitialConfig.WeaponChannels.Num(); ++Index)
	{
		const FGuLiWingmanWeaponChannelConfig& Source = InitialConfig.WeaponChannels[Index];
		const FGuLiWingmanWeaponChannelConfig& Copy = InitialBootstrap.AbilityConfig.WeaponChannels[Index];
		TestTrue(TEXT("Bootstrap channel preserves Binding"), Copy.Binding == Source.Binding);
		TestEqual(TEXT("Bootstrap channel preserves SkillId"), Copy.SkillId, Source.SkillId);
		TestTrue(TEXT("Bootstrap channel preserves AbilityId"), Copy.AbilityId == Source.AbilityId);
		TestEqual(TEXT("Bootstrap channel preserves ProfileRevision"), Copy.ProfileRevision, Source.ProfileRevision);
	}
	if (!ActivateRelay(*this, Relay, Owner))
	{
		return false;
	}

	TestTrue(TEXT("Configured backup becomes the recovery lease owner"),
		Relay.BeginTakeover(Backup, Owner, 0.2));
	FGuLiWingmanBootstrapBundle TakeoverBootstrap;
	if (!TestTrue(TEXT("Backup takeover builds a frozen reliable cut"),
		Relay.BuildBootstrap(TakeoverBootstrap)))
	{
		return false;
	}
	TestTrue(TEXT("Takeover cut includes its transfer baseline"),
		TakeoverBootstrap.bHasTransferBaseline && TakeoverBootstrap.TransferBaseline.IsWellFormed());
	TestEqual(TEXT("Takeover keeps the exact full ability projection hash"),
		TakeoverBootstrap.AbilityConfig.SnapshotHash, InitialConfig.SnapshotHash);
	TestEqual(TEXT("Transfer baseline freezes that same ability hash"),
		TakeoverBootstrap.TransferBaseline.AbilityConfigHash, InitialConfig.SnapshotHash);
	TestEqual(TEXT("Lease takeover does not rewrite the stable growth owner"),
		TakeoverBootstrap.AbilityConfig.OwnerPlayerGuid, Owner);
	TestEqual(TEXT("Lease takeover preserves the complete channel array"),
		TakeoverBootstrap.AbilityConfig.WeaponChannels.Num(), InitialConfig.WeaponChannels.Num());

	FGuLiGroupAbilityConfigAck TakeoverAck;
	TakeoverAck.Group = Relay.GetLeaseState().Group;
	TakeoverAck.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
	TakeoverAck.SnapshotRevision = InitialConfig.SnapshotRevision;
	TakeoverAck.SnapshotHash = InitialConfig.SnapshotHash;
	TestTrue(TEXT("Backup acknowledges the exact retained weapon projection"),
		Relay.AcknowledgeAbilityConfig(Backup, TakeoverAck, 0.21));
	FGuLiWingmanBootstrapCommit OldCommit = TakeoverBootstrap.Commit;
	OldCommit.ProtocolVersion = GULI_WINGMAN_PROTOCOL_VERSION - 1u;
	TestFalse(TEXT("An old reliable-cut protocol cannot complete recovery"),
		Relay.AcknowledgeBootstrap(
			Backup, OldCommit, &TakeoverBootstrap.TransferBaseline, 0.215));
	TestTrue(TEXT("The exact current cut advances recovery to its takeover-batch gate"),
		Relay.AcknowledgeBootstrap(
			Backup, TakeoverBootstrap.Commit, &TakeoverBootstrap.TransferBaseline, 0.22));
	TestTrue(TEXT("Recovery keeps the group unavailable until its required takeover batch"),
		Relay.GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Unavailable
		&& Relay.GetActiveLeaseTransaction().State
			== EGuLiWingmanActiveLeaseTransactionState::AwaitingTakeoverBatch);

	Relay.Revoke(0.3);
	const FGuLiGroupAbilityConfigSnapshot& Tombstone = Relay.GetAbilityConfig();
	TestTrue(TEXT("Revocation publishes one structurally valid tombstone"), Tombstone.IsWellFormed());
	TestFalse(TEXT("Tombstone cannot authorize the old group"), Tombstone.IsUsableByLeaseOwner());
	TestEqual(TEXT("Tombstone preserves match ownership context"), Tombstone.OwnerPlayerGuid, Owner);
	TestEqual(TEXT("Tombstone preserves WingmanTypeId context"),
		Tombstone.WingmanTypeId, InitialConfig.WingmanTypeId);
	TestEqual(TEXT("Tombstone clears LoadoutRevision"), Tombstone.LoadoutRevision, 0u);
	TestEqual(TEXT("Tombstone clears the complete channel array"), Tombstone.WeaponChannels.Num(), 0);
	TestTrue(TEXT("Tombstone advances beyond and cannot equal the old current snapshot"),
		Tombstone.SnapshotRevision > InitialConfig.SnapshotRevision
		&& !Tombstone.HasSameVersion(InitialConfig));
	TestFalse(TEXT("A revoked group cannot rebuild a usable bootstrap"), Relay.BuildBootstrap(InitialBootstrap));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanEmergencyRebaseAuthorityTest,
	"GuLiStrike.Wingman.Relay.EmergencyRebase.Authority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanEmergencyRebaseAuthorityTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanRelayTests;
	FGuLiWingmanRelayServer Relay;
	FGuid Owner;
	FGuid Backup;
	if (!InitializeRelay(*this, Relay, Owner, Backup))
	{
		return false;
	}
	FGuLiWingmanRelayValidationRevisions Revisions;
	Revisions.NavSchemaRevision = 1u;
	Revisions.NavDataChecksum = 0x12345678u;
	Revisions.TuningRevision = 1u;
	Revisions.ObstacleRevision = 1u;
	if (!TestTrue(TEXT("The fixture enables the v13 strict Flight contract"),
		Relay.ConfigureStrictFlightContract(3u, Revisions, 0.03)))
	{
		return false;
	}
	FGuLiWingmanBootstrapBundle Bootstrap;
	if (!TestTrue(TEXT("The strict v13 bootstrap builds"), Relay.BuildBootstrap(Bootstrap)))
	{
		return false;
	}
	FGuLiGroupAbilityConfigAck AbilityAck;
	AbilityAck.Group = Relay.GetLeaseState().Group;
	AbilityAck.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
	AbilityAck.SnapshotRevision = Relay.GetAbilityConfig().SnapshotRevision;
	AbilityAck.SnapshotHash = Relay.GetAbilityConfig().SnapshotHash;
	TestTrue(TEXT("The strict ability projection is acknowledged"),
		Relay.AcknowledgeAbilityConfig(Owner, AbilityAck, 0.04));
	TestTrue(TEXT("The strict six-scope cut is acknowledged"),
		Relay.AcknowledgeBootstrap(Owner, Bootstrap.Commit, nullptr, 0.05));

	TArray<FGuLiWingmanCandidateBatch> SlowFlights;
	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		SlowFlights.Add(MakeStrictSlowFlightCandidate(
			Relay, FlightIndex, FlightIndex + 1u, 1u, 0u, 100u, 0.1));
		TestTrue(TEXT("Each strict slow Flight candidate is well formed"),
			SlowFlights.Last().IsWellFormed());
	}
	FGuLiWingmanAtomicCandidateBatchFragment Fragment;
	Fragment.Header.BatchId = 1u;
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
	Fragment.Header.FragmentCount = 1u;
	Fragment.Header.BatchPayloadHash = GuLiWingmanRelayHash::CandidatePayloads(SlowFlights);
	Fragment.FragmentIndex = 0u;
	Fragment.Flights = SlowFlights;
	for (const FGuLiWingmanCandidateBatch& Flight : SlowFlights)
	{
		FGuLiWingmanAtomicCandidateBatchFragment OneFlight;
		OneFlight.Flights.Add(Flight);
		Fragment.Header.BatchPayloadBytes += OneFlight.EstimatePayloadBytes();
	}
	const FGuLiWingmanAtomicBatchAcceptance AtomicAccepted =
		Relay.SubmitAtomicCandidateFragment(
			Owner, Fragment, 0.1, FoundCarrier(), PermitWorld());
	if (!TestEqual(TEXT("The slow all-Flight baseline activates atomically"),
		AtomicAccepted.Disposition, EGuLiWingmanSubmissionDisposition::Accepted))
	{
		return false;
	}
	const FGuLiWingmanAcceptedBatch* InitialFlight = Relay.GetAcceptedHistory().FindByPredicate(
		[](const FGuLiWingmanAcceptedBatch& Batch)
		{
			return Batch.FlightIndex == 0u;
		});
	if (!TestNotNull(TEXT("The Accepted Store contains Flight zero"), InitialFlight))
	{
		return false;
	}
	const FGuLiWingmanCandidateBatch& SlowCandidate = SlowFlights[0];
	const FGuLiWingmanHandle FirstMember = SlowCandidate.Samples[0].Wingman;
	const FGuLiWingmanHandle SecondMember = SlowCandidate.Samples[1].Wingman;
	const uint32 SiblingFlightBaseline = Relay.GetAcceptedSequenceForFlight(1u);
	FGuLiWingmanAcceptedBatch AcceptedCut;

	FGuLiWingmanEmergencyRebaseRequest WrongLease = MakeRebaseRequest(
		Relay, FirstMember, 1u, InitialFlight->StateRef.AcceptedSequence);
	const FGuLiWingmanEmergencyRebaseResponse WrongLeaseResult = Relay.SubmitEmergencyRebase(
		Backup, WrongLease, 1.7, FoundCarrier(), PermitWorld(), AcceptedCut);
	TestEqual(TEXT("A non-owner cannot request a member rebase"), WrongLeaseResult.Result,
		EGuLiWingmanEmergencyRebaseResult::WrongLease);

	FGuLiWingmanEmergencyRebaseRequest Stale = MakeRebaseRequest(
		Relay, FirstMember, 2u, InitialFlight->StateRef.AcceptedSequence + 1u);
	const FGuLiWingmanEmergencyRebaseResponse StaleResult = Relay.SubmitEmergencyRebase(
		Owner, Stale, 1.7, FoundCarrier(), PermitWorld(), AcceptedCut);
	TestEqual(TEXT("A stale Accepted baseline cannot relocate a member"), StaleResult.Result,
		EGuLiWingmanEmergencyRebaseResult::StaleBaseline);

	FGuLiWingmanAutoTargetAssignment& Assignment =
		Relay.AttackState.AutomaticTargets.AddDefaulted_GetRef();
	Assignment.Emitter = FirstMember;
	Assignment.Target.Target.Kind = EGuLiTargetKind::CommanderSoldier;
	Assignment.Target.Target.AuthorityId = FGuid(10u, 11u, 12u, 13u);
	Assignment.Target.Target.Generation = 1u;
	Assignment.Target.Target.LocalId = 1u;
	Assignment.Target.Location = FVector(100000.0, 0.0, 0.0);
	Assignment.Target.Radius = 100.0f;
	Assignment.Target.Revision = 1u;
	Assignment.Target.ServerTime = 1.6;
	FGuLiWingmanAttackCheckpoint& Checkpoint = Relay.AttackState.Checkpoints.AddDefaulted_GetRef();
	Checkpoint.Emitter = FirstMember;
	Checkpoint.SlotId = TEXT("BasicWeapon");
	Checkpoint.SkillId = TEXT("Test.Basic.Auto");
	Checkpoint.DefinitionChecksum = 1u;
	Checkpoint.FrozenTargetHandle = Assignment.Target.Target;
	Checkpoint.ProfileRevision = 1u;
	Checkpoint.RunId = 1u;
	Checkpoint.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
	Checkpoint.LastShotIndex = 0;
	Checkpoint.StartTime = 1.0;
	Checkpoint.NextFireTime = 2.0;
	Checkpoint.FrozenTarget = Assignment.Target.Location;
	Checkpoint.ApproachDirection = FVector::ForwardVector;

	bool bInvalidationCallback = false;
	Relay.OnEmergencyRebaseAccepted = [&bInvalidationCallback, &FirstMember](
		const FGuLiWingmanHandle& Emitter)
	{
		bInvalidationCallback = Emitter == FirstMember;
	};
	FGuLiWingmanEmergencyRebaseRequest Valid = MakeRebaseRequest(
		Relay, FirstMember, 3u, InitialFlight->StateRef.AcceptedSequence);
	const FGuLiWingmanEmergencyRebaseResponse Accepted = Relay.SubmitEmergencyRebase(
		Owner, Valid, 1.7, FoundCarrier(), PermitWorld(), AcceptedCut);
	TestEqual(TEXT("A member with sufficient low-speed history is rebased"), Accepted.Result,
		EGuLiWingmanEmergencyRebaseResult::Accepted);
	TestTrue(TEXT("The response and complete Flight Cut are well formed"),
		Accepted.IsWellFormed() && AcceptedCut.IsWellFormed());
	TestEqual(TEXT("Only the affected member is marked in the Flight Cut"),
		AcceptedCut.RebasedMemberMask, static_cast<uint8>(1u << FirstMember.MemberIndex));
	TestEqual(TEXT("Only the affected Flight Accepted sequence advances"),
		Relay.GetAcceptedSequenceForFlight(0u), Accepted.AcceptedSequence);
	TestEqual(TEXT("Sibling Flights keep their baseline"),
		Relay.GetAcceptedSequenceForFlight(1u), SiblingFlightBaseline);
	TestTrue(TEXT("The server chooses a new member position"),
		Accepted.ServerPosition != FVector(SlowCandidate.Samples[0].PositionCentimeters));
	TestTrue(TEXT("Attack authorization cleanup callback is emitted"), bInvalidationCallback);
	TestFalse(TEXT("The rebased member automatic assignment is cleared"),
		Relay.AttackState.AutomaticTargets.ContainsByPredicate(
			[&FirstMember](const FGuLiWingmanAutoTargetAssignment& Entry)
			{
				return Entry.Emitter == FirstMember;
			}));
	TestFalse(TEXT("The rebased member frozen run is cleared"),
		Relay.AttackState.Checkpoints.ContainsByPredicate(
			[&FirstMember](const FGuLiWingmanAttackCheckpoint& Entry)
			{
				return Entry.Emitter == FirstMember;
			}));

	FGuLiWingmanEmergencyRebaseRequest RateLimited = MakeRebaseRequest(
		Relay, FirstMember, 4u, Accepted.AcceptedSequence);
	const FGuLiWingmanEmergencyRebaseResponse RateLimitedResult = Relay.SubmitEmergencyRebase(
		Owner, RateLimited, 1.8, FoundCarrier(), PermitWorld(), AcceptedCut);
	TestEqual(TEXT("A member rebase is limited to once per five seconds"),
		RateLimitedResult.Result, EGuLiWingmanEmergencyRebaseResult::RateLimited);
	TestTrue(TEXT("The rate-limited response exposes the retry boundary"),
		RateLimitedResult.RetryAfterServerTimeSeconds >= 6.7);

	const FGuLiCandidateWorldValidator RejectEveryPoint =
		[](const FGuLiWingmanCandidateWorldValidationContext&)
		{
			return EGuLiWingmanRejectReason::InvalidIdentity;
		};
	FGuLiWingmanEmergencyRebaseRequest NoSafePoint = MakeRebaseRequest(
		Relay, SecondMember, 1u, Accepted.AcceptedSequence);
	const FGuLiWingmanEmergencyRebaseResponse NoSafeResult = Relay.SubmitEmergencyRebase(
		Owner, NoSafePoint, 1.8, FoundCarrier(), RejectEveryPoint, AcceptedCut);
	TestEqual(TEXT("Authority fails closed when no generated point passes World/FlightNav"),
		NoSafeResult.Result, EGuLiWingmanEmergencyRebaseResult::NoSafePoint);
	TestTrue(TEXT("No-safe-point retry is bounded to one second"),
		FMath::IsNearlyEqual(NoSafeResult.RetryAfterServerTimeSeconds, 2.8));
	return true;
}
#endif
