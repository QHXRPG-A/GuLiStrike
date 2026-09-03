// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Relay/GuLiWingmanRelayAuthorityRegistry.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Misc/AutomationTest.h"

namespace GuLiWingmanAuthorityRegistryTests
{
	FGuLiWingmanGroupHandle MakeGroup(const uint32 Seed = 1u)
	{
		FGuLiWingmanGroupHandle Group;
		Group.ShipInstanceId = FGuid(0x11000000u + Seed, 0x22000000u, 0x33000000u, 0x44000000u);
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
		Config.FormationDefinitionChecksum = 0x1011u;
		Config.BasicWeaponDefinitionRevision = 7u;
		Config.BasicWeaponDefinitionChecksum = 0x1213u;
		Config.MissileDefinitionRevision = 8u;
		Config.MissileDefinitionChecksum = 0x1415u;
		Config.FormationCommandRevision = 9u;
		Config.EffectiveClientSimTick = 10u;
		Config.RefreshHash();
		return Config;
	}

	bool Activate(FAutomationTestBase& Test, FGuLiWingmanRelayServer& Relay, const FGuid& Owner)
	{
		FGuLiWingmanBootstrapBundle Bootstrap;
		if (!Test.TestTrue(TEXT("Build initial six-scope Bootstrap"), Relay.BuildBootstrap(Bootstrap)))
		{
			return false;
		}
		FGuLiGroupAbilityConfigAck AbilityAck;
		AbilityAck.Group = Bootstrap.Commit.Group;
		AbilityAck.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
		AbilityAck.SnapshotRevision = Bootstrap.AbilityConfig.SnapshotRevision;
		AbilityAck.SnapshotHash = Bootstrap.AbilityConfig.SnapshotHash;
		if (!Test.TestTrue(TEXT("ACK projected ability config"),
			Relay.AcknowledgeAbilityConfig(Owner, AbilityAck, 0.01)))
		{
			return false;
		}
		const FGuLiWingmanTransferBaseline* Baseline = Bootstrap.bHasTransferBaseline
			? &Bootstrap.TransferBaseline : nullptr;
		return Test.TestTrue(TEXT("ACK atomic Bootstrap"),
			Relay.AcknowledgeBootstrap(Owner, Bootstrap.Commit, Baseline, 0.02));
	}

	FGuLiWingmanCandidateBatch MakeCandidate(
		const FGuLiWingmanRelayServer& Relay, const uint32 Sequence = 1u)
	{
		FGuLiWingmanCandidateBatch Candidate;
		Candidate.Group = Relay.GetLeaseState().Group;
		Candidate.MatchEpoch = Relay.GetMatchEpoch();
		Candidate.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
		Candidate.CandidateSequence = Sequence;
		Candidate.ClientSimTick = 100u + Sequence;
		Candidate.CarrierSource.CanonicalEpoch = 1u;
		Candidate.CarrierSource.MoveRevision = 1u;
		Candidate.AbilitySetRevision = Relay.GetAbilityConfig().AbilitySetRevision;
		Candidate.FormationCommandRevision = Relay.GetAbilityConfig().FormationCommandRevision;
		Candidate.FormationDefinitionChecksum = Relay.GetAbilityConfig().FormationDefinitionChecksum;
		for (const FGuLiWingmanRosterEntry& Entry : Relay.GetRoster())
		{
			FGuLiWingmanCandidateSample& Sample = Candidate.Samples.AddDefaulted_GetRef();
			Sample.Wingman = Entry.Wingman;
			Sample.PositionCentimeters = FIntVector(
				Entry.Wingman.GetGroupMemberIndex() * 100, 0, 1000);
			Sample.VelocityCentimetersPerSecond = FIntVector(100, 0, 0);
		}
		return Candidate;
	}

	FGuLiCarrierSourceResolver FoundCarrier()
	{
		return [](const FGuLiCarrierSourceRef&, FGuLiRelayCarrierState& OutState)
		{
			OutState.Transform = FTransform::Identity;
			OutState.Velocity = FVector::ZeroVector;
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanRelayPersistentDisconnectTakeoverTest,
	"GuLiStrike.Wingman.Relay.AuthorityRegistry.PersistentDisconnectTakeover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanRelayPersistentDisconnectTakeoverTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanAuthorityRegistryTests;
	FGuLiWingmanRelayAuthorityRegistry Registry;
	const FGuLiWingmanGroupHandle Group = MakeGroup();
	const FGuid Owner(10u, 11u, 12u, 13u);
	const FGuid PreferredBackup(20u, 21u, 22u, 23u);
	const FGuid OtherBackup(30u, 31u, 32u, 33u);
	FGuLiWingmanRelayServer* Relay = Registry.CreateGroup(
		77u, Group, Owner, PreferredBackup, MakeConfig(Group), 0.0);
	if (!TestNotNull(TEXT("GameState-lifetime registry creates one core"), Relay)
		|| !Activate(*this, *Relay, Owner))
	{
		return false;
	}
	int32 WorldValidationCount = 0;
	TestTrue(TEXT("Ship installs one durable Candidate World validator"),
		Registry.SetCandidateWorldValidator(
			Group,
			[&WorldValidationCount](const FGuLiWingmanCandidateWorldValidationContext& Context)
			{
				++WorldValidationCount;
				return Context.IsWellFormed()
					? EGuLiWingmanRejectReason::None
					: EGuLiWingmanRejectReason::InvalidIdentity;
			}));
	const FGuLiCandidateWorldValidator* WorldValidator =
		Registry.FindCandidateWorldValidator(Group);
	if (!TestNotNull(TEXT("Registry exposes the installed World validator"), WorldValidator))
	{
		return false;
	}
	const FGuLiWingmanSubmissionResult Accepted = Relay->SubmitCandidate(
		Owner, MakeCandidate(*Relay), 0.1, FoundCarrier(), *WorldValidator);
	if (!TestEqual(TEXT("Pre-disconnect Candidate is accepted"),
		Accepted.Disposition, EGuLiWingmanSubmissionDisposition::Accepted))
	{
		return false;
	}
	const uint64 AcceptedHashBefore = Accepted.AcceptedBatch.StableHash;
	const uint64 AbilityHashBefore = Relay->GetAbilityConfig().SnapshotHash;
	const uint32 LeaseEpochBefore = Relay->GetLeaseState().LeaseEpoch;
	const int32 RosterCountBefore = Relay->GetRoster().Num();

	const TArray<FGuLiWingmanOwnerLossAssignment> Assignments = Registry.HandleOwnerDisconnected(
		Owner, {OtherBackup, PreferredBackup}, 0.2);
	TestEqual(TEXT("Exactly one owned group enters takeover"), Assignments.Num(), 1);
	TestEqual(TEXT("Configured connected backup wins deterministically"),
		Assignments[0].NewOwnerPlayerGuid, PreferredBackup);
	TestEqual(TEXT("Remaining candidate becomes next stable backup"),
		Assignments[0].NewBackupPlayerGuid, OtherBackup);
	TestEqual(TEXT("Disconnect starts a preview Offer without destroying the core"),
		Assignments[0].Disposition, EGuLiWingmanOwnerLossDisposition::OfferStarted);
	TestTrue(TEXT("Registry preserves the exact core allocation"), Registry.FindGroup(Group) == Relay);
	TestNotNull(TEXT("Controller EndPlay/takeover does not clear the Ship World validator"),
		Registry.FindCandidateWorldValidator(Group));
	TestEqual(TEXT("The pre-disconnect packet ran the durable validator"), WorldValidationCount, 1);
	TestEqual(TEXT("Roster survives transport destruction"), Relay->GetRoster().Num(), RosterCountBefore);
	TestEqual(TEXT("Accepted history survives transport destruction"),
		Relay->GetAcceptedHistory().Last().StableHash, AcceptedHashBefore);
	TestEqual(TEXT("Ability projection survives transport destruction"),
		Relay->GetAbilityConfig().SnapshotHash, AbilityHashBefore);
	TestEqual(TEXT("Offer preview does not change the active lease epoch"),
		Relay->GetLeaseState().LeaseEpoch, LeaseEpochBefore);
	TestEqual(TEXT("Offer preview does not replace the active owner"),
		Relay->GetLeaseState().OwnerPlayerGuid, Owner);
	FGuLiWingmanOwnerLossAssignment CommitAssignment;
	TestTrue(TEXT("Exact Ready commits the offered owner"),
		Registry.AcknowledgeLeaseOfferReady(
			Group, PreferredBackup, Assignments[0].OfferRevision, 0.21, CommitAssignment));
	TestTrue(TEXT("Old lease epoch is permanently invalidated at commit"),
		Relay->GetLeaseState().LeaseEpoch > LeaseEpochBefore);
	TestEqual(TEXT("Backup owns the committed new lease"),
		Relay->GetLeaseState().OwnerPlayerGuid, PreferredBackup);
	TestEqual(TEXT("Committed takeover remains Unavailable until exact ACK and atomic batch"),
		Relay->GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Unavailable);

	FGuLiWingmanBootstrapBundle TakeoverBootstrap;
	TestTrue(TEXT("New transport receives a complete takeover Bootstrap"),
		Relay->BuildBootstrap(TakeoverBootstrap));
	TestTrue(TEXT("Takeover Bootstrap carries a frozen baseline"),
		TakeoverBootstrap.bHasTransferBaseline);
	TestEqual(TEXT("Frozen ability revision is exact"),
		TakeoverBootstrap.TransferBaseline.AbilityConfigRevision,
		Relay->GetAbilityConfig().SnapshotRevision);
	TestEqual(TEXT("Frozen ability hash is exact"),
		TakeoverBootstrap.TransferBaseline.AbilityConfigHash, AbilityHashBefore);
	TestEqual(TEXT("Frozen Accepted hash is exact"),
		TakeoverBootstrap.TransferBaseline.AcceptedSnapshotHash,
		GuLiWingmanRelayHash::AcceptedSnapshot(TakeoverBootstrap.AcceptedSnapshot));
	TestEqual(TEXT("Authority still never writes Wingman movement"),
		Relay->GetServerWingmanMovementWriteCount(), static_cast<uint64>(0u));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanRelayRegistryWorldHookLifetimeTest,
	"GuLiStrike.Wingman.Relay.AuthorityRegistry.WorldHookRevokeAndDestroyLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanRelayRegistryWorldHookLifetimeTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanAuthorityRegistryTests;
	FGuLiWingmanRelayAuthorityRegistry Registry;
	const FGuid Owner(41u, 42u, 43u, 44u);
	const FGuLiWingmanGroupHandle RevokedGroup = MakeGroup(31u);
	TestNotNull(TEXT("Revoke fixture creates a group"), Registry.CreateGroup(
		12u, RevokedGroup, Owner, FGuid{}, MakeConfig(RevokedGroup), 0.0));
	TestTrue(TEXT("Revoke fixture stores a World hook"),
		Registry.SetCandidateWorldValidator(RevokedGroup, PermitWorld()));
	TestNotNull(TEXT("Stored hook is discoverable"),
		Registry.FindCandidateWorldValidator(RevokedGroup));
	TestTrue(TEXT("Registry-level revoke succeeds"), Registry.RevokeGroup(RevokedGroup, 0.1));
	TestNull(TEXT("Revoke clears the durable World hook"),
		Registry.FindCandidateWorldValidator(RevokedGroup));
	TestEqual(TEXT("Revoke preserves an explicit tombstone core"),
		Registry.FindGroup(RevokedGroup)->GetLeaseState().Lifecycle,
		EGuLiWingmanGroupLifecycle::Revoked);

	const FGuLiWingmanGroupHandle DestroyedGroup = MakeGroup(32u);
	TestNotNull(TEXT("Destroy fixture creates a group"), Registry.CreateGroup(
		13u, DestroyedGroup, Owner, FGuid{}, MakeConfig(DestroyedGroup), 0.2));
	TestTrue(TEXT("Destroy fixture stores a World hook"),
		Registry.SetCandidateWorldValidator(DestroyedGroup, PermitWorld()));
	TestTrue(TEXT("Final group destruction revokes then erases the entry"),
		Registry.DestroyGroup(DestroyedGroup, 0.3));
	TestNull(TEXT("Destroyed group has no core"), Registry.FindGroup(DestroyedGroup));
	TestNull(TEXT("Destroyed group has no World hook"),
		Registry.FindCandidateWorldValidator(DestroyedGroup));
	TestEqual(TEXT("Only the retained revoke tombstone remains"), Registry.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanRelayNoOwnerRetentionTest,
	"GuLiStrike.Wingman.Relay.AuthorityRegistry.NoOwnerThenReconnect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanRelayNoOwnerRetentionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanAuthorityRegistryTests;
	FGuLiWingmanRelayAuthorityRegistry Registry;
	const FGuLiWingmanGroupHandle Group = MakeGroup(2u);
	const FGuid Owner(101u, 102u, 103u, 104u);
	FGuLiWingmanRelayServer* Relay = Registry.CreateGroup(
		88u, Group, Owner, FGuid{}, MakeConfig(Group), 0.0);
	if (!TestNotNull(TEXT("Registry creates no-backup fixture"), Relay)
		|| !Activate(*this, *Relay, Owner))
	{
		return false;
	}
	const uint64 AbilityHashBefore = Relay->GetAbilityConfig().SnapshotHash;
	const TArray<FGuLiWingmanOwnerLossAssignment> OwnerLoss =
		Registry.HandleOwnerDisconnected(Owner, {}, 1.0);
	TestEqual(TEXT("One group is retained without a connected backup"), OwnerLoss.Num(), 1);
	TestEqual(TEXT("No-owner state is explicit"), OwnerLoss[0].Disposition,
		EGuLiWingmanOwnerLossDisposition::AwaitingConnectedOwner);
	TestEqual(TEXT("No-owner group is unavailable and cannot fight"),
		Relay->GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Unavailable);
	TestEqual(TEXT("No-owner retention keeps projected abilities"),
		Relay->GetAbilityConfig().SnapshotHash, AbilityHashBefore);
	TestEqual(TEXT("Registry exposes exactly one waiting group"),
		Registry.GetAwaitingOwnerGroups().Num(), 1);

	const FGuid ReconnectedBackup(201u, 202u, 203u, 204u);
	FGuLiWingmanOwnerLossAssignment Assignment;
	TestTrue(TEXT("A later eligible connection starts takeover"),
		Registry.AssignAwaitingGroup(Group, {ReconnectedBackup}, 2.0, Assignment));
	TestEqual(TEXT("Later connection receives a preview Offer"), Assignment.Disposition,
		EGuLiWingmanOwnerLossDisposition::OfferStarted);
	TestFalse(TEXT("Preview Offer does not own a new epoch yet"),
		Relay->GetLeaseState().OwnerPlayerGuid == ReconnectedBackup);
	FGuLiWingmanOwnerLossAssignment CommitAssignment;
	TestTrue(TEXT("Exact Ready commits the retained group"),
		Registry.AcknowledgeLeaseOfferReady(
			Group, ReconnectedBackup, Assignment.OfferRevision, 2.1, CommitAssignment));
	TestEqual(TEXT("Later connection owns the committed new epoch"),
		Relay->GetLeaseState().OwnerPlayerGuid, ReconnectedBackup);
	TestEqual(TEXT("Waiting marker remains until atomic activation"),
		Registry.GetAwaitingOwnerGroups().Num(), 1);
	TestTrue(TEXT("Retained ability projection is still byte-stable"),
		Relay->GetAbilityConfig().SnapshotHash == AbilityHashBefore);
	TestEqual(TEXT("No-owner/reconnect path has zero server movement writes"),
		Relay->GetServerWingmanMovementWriteCount(), static_cast<uint64>(0u));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanRelaySilentOwnerRecoveryTest,
	"GuLiStrike.Wingman.Relay.AuthorityRegistry.SilentOwnerEntersBackupRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanRelaySilentOwnerRecoveryTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanAuthorityRegistryTests;
	(void)Parameters;
	FGuLiWingmanRelayAuthorityRegistry Registry;
	const FGuLiWingmanGroupHandle Group = MakeGroup(3u);
	const FGuid Owner(301u, 302u, 303u, 304u);
	const FGuid Backup(401u, 402u, 403u, 404u);
	FGuLiWingmanRelayServer* Relay = Registry.CreateGroup(
		99u, Group, Owner, Backup, MakeConfig(Group), 0.0);
	if (!TestNotNull(TEXT("Silent-owner fixture creates a retained group"), Relay)
		|| !Activate(*this, *Relay, Owner))
	{
		return false;
	}
	const FGuLiWingmanSubmissionResult First = Relay->SubmitCandidate(
		Owner, MakeCandidate(*Relay), 0.1, FoundCarrier(), PermitWorld());
	if (!TestEqual(TEXT("Silent-owner fixture starts from an accepted pose"),
		First.Disposition, EGuLiWingmanSubmissionDisposition::Accepted))
	{
		return false;
	}
	const uint64 AcceptedHashBefore = GuLiWingmanRelayHash::AcceptedSnapshot(
		Relay->GetAcceptedHistory());
	Registry.RunLeaseMaintenance(1.0);
	Registry.RunLeaseMaintenance(2.0);
	TestEqual(TEXT("Candidate silence becomes Stale at the 1 Hz boundary"),
		Relay->GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Stale);
	Registry.RunLeaseMaintenance(3.0);
	TestEqual(TEXT("Candidate silence becomes Unavailable at the next boundary"),
		Relay->GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Unavailable);
	Registry.RunLeaseMaintenance(4.0);
	TestTrue(TEXT("Watchdog revocation retains a recoverable NoOwner group"),
		Relay->IsActiveLeaseRevoked()
			&& Relay->GetActiveLeaseTransaction().State
				== EGuLiWingmanActiveLeaseTransactionState::NoOwner);
	TestEqual(TEXT("The registry exposes exactly one unseeded lease-loss recovery"),
		Registry.GetRecoverableLeaseLossGroups().Num(), 1);

	FGuLiWingmanOwnerLossAssignment Offer;
	TestTrue(TEXT("A connected backup enters the normal reliable Offer rotation"),
		Registry.BeginLeaseLossRecovery(Group, {Backup}, 4.01, Offer));
	TestEqual(TEXT("Lease-loss recovery starts with an Offer, not an implicit commit"),
		Offer.Disposition, EGuLiWingmanOwnerLossDisposition::OfferStarted);
	TestEqual(TEXT("Offer preview preserves the old epoch until Ready"),
		Relay->GetLeaseState().OwnerPlayerGuid, Owner);
	TestEqual(TEXT("The accepted static snapshot survives the recovery preview"),
		GuLiWingmanRelayHash::AcceptedSnapshot(Relay->GetAcceptedHistory()),
		AcceptedHashBefore);
	TestTrue(TEXT("The group is now part of the normal awaiting-owner directory"),
		Registry.GetAwaitingOwnerGroups().Contains(Group));
	TestTrue(TEXT("A seeded recovery is no longer reported as unhandled"),
		Registry.GetRecoverableLeaseLossGroups().IsEmpty());
	TestEqual(TEXT("Recovery orchestration writes no server Wingman movement"),
		Relay->GetServerWingmanMovementWriteCount(), static_cast<uint64>(0u));
	return true;
}
#endif
