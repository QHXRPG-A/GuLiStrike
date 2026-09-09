// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Combat/GuLiWingmanReplenishmentController.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Misc/AutomationTest.h"

namespace GuLiWingmanLifecycleTests
{
	FGuLiWingmanGroupHandle MakeGroup()
	{
		FGuLiWingmanGroupHandle Group;
		Group.ShipInstanceId = FGuid(0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f001u);
		Group.ShipGeneration = 3u;
		Group.GroupGeneration = 9u;
		return Group;
	}

	FGuLiGroupAbilityConfigSnapshot MakeConfig(const FGuLiWingmanGroupHandle& Group)
	{
		FGuLiGroupAbilityConfigSnapshot Config;
		Config.ShipInstanceId = Group.ShipInstanceId;
		Config.MatchEpoch = 5u;
		Config.Team = EGuLiTeam::Red;
		Config.OwnerPlayerGuid = FGuid(1u, 2u, 3u, 4u);
		Config.WingmanTypeId = TEXT("LifecycleTestWingman");
		Config.ShipGeneration = Group.ShipGeneration;
		Config.GroupGeneration = Group.GroupGeneration;
		Config.AbilitySetRevision = 2u;
		Config.LoadoutRevision = 2u;
		Config.SnapshotRevision = 4u;
		Config.bGroupAbilitiesValid = true;
		Config.FormationAbilityId = TAG_GuLi_ShipAbility_Formation_DoubleRing;
		Config.BasicWeaponAbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
		Config.MissileAbilityId = TAG_GuLi_ShipAbility_Weapon_Missile_Salvo;
		Config.FormationDefinitionRevision = 1u;
		Config.FormationDefinitionChecksum = 0x101u;
		Config.BasicWeaponDefinitionRevision = 1u;
		Config.BasicWeaponDefinitionChecksum = 0x202u;
		Config.MissileDefinitionRevision = 1u;
		Config.MissileDefinitionChecksum = 0x303u;
		Config.FormationCommandRevision = 1u;
		Config.EffectiveClientSimTick = 1u;
		Config.RefreshHash();
		return Config;
	}

	bool ActivateRelay(FAutomationTestBase& Test, FGuLiWingmanRelayServer& Relay, const FGuid& Owner)
	{
		FGuLiWingmanBootstrapBundle Bootstrap;
		if (!Test.TestTrue(TEXT("Initial six-scope bootstrap builds"), Relay.BuildBootstrap(Bootstrap)))
		{
			return false;
		}
		FGuLiGroupAbilityConfigAck AbilityAck;
		AbilityAck.Group = Relay.GetLeaseState().Group;
		AbilityAck.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
		AbilityAck.SnapshotRevision = Relay.GetAbilityConfig().SnapshotRevision;
		AbilityAck.SnapshotHash = Relay.GetAbilityConfig().SnapshotHash;
		return Test.TestTrue(TEXT("Ability config is acknowledged"),
			Relay.AcknowledgeAbilityConfig(Owner, AbilityAck, 0.01))
			&& Test.TestTrue(TEXT("Initial bootstrap is acknowledged"),
				Relay.AcknowledgeBootstrap(Owner, Bootstrap.Commit, nullptr, 0.02))
			&& Test.TestTrue(TEXT("Group becomes Active"),
				Relay.GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Active);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanReplenishmentLifecycleTest,
	"GuLiStrike.Wingman.Combat.Lifecycle.AuthorityReplenishesStableSlotAfter15Seconds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanReplenishmentLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanLifecycleTests;
	const FGuLiTargetHandle SoldierTarget =
		GuLiCombatTargets::MakeCommanderSoldierTargetHandle(5u, 77u);
	TestTrue(TEXT("Commander Soldier target identity satisfies the non-Ship protocol"), SoldierTarget.IsValid());
	TestEqual(TEXT("Commander Soldier LocalId is the stable SoldierId"), SoldierTarget.LocalId, 77u);
	TestFalse(TEXT("A zero SoldierId cannot enter the target directory"),
		GuLiCombatTargets::MakeCommanderSoldierTargetHandle(5u, 0u).IsValid());

	const FGuid Owner(1u, 2u, 3u, 4u);
	FGuLiWingmanRelayServer Relay;
	const FGuLiWingmanGroupHandle Group = MakeGroup();
	if (!TestTrue(TEXT("Authority creates the deterministic group"),
		Relay.InitializeGroup(5u, Group, Owner, FGuid(5u, 6u, 7u, 8u), MakeConfig(Group), 0.0))
		|| !ActivateRelay(*this, Relay, Owner))
	{
		return false;
	}

	FGuLiWingmanReplenishmentController Controller;
	TArray<FGuLiWingmanReplenishmentResult> Replenished;
	bool bRosterChanged = false;
	Controller.Advance(Relay, 1.0, Replenished, bRosterChanged);
	TestTrue(TEXT("First observation establishes the stable roster directory"), bRosterChanged);
	TestTrue(TEXT("No living member is scheduled"), Replenished.IsEmpty());

	const int32 StableSlot = 2 * GULI_WINGMAN_MEMBERS_PER_FLIGHT + 3;
	const FGuLiWingmanHandle Previous = Relay.GetRoster()[StableSlot].Wingman;
	TestTrue(TEXT("The selected member dies through the authoritative lifecycle"), Relay.MarkWingmanDead(Previous));
	Controller.Advance(Relay, 2.0, Replenished, bRosterChanged);
	TestTrue(TEXT("Death changes the combat target directory"), bRosterChanged);
	TestEqual(TEXT("Exactly one dead stable slot is timed"), Controller.GetTrackedDeadSlotCount(), 1);
	TestTrue(TEXT("A member is not replenished before 15 seconds"), Replenished.IsEmpty());
	uint64 ScheduleId = 0u;
	double ReplenishAtSeconds = 0.0;
	bool bScheduleDue = true;
	TestTrue(TEXT("The production timer exposes the exact dead-identity schedule"),
		Controller.TryGetSchedule(Previous, ScheduleId, ReplenishAtSeconds, bScheduleDue));
	TestTrue(TEXT("The schedule identity is non-zero"), ScheduleId != 0u);
	TestEqual(TEXT("The schedule is exactly 15 seconds after first death observation"),
		ReplenishAtSeconds, 17.0);
	TestFalse(TEXT("The schedule is not due before its deadline"), bScheduleDue);

	Controller.Advance(Relay, 16.999, Replenished, bRosterChanged);
	TestTrue(TEXT("The full delay is enforced"), Replenished.IsEmpty());
	TestTrue(TEXT("The same schedule remains queryable before its deadline"),
		Controller.TryGetSchedule(Previous, ScheduleId, ReplenishAtSeconds, bScheduleDue));
	TestFalse(TEXT("The timer remains not due at 14.999 seconds"), bScheduleDue);
	Controller.Advance(Relay, 17.0, Replenished, bRosterChanged);
	if (!TestEqual(TEXT("The due slot replenishes exactly once"), Replenished.Num(), 1))
	{
		return false;
	}
	const FGuLiWingmanHandle Current = Replenished[0].ReplenishedWingman;
	TestTrue(TEXT("Replacement remains in the same group"), Current.Flight.Group == Previous.Flight.Group);
	TestEqual(TEXT("Replacement preserves Flight"), Current.Flight.FlightIndex, Previous.Flight.FlightIndex);
	TestEqual(TEXT("Replacement preserves member slot"), Current.MemberIndex, Previous.MemberIndex);
	TestEqual(TEXT("Replacement advances only EntityGeneration"),
		Current.EntityGeneration, Previous.EntityGeneration + 1u);
	TestFalse(TEXT("Replacement is alive"), Relay.GetRoster()[StableSlot].bDead);
	TestEqual(TEXT("Replacement returns at full lifecycle health"),
		Relay.GetHealth()[StableSlot].CurrentHealthPermille,
		Relay.GetHealth()[StableSlot].MaximumHealthPermille);

	const FGuLiTargetHandle PreviousTarget = GuLiCombatTargets::MakeWingmanTargetHandle(Previous);
	const FGuLiTargetHandle CurrentTarget = GuLiCombatTargets::MakeWingmanTargetHandle(Current);
	TestTrue(TEXT("Both old and replacement target identities satisfy the protocol"),
		PreviousTarget.IsValid() && CurrentTarget.IsValid());
	TestEqual(TEXT("Stable target LocalId preserves the roster slot"),
		CurrentTarget.LocalId, PreviousTarget.LocalId);
	TestTrue(TEXT("EntityGeneration invalidates the old target identity"), CurrentTarget != PreviousTarget);

	FGuLiWingmanBootstrapBundle ReplacementBootstrap;
	TestTrue(TEXT("Active roster mutation requests one reliable six-scope cut"),
		Relay.IsActiveRosterCutPending());
	TestTrue(TEXT("Replacement is published without entering Resume"),
		Relay.RefreshActiveRosterCut(17.0, ReplacementBootstrap));
	TestTrue(TEXT("Replacement bootstrap is complete"), ReplacementBootstrap.IsWellFormed());
	TestTrue(TEXT("The cut is explicitly an Active roster refresh"),
		ReplacementBootstrap.bActiveRosterRefresh);
	TestTrue(TEXT("Active refresh never asks for an all-Flight atomic batch"),
		!ReplacementBootstrap.bRequiresAtomicCandidateBatch);
	TestEqual(TEXT("Lifecycle stays Active while the owner acknowledges the roster cut"),
		Relay.GetLeaseState().Lifecycle, EGuLiWingmanGroupLifecycle::Active);
	TestTrue(TEXT("The reliable roster cut contains the new identity"),
		ReplacementBootstrap.Roster.ContainsByPredicate([&Current](const FGuLiWingmanRosterEntry& Entry)
		{
			return Entry.Wingman == Current && !Entry.bDead;
		}));
	FGuLiGroupAbilityConfigAck RefreshAbilityAck;
	RefreshAbilityAck.Group = Group;
	RefreshAbilityAck.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
	RefreshAbilityAck.SnapshotRevision = Relay.GetAbilityConfig().SnapshotRevision;
	RefreshAbilityAck.SnapshotHash = Relay.GetAbilityConfig().SnapshotHash;
	TestTrue(TEXT("Unchanged ability projection ACK is accepted in the roster-cut context"),
		Relay.AcknowledgeAbilityConfig(Owner, RefreshAbilityAck, 17.01));
	TestTrue(TEXT("Exact six-scope roster cut ACK releases the replacement identity"),
		Relay.AcknowledgeBootstrap(Owner, ReplacementBootstrap.Commit, nullptr, 17.02));
	TestFalse(TEXT("The Active roster ACK barrier is cleared"), Relay.IsActiveRosterCutPending());
	TestEqual(TEXT("Roster ACK does not leave Active"), Relay.GetLeaseState().Lifecycle,
		EGuLiWingmanGroupLifecycle::Active);
	return true;
}
#endif
