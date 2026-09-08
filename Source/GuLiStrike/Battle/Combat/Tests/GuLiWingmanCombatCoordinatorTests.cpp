// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Combat/GuLiWingmanCombatCoordinator.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Battle/Combat/GuLiLogicalMissileSubsystem.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySet.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySystemComponent.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

namespace GuLiWingmanCombatCoordinatorTests
{
	FGuLiTargetHandle MakeShipTarget(const uint32 Id)
	{
		FGuLiTargetHandle Handle;
		Handle.Kind = EGuLiTargetKind::Ship;
		Handle.AuthorityId = FGuid(0u, 0u, 0u, Id);
		Handle.Generation = 1u;
		return Handle;
	}

	struct FFixture
	{
		UWorld* World = nullptr;
		AActor* Ship = nullptr;
		AActor* Enemy = nullptr;
		AActor* FarEnemy = nullptr;
		AActor* Friendly = nullptr;
		UGuLiShipAbilitySystemComponent* ASC = nullptr;
		UGuLiShipAbilitySet* AbilitySet = nullptr;
		UGuLiDamageLedgerSubsystem* Ledger = nullptr;
		UGuLiLogicalMissileSubsystem* Missiles = nullptr;
		FGuLiWingmanRelayServer Relay;
		FGuLiWingmanCombatCoordinator Coordinator;
		FGuLiGroupAbilityConfigSnapshot Config;
		FGuLiWingmanAcceptedBatch Accepted;
		FGuLiTargetHandle ShipTarget;
		FGuLiTargetHandle EnemyTarget;
		FGuLiTargetHandle FarEnemyTarget;
		FGuLiTargetHandle FriendlyTarget;
		FGuid OwnerGuid = FGuid(1u, 2u, 3u, 4u);
		bool bLineOfSight = true;
		bool bWorldContextRegistered = false;

		~FFixture()
		{
			Coordinator.Reset();
			if (!World)
			{
				return;
			}
			World->DestroyWorld(false);
			if (bWorldContextRegistered && GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
		}

		UGuLiCombatHealthComponent* AddHealth(FAutomationTestBase& Test, AActor& Actor,
			const FGuLiTargetHandle& Handle, const EGuLiTeam Team)
		{
			UGuLiCombatHealthComponent* Health = NewObject<UGuLiCombatHealthComponent>(&Actor);
			Actor.AddInstanceComponent(Health);
			Health->RegisterComponent();
			if (!Test.TestTrue(TEXT("Combat target receives a stable identity"),
				Health->ConfigureServerTarget(Handle, Team))
				|| !Test.TestTrue(TEXT("Combat target receives custom server health"),
					Health->InitializeServerHealth(1000.0f, false)))
			{
				return nullptr;
			}
			return Health;
		}

		AActor* SpawnTargetActor(FAutomationTestBase& Test, const FGuLiTargetHandle& Handle,
			const EGuLiTeam Team, const FVector& Location)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			if (!Test.TestNotNull(TEXT("Combat target Actor exists"), Actor))
			{
				return nullptr;
			}
			USceneComponent* Root = NewObject<USceneComponent>(Actor);
			Actor->SetRootComponent(Root);
			Actor->AddInstanceComponent(Root);
			Root->RegisterComponent();
			Actor->SetActorLocation(Location);
			return AddHealth(Test, *Actor, Handle, Team) ? Actor : nullptr;
		}

		bool Initialize(FAutomationTestBase& Test, const bool bMultiChannel = false)
		{
			if (!Test.TestNotNull(TEXT("Engine exists for Wingman combat test"), GEngine))
			{
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("Isolated authority game World exists"), World))
			{
				return false;
			}
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bWorldContextRegistered = true;
			Ledger = World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
			Missiles = World->GetSubsystem<UGuLiLogicalMissileSubsystem>();
			if (!Test.TestNotNull(TEXT("Damage Ledger exists"), Ledger)
				|| !Test.TestNotNull(TEXT("Logical missile service exists"), Missiles)
				|| !Test.TestTrue(TEXT("Combat epoch starts"), Ledger->BeginServerEpoch(17u)))
			{
				return false;
			}

			ShipTarget = MakeShipTarget(10u);
			EnemyTarget = MakeShipTarget(20u);
			FarEnemyTarget = MakeShipTarget(21u);
			FriendlyTarget = MakeShipTarget(30u);
			Ship = SpawnTargetActor(Test, ShipTarget, EGuLiTeam::Red, FVector::ZeroVector);
			Enemy = SpawnTargetActor(Test, EnemyTarget, EGuLiTeam::Blue, FVector(50000.0, 0.0, 0.0));
			FarEnemy = SpawnTargetActor(Test, FarEnemyTarget, EGuLiTeam::Blue, FVector(75000.0, 0.0, 0.0));
			Friendly = SpawnTargetActor(Test, FriendlyTarget, EGuLiTeam::Red, FVector(50000.0, 0.0, 0.0));
			if (!Ship || !Enemy || !FarEnemy || !Friendly)
			{
				return false;
			}

			ASC = NewObject<UGuLiShipAbilitySystemComponent>(Ship, TEXT("WingmanCombatShipASC"));
			Ship->AddInstanceComponent(ASC);
			ASC->RegisterComponent();
			ASC->InitializeShipActorInfo(Ship);
			FGuLiShipAbilityProjectionContext Projection;
			Projection.ShipInstanceId = ShipTarget.AuthorityId;
			Projection.MatchEpoch = 17u;
			Projection.Team = EGuLiTeam::Red;
			Projection.OwnerPlayerGuid = OwnerGuid;
			Projection.WingmanTypeId = GuLiGetDefaultWingmanTypeId();
			Projection.ShipGeneration = ShipTarget.Generation;
			Projection.GroupGeneration = 1u;
			Projection.FormationCommandRevision = 1u;
			Projection.EffectiveClientSimTick = 100u;
			if (!Test.TestTrue(TEXT("Ship projection context is installed"), ASC->SetProjectionContext(Projection)))
			{
				return false;
			}
			AbilitySet = UGuLiShipAbilitySet::CreateNativeV1Transient(ASC);
			FGuLiShipAbilityLoadoutState AppliedLoadout = FGuLiShipAbilityLoadoutState::MakeNativeV1();
			if (bMultiChannel && AbilitySet)
			{
				const FGuLiShipAbilityGrant* NativeBasic =
					AbilitySet->FindGrant(TAG_GuLi_ShipAbility_Weapon_Basic_Auto);
				const FGuLiShipAbilityGrant* NativeMissile =
					AbilitySet->FindGrant(TAG_GuLi_ShipAbility_Weapon_Missile_Salvo);
				if (!Test.TestNotNull(TEXT("Native basic catalog entry exists"), NativeBasic)
					|| !Test.TestNotNull(TEXT("Native missile catalog entry exists"), NativeMissile))
				{
					return false;
				}
				const FGuLiShipAbilityGrant NativeBasicCopy = *NativeBasic;
				const FGuLiShipAbilityGrant NativeMissileCopy = *NativeMissile;

				UGuLiWingmanWeaponDefinition* SecondaryBasicDefinition =
					DuplicateObject<UGuLiWingmanWeaponDefinition>(NativeBasicCopy.WeaponDefinition.Get(), AbilitySet);
				UGuLiWingmanWeaponDefinition* SecondaryMissileDefinition =
					DuplicateObject<UGuLiWingmanWeaponDefinition>(NativeMissileCopy.WeaponDefinition.Get(), AbilitySet);
				UGuLiWingmanWeaponDefinition* IndependentMissileDefinition =
					DuplicateObject<UGuLiWingmanWeaponDefinition>(NativeMissileCopy.WeaponDefinition.Get(), AbilitySet);
				if (!Test.TestNotNull(TEXT("Secondary automatic definition duplicates"), SecondaryBasicDefinition)
					|| !Test.TestNotNull(TEXT("Shared-group missile definition duplicates"), SecondaryMissileDefinition)
					|| !Test.TestNotNull(TEXT("Independent missile definition duplicates"), IndependentMissileDefinition))
				{
					return false;
				}
				SecondaryBasicDefinition->Revision = 2u;
				SecondaryBasicDefinition->Damage = 20.0f;
				SecondaryBasicDefinition->CooldownSeconds = 3.0f;
				SecondaryMissileDefinition->Revision = 2u;
				SecondaryMissileDefinition->Damage = 120.0f;
				IndependentMissileDefinition->Revision = 3u;
				IndependentMissileDefinition->Damage = 140.0f;

				FGuLiShipAbilityGrant SecondaryBasic = NativeBasicCopy;
				SecondaryBasic.AbilityId = TAG_GuLi_ShipWingman_Weapon_Basic;
				SecondaryBasic.WeaponSlotId = TEXT("SecondaryWeapon");
				SecondaryBasic.SkillId = TEXT("Test.Secondary.Auto");
				SecondaryBasic.ProfileRevision = 2u;
				SecondaryBasic.WeaponDefinition = SecondaryBasicDefinition;
				AbilitySet->Grants.Add(SecondaryBasic);

				FGuLiShipAbilityGrant SecondaryMissile = NativeMissileCopy;
				SecondaryMissile.AbilityId = TAG_GuLi_ShipWingman_Weapon_Missile;
				SecondaryMissile.WeaponSlotId = TEXT("SecondaryMissile");
				SecondaryMissile.SkillId = TEXT("Test.Secondary.Missile");
				SecondaryMissile.ProfileRevision = 2u;
				SecondaryMissile.CooldownGroupId = NativeMissileCopy.GetEffectiveCooldownGroupId();
				SecondaryMissile.WeaponDefinition = SecondaryMissileDefinition;
				AbilitySet->Grants.Add(SecondaryMissile);

				FGuLiShipAbilityGrant IndependentMissile = NativeMissileCopy;
				IndependentMissile.AbilityId = TAG_GuLi_ShipAbility_Reticle_Omni;
				IndependentMissile.WeaponSlotId = TEXT("IndependentMissile");
				IndependentMissile.SkillId = TEXT("Test.Independent.Missile");
				IndependentMissile.ProfileRevision = 3u;
				IndependentMissile.CooldownGroupId = TEXT("IndependentTestSalvo");
				IndependentMissile.WeaponDefinition = IndependentMissileDefinition;
				AbilitySet->Grants.Add(IndependentMissile);

				AppliedLoadout.AbilityIds.Add(SecondaryBasic.AbilityId);
				AppliedLoadout.AbilityIds.Add(SecondaryMissile.AbilityId);
				AppliedLoadout.AbilityIds.Add(IndependentMissile.AbilityId);
				AppliedLoadout.Revision = 2u;
				AppliedLoadout.Normalize();
			}
			FString Error;
			if (!Test.TestNotNull(TEXT("Native Wingman ability definitions exist"), AbilitySet)
				|| !Test.TestTrue(TEXT("Ship grants the selected Wingman abilities"),
					ASC->ServerApplyAbilitySet(AbilitySet, AppliedLoadout, Error)))
			{
				Test.AddError(Error);
				return false;
			}
			// Exercise a real previous nonzero revision. Revision zero is malformed by protocol and cannot
			// represent an older acknowledged loadout.
			ASC->ServerClearShipAbilities();
			if (!Test.TestTrue(TEXT("A replacement Ship grant produces a prior nonzero revision"),
				ASC->ServerApplyAbilitySet(AbilitySet, AppliedLoadout, Error))
				|| !Test.TestTrue(TEXT("Ship publishes a complete ability config"),
					ASC->BuildGroupAbilityConfigSnapshot(Config)))
			{
				Test.AddError(Error);
				return false;
			}

			FGuLiWingmanGroupHandle Group;
			Group.ShipInstanceId = ShipTarget.AuthorityId;
			Group.ShipGeneration = ShipTarget.Generation;
			Group.GroupGeneration = 1u;
			if (!Test.TestTrue(TEXT("Relay initializes a 25-member group"), Relay.InitializeGroup(
				17u, Group, OwnerGuid, FGuid(5u, 6u, 7u, 8u), Config, 0.0)))
			{
				return false;
			}
			FGuLiWingmanBootstrapBundle Bootstrap;
			if (!Test.TestTrue(TEXT("Relay builds six-scope bootstrap"), Relay.BuildBootstrap(Bootstrap)))
			{
				return false;
			}
			FGuLiGroupAbilityConfigAck Ack;
			Ack.Group = Group;
			Ack.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
			Ack.SnapshotRevision = Config.SnapshotRevision;
			Ack.SnapshotHash = Config.SnapshotHash;
			if (!Test.TestTrue(TEXT("Owner confirms ability config"), Relay.AcknowledgeAbilityConfig(OwnerGuid, Ack, 0.01))
				|| !Test.TestTrue(TEXT("Owner commits atomic bootstrap"),
					Relay.AcknowledgeBootstrap(OwnerGuid, Bootstrap.Commit, nullptr, 0.02)))
			{
				return false;
			}

			FGuLiWingmanCandidateBatch Candidate;
			Candidate.MatchEpoch = 17u;
			Candidate.Group = Group;
			Candidate.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
			Candidate.CandidateSequence = 1u;
			Candidate.ClientSimTick = 100u;
			Candidate.CarrierSource.CanonicalEpoch = 1u;
			Candidate.CarrierSource.MoveRevision = 1u;
			Candidate.AbilitySetRevision = Config.AbilitySetRevision;
			Candidate.FormationCommandRevision = Config.FormationCommandRevision;
			Candidate.FormationDefinitionChecksum = Config.FormationDefinitionChecksum;
			for (const FGuLiWingmanRosterEntry& Roster : Relay.GetRoster())
			{
				FGuLiWingmanCandidateSample& Sample = Candidate.Samples.AddDefaulted_GetRef();
				Sample.Wingman = Roster.Wingman;
				Sample.PositionCentimeters = FIntVector(0,
					static_cast<int32>(Roster.Wingman.Flight.FlightIndex) * 1000
						+ static_cast<int32>(Roster.Wingman.MemberIndex) * 100, 0);
				Sample.VelocityCentimetersPerSecond = FIntVector(4500, 0, 0);
				Sample.RotationCentiDegrees = FIntVector::ZeroValue;
				Sample.FlightMode = 1u;
			}
			const FGuLiCarrierSourceResolver Carrier = [](const FGuLiCarrierSourceRef&, FGuLiRelayCarrierState& Out)
			{
				Out.Transform = FTransform::Identity;
				Out.ServerWorldTimeSeconds = 0.1;
				return EGuLiRelayCarrierLookupResult::Found;
			};
			const FGuLiCandidateWorldValidator PermitWellFormedWorld = [](
				const FGuLiWingmanCandidateWorldValidationContext& WorldContext)
			{
				return WorldContext.IsWellFormed()
					? EGuLiWingmanRejectReason::None
					: EGuLiWingmanRejectReason::InvalidIdentity;
			};
			const FGuLiWingmanSubmissionResult CandidateResult = Relay.SubmitCandidate(
				OwnerGuid, Candidate, 0.1, Carrier, PermitWellFormedWorld);
			if (!Test.TestTrue(TEXT("Synthetic owner poses become the fresh Accepted batch"),
				CandidateResult.Disposition == EGuLiWingmanSubmissionDisposition::Accepted))
			{
				return false;
			}
			Accepted = CandidateResult.AcceptedBatch;

			FGuLiWingmanCombatContext CombatContext;
			CombatContext.MatchEpoch = 17u;
			CombatContext.ShipSource = ShipTarget;
			CombatContext.ShipTeam = EGuLiTeam::Red;
			CombatContext.ShipASC = ASC;
			CombatContext.Relay = &Relay;
			CombatContext.DamageLedger = Ledger;
			CombatContext.LogicalMissiles = Missiles;
			CombatContext.LineOfSightResolver = [this](const FVector&, const FGuLiCombatTargetSnapshot&)
			{
				return bLineOfSight;
			};
			CombatContext.ServerTimeProvider = [] { return 0.15; };
			return Test.TestTrue(TEXT("Ship-owned combat coordinator initializes"),
				Coordinator.Initialize(CombatContext, &Error));
		}

		FGuLiWingmanFireIntent BasicIntent(const FGuLiTargetHandle& Target, const uint32 Sequence = 1u) const
		{
			const FGuLiWingmanWeaponChannelConfig* Channel =
				Config.FindFirstWeaponChannel(EGuLiWingmanWeaponKind::BasicAutomatic);
			return Channel ? WeaponIntent(*Channel, Accepted.Samples[0].Wingman, Target, Sequence)
				: FGuLiWingmanFireIntent{};
		}

		const FGuLiWingmanWeaponChannelConfig* ChannelBySlot(const FName SlotId) const
		{
			return Config.WeaponChannels.FindByPredicate([SlotId](const FGuLiWingmanWeaponChannelConfig& Channel)
			{
				return Channel.Binding.SlotId == SlotId;
			});
		}

		FGuLiWingmanFireIntent WeaponIntent(
			const FGuLiWingmanWeaponChannelConfig& Channel,
			const FGuLiWingmanHandle& Emitter,
			const FGuLiTargetHandle& Target,
			const uint32 Sequence) const
		{
			FGuLiWingmanFireIntent Intent;
			Intent.MatchEpoch = 17u;
			Intent.Group = Relay.GetLeaseState().Group;
			Intent.LeaseEpoch = Relay.GetLeaseState().LeaseEpoch;
			Intent.DomainFireSequence = Sequence;
			Intent.Emitter = Emitter;
			Intent.Binding = Channel.Binding;
			Intent.SourceAcceptedState = Accepted.StateRef;
			Intent.ClientFireTick = Accepted.StateRef.ClientSimTick + 1u;
			Intent.Target = Target;
			Intent.WeaponAbilityId = Channel.AbilityId;
			Intent.SkillId = Channel.SkillId;
			Intent.LoadoutRevision = Config.LoadoutRevision;
			Intent.ProfileRevision = Channel.ProfileRevision;
			Intent.WeaponDefinitionRevision = Channel.DefinitionRevision;
			Intent.AbilitySetRevision = Config.AbilitySetRevision;
			Intent.AimDirectionMilli = FIntVector(1000, 0, 0);
			Intent.bClientPredictedLineOfSight = true;
			return Intent;
		}

		FGuLiWingmanMissileSalvoRequest MissileRequest(
			const uint32 Id,
			const FVector& AimForward = FVector::ForwardVector) const
		{
			const FGuLiWingmanWeaponChannelConfig* Channel =
				Config.FindFirstWeaponChannel(EGuLiWingmanWeaponKind::Missile);
			return Channel ? MissileRequestForChannel(*Channel, Id, AimForward)
				: FGuLiWingmanMissileSalvoRequest{};
		}

		FGuLiWingmanMissileSalvoRequest MissileRequestForChannel(
			const FGuLiWingmanWeaponChannelConfig& Channel,
			const uint32 Id,
			const FVector& AimForward = FVector::ForwardVector) const
		{
			FGuLiWingmanMissileSalvoRequest Request;
			Request.ActivationId = FGuid(0u, 0u, 100u, Id);
			Request.Binding = Channel.Binding;
			Request.SkillId = Channel.SkillId;
			Request.MissileAbilityId = Channel.AbilityId;
			Request.AbilitySetRevision = Config.AbilitySetRevision;
			Request.LoadoutRevision = Config.LoadoutRevision;
			Request.ProfileRevision = Channel.ProfileRevision;
			Request.MissileDefinitionRevision = Channel.DefinitionRevision;
			GuLiWingmanMissileAim::Quantize(AimForward, Request.AimDirectionMilli);
			return Request;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanBasicWeaponCoordinatorTest,
	"GuLiStrike.Wingman.Combat.BasicWeapon.ValidationCooldownAndLedgerIdempotency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanBasicWeaponCoordinatorTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanCombatCoordinatorTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	const FGuLiWingmanFireIntent Valid = Fixture.BasicIntent(Fixture.EnemyTarget);
	const FGuLiWingmanAcceptedBatch& Accepted = Fixture.Accepted;

	const FGuLiTargetHandle MissingTarget = MakeShipTarget(99u);
	TestTrue(TEXT("Unregistered targets are rejected precisely"),
		Fixture.Coordinator.ValidateBasicFireIntent(Fixture.BasicIntent(MissingTarget), Accepted, 0.12)
			== EGuLiWingmanRejectReason::InvalidTarget);
	TestTrue(TEXT("Friendly targets are rejected before damage"),
		Fixture.Coordinator.ValidateBasicFireIntent(Fixture.BasicIntent(Fixture.FriendlyTarget), Accepted, 0.12)
			== EGuLiWingmanRejectReason::FriendlyTarget);

	Fixture.Enemy->SetActorLocation(FVector(151000.0, 0.0, 0.0));
	TestTrue(TEXT("Basic weapon range is enforced from the accepted emitter pose"),
		Fixture.Coordinator.ValidateBasicFireIntent(Valid, Accepted, 0.12)
			== EGuLiWingmanRejectReason::OutOfRange);
	Fixture.Enemy->SetActorLocation(FVector(50000.0, 0.0, 0.0));
	Fixture.bLineOfSight = false;
	TestTrue(TEXT("Occluded basic fire is rejected"),
		Fixture.Coordinator.ValidateBasicFireIntent(Valid, Accepted, 0.12)
			== EGuLiWingmanRejectReason::NoLineOfSight);
	Fixture.bLineOfSight = true;
	FGuLiWingmanFireIntent OldVersion = Valid;
	--OldVersion.AbilitySetRevision;
	const FGuLiWingmanBasicFireResult OldVersionResult = Fixture.Coordinator.SubmitBasicFireIntent(
		Fixture.OwnerGuid, OldVersion, 0.12);
	TestTrue(TEXT("Old ability revisions never reach the damage ledger"),
		OldVersionResult.RelayResult.RejectReason == EGuLiWingmanRejectReason::StaleAbilitySetRevision);
	TestEqual(TEXT("Old ability revisions do not advance damage commits"),
		Fixture.Ledger->GetCommitCount(), static_cast<uint64>(0u));

	const uint64 CommitCountBefore = Fixture.Ledger->GetCommitCount();
	const FGuLiWingmanBasicFireResult First = Fixture.Coordinator.SubmitBasicFireIntent(
		Fixture.OwnerGuid, Valid, 0.12);
	TestTrue(TEXT("A fully valid basic intent commits"), First.WasCommitted());
	TestEqual(TEXT("One accepted basic intent creates one ledger commit"),
		Fixture.Ledger->GetCommitCount(), CommitCountBefore + 1u);
	FGuLiCombatTargetSnapshot TargetAfterFirst;
	Fixture.Ledger->TryGetTargetSnapshot(Fixture.EnemyTarget, TargetAfterFirst);
	const float HealthAfterFirst = TargetAfterFirst.Health;

	const FGuLiWingmanBasicFireResult Replayed = Fixture.Coordinator.CommitAcceptedBasicFireIntent(Valid, 0.13);
	TestTrue(TEXT("Replayed accepted callback resolves as an idempotent ledger duplicate"),
		Replayed.DamageResult.Status == EGuLiDamageCommitStatus::Duplicate);
	FGuLiCombatTargetSnapshot TargetAfterReplay;
	Fixture.Ledger->TryGetTargetSnapshot(Fixture.EnemyTarget, TargetAfterReplay);
	TestEqual(TEXT("Duplicate callback applies no second damage"), TargetAfterReplay.Health, HealthAfterFirst);
	TestEqual(TEXT("Duplicate callback does not increment ledger commit count"),
		Fixture.Ledger->GetCommitCount(), CommitCountBefore + 1u);

	const FGuLiWingmanBasicFireResult Cooldown = Fixture.Coordinator.SubmitBasicFireIntent(
		Fixture.OwnerGuid, Fixture.BasicIntent(Fixture.EnemyTarget, 2u), 0.13);
	TestTrue(TEXT("Each emitter owns an independent basic-weapon cooldown"),
		Cooldown.RelayResult.RejectReason == EGuLiWingmanRejectReason::CooldownActive);
	FGuLiWingmanFireIntent OtherEmitter = Fixture.BasicIntent(Fixture.EnemyTarget, 1u);
	OtherEmitter.Emitter = Fixture.Accepted.Samples[1].Wingman;
	const FGuLiWingmanBasicFireResult Independent = Fixture.Coordinator.SubmitBasicFireIntent(
		Fixture.OwnerGuid, OtherEmitter, 0.13);
	TestTrue(TEXT("A second emitter can fire while the first emitter is cooling down"),
		Independent.WasCommitted());
	TestTrue(TEXT("The first emitter can die after its shot"), Fixture.Relay.MarkWingmanDead(Valid.Emitter));
	TestEqual(TEXT("Roster synchronization removes only dead/replaced emitter cooldown state"),
		Fixture.Coordinator.SynchronizeRosterState(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanMissileCoordinatorTest,
	"GuLiStrike.Wingman.Combat.Missile.StableFlightSharedCooldownAndNoActors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanMissileCoordinatorTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanCombatCoordinatorTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	const FGuLiWingmanMissileSalvoRequest NoTarget = Fixture.MissileRequest(1u, FVector::BackwardVector);
	TestTrue(TEXT("No target in the camera cone fails without starting shared cooldown"),
		Fixture.Coordinator.ActivateMissileSalvo(NoTarget, 0.12).RejectReason
			== EGuLiWingmanRejectReason::InvalidTarget);
	TestFalse(TEXT("Failed lock does not consume missile cooldown"), Fixture.ASC->IsMissileCooldownActive());
	TestTrue(TEXT("A terminal failed activation identity is cached"),
		Fixture.Coordinator.ActivateMissileSalvo(NoTarget, 0.13).RejectReason
			== EGuLiWingmanRejectReason::Duplicate);
	TestEqual(TEXT("Retrying a failed identity still creates no missile"),
		Fixture.Missiles->GetActiveMissileCount(), 0);

	Fixture.Enemy->SetActorLocation(FVector(-50000.0, 0.0, 0.0));
	Fixture.FarEnemy->SetActorLocation(FVector(-75000.0, 0.0, 0.0));
	TestTrue(TEXT("Friendly missile lock is rejected"),
		Fixture.Coordinator.ActivateMissileSalvo(Fixture.MissileRequest(2u), 0.12).RejectReason
			== EGuLiWingmanRejectReason::FriendlyTarget);

	Fixture.Friendly->SetActorLocation(FVector(-50000.0, 0.0, 0.0));
	Fixture.Enemy->SetActorLocation(FVector(300000.0, 0.0, 0.0));
	Fixture.FarEnemy->SetActorLocation(FVector(350000.0, 0.0, 0.0));
	TestTrue(TEXT("Ship lock range is enforced"),
		Fixture.Coordinator.ActivateMissileSalvo(Fixture.MissileRequest(3u), 0.12).RejectReason
			== EGuLiWingmanRejectReason::OutOfRange);
	Fixture.Enemy->SetActorLocation(FVector(50000.0, 0.0, 0.0));
	Fixture.FarEnemy->SetActorLocation(FVector(75000.0, 0.0, 0.0));
	Fixture.bLineOfSight = false;
	TestTrue(TEXT("Ship lock LOS is enforced"),
		Fixture.Coordinator.ActivateMissileSalvo(Fixture.MissileRequest(4u), 0.12).RejectReason
			== EGuLiWingmanRejectReason::NoLineOfSight);
	Fixture.bLineOfSight = true;
	FGuLiWingmanMissileSalvoRequest OldVersion = Fixture.MissileRequest(5u);
	--OldVersion.AbilitySetRevision;
	TestTrue(TEXT("Old missile ability revisions are rejected"),
		Fixture.Coordinator.ActivateMissileSalvo(OldVersion, 0.12).RejectReason
			== EGuLiWingmanRejectReason::StaleAbilitySetRevision);
	TestFalse(TEXT("Old missile revisions do not start shared cooldown"), Fixture.ASC->IsMissileCooldownActive());
	TestEqual(TEXT("Old missile revisions create no logical missile"),
		Fixture.Missiles->GetActiveMissileCount(), 0);

	TestTrue(TEXT("First Flight member zero can die before the salvo"),
		Fixture.Relay.MarkWingmanDead(Fixture.Accepted.Samples[0].Wingman));
	TestTrue(TEXT("First Flight member one can die before the salvo"),
		Fixture.Relay.MarkWingmanDead(Fixture.Accepted.Samples[1].Wingman));
	const FGuLiWingmanMissileSalvoRequest Valid = Fixture.MissileRequest(6u);
	const FGuLiWingmanMissileSalvoResult Salvo = Fixture.Coordinator.ActivateMissileSalvo(Valid, 0.12);
	TestTrue(TEXT("A legal partial Flight launches"), Salvo.WasLaunched());
	TestTrue(TEXT("Authority selects the nearest legal target without a client target handle"),
		Salvo.SelectedTarget == Fixture.EnemyTarget);
	TestEqual(TEXT("Stable lowest legal Flight is selected"), Salvo.FlightIndex, static_cast<uint8>(0u));
	TestEqual(TEXT("Only the three living Flight members launch"), Salvo.LaunchedCount, 3);
	TestTrue(TEXT("A Flight salvo can never exceed five logical missiles"),
		Salvo.LaunchedCount <= static_cast<int32>(GULI_WINGMAN_MEMBERS_PER_FLIGHT));
	TestEqual(TEXT("No replicated missile Actor is created; only logical records exist"),
		Fixture.Missiles->GetActiveMissileCount(), 3);
	TestTrue(TEXT("At least one launch starts the Ship ASC shared cooldown"), Fixture.ASC->IsMissileCooldownActive());

	const int32 ActiveBeforeDuplicate = Fixture.Missiles->GetActiveMissileCount();
	const FGuLiWingmanMissileSalvoResult Duplicate = Fixture.Coordinator.ActivateMissileSalvo(Valid, 0.13);
	TestTrue(TEXT("Repeated GA activation identity is idempotently rejected"),
		Duplicate.RejectReason == EGuLiWingmanRejectReason::Duplicate);
	TestEqual(TEXT("Duplicate activation creates no missiles"),
		Fixture.Missiles->GetActiveMissileCount(), ActiveBeforeDuplicate);
	const FGuLiWingmanMissileSalvoResult SharedCooldown = Fixture.Coordinator.ActivateMissileSalvo(
		Fixture.MissileRequest(7u), 0.13);
	TestTrue(TEXT("A different activation still observes one Ship-level shared cooldown"),
		SharedCooldown.RejectReason == EGuLiWingmanRejectReason::CooldownActive);
	TestEqual(TEXT("A failed cooldown reservation creates no partial salvo"),
		Fixture.Missiles->GetActiveMissileCount(), ActiveBeforeDuplicate);

	for (int32 SampleIndex = 2; SampleIndex < GULI_WINGMAN_MEMBERS_PER_FLIGHT; ++SampleIndex)
	{
		TestTrue(TEXT("A launched missile survives its emitter's later death"),
			Fixture.Relay.MarkWingmanDead(Fixture.Accepted.Samples[SampleIndex].Wingman));
	}
	const FGuLiCarrierSourceResolver FoundCarrier = [](const FGuLiCarrierSourceRef&, FGuLiRelayCarrierState& Out)
	{
		Out.Transform = FTransform::Identity;
		Out.ServerWorldTimeSeconds = 1.11;
		return EGuLiRelayCarrierLookupResult::Found;
	};
	// Freshness starts at the accepted Candidate time (0.1), so advance strictly
	// beyond the one-second Active -> Stale threshold.
	Fixture.Relay.AdvanceTime(1.11, FoundCarrier);
	TestTrue(TEXT("The source group can become Stale after launch"),
		Fixture.Relay.GetLeaseState().Lifecycle == EGuLiWingmanGroupLifecycle::Stale);
	for (int32 Step = 0; Step < 60 && Fixture.Missiles->GetActiveMissileCount() > 0; ++Step)
	{
		Fixture.Missiles->Tick(1.0f / 30.0f);
	}
	TestEqual(TEXT("Accepted missiles finish after emitter death and source-group Stale"),
		Fixture.Missiles->GetActiveMissileCount(), 0);
	TestEqual(TEXT("Each of the three logical impacts commits exactly once"),
		Fixture.Ledger->GetCommitCount(), static_cast<uint64>(3u));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanMissileAimQuantizationTest,
	"GuLiStrike.Wingman.Combat.Missile.CameraAimQuantization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanMissileAimQuantizationTest::RunTest(const FString& Parameters)
{
	FIntVector Quantized;
	const FVector Input = FVector(0.3, -0.4, 0.5).GetSafeNormal();
	TestTrue(TEXT("A finite camera forward vector quantizes"),
		GuLiWingmanMissileAim::Quantize(Input, Quantized));
	FVector Decoded;
	TestTrue(TEXT("The canonical milli-vector decodes"),
		GuLiWingmanMissileAim::Decode(Quantized, Decoded));
	TestTrue(TEXT("Quantization preserves the reticle direction"),
		FVector::DotProduct(Input, Decoded) > 0.99999);
	TestFalse(TEXT("A zero aim cannot authorize a missile request"),
		GuLiWingmanMissileAim::Decode(FIntVector::ZeroValue, Decoded));
	TestFalse(TEXT("An out-of-domain aim cannot authorize a missile request"),
		GuLiWingmanMissileAim::Decode(FIntVector(1001, 1001, 1001), Decoded));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanMultiChannelCooldownAndProvenanceTest,
	"GuLiStrike.Wingman.Combat.MultiChannelCooldownAndProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanMultiChannelCooldownAndProvenanceTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanCombatCoordinatorTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this, true))
	{
		return false;
	}

	const FGuLiWingmanWeaponChannelConfig* PrimaryBasic = Fixture.ChannelBySlot(TEXT("BasicWeapon"));
	const FGuLiWingmanWeaponChannelConfig* SecondaryBasic = Fixture.ChannelBySlot(TEXT("SecondaryWeapon"));
	const FGuLiWingmanWeaponChannelConfig* PrimaryMissile = Fixture.ChannelBySlot(TEXT("Missile"));
	const FGuLiWingmanWeaponChannelConfig* SecondaryMissile = Fixture.ChannelBySlot(TEXT("SecondaryMissile"));
	const FGuLiWingmanWeaponChannelConfig* IndependentMissile = Fixture.ChannelBySlot(TEXT("IndependentMissile"));
	if (!TestNotNull(TEXT("Primary automatic channel exists"), PrimaryBasic)
		|| !TestNotNull(TEXT("Secondary automatic channel exists"), SecondaryBasic)
		|| !TestNotNull(TEXT("Primary missile channel exists"), PrimaryMissile)
		|| !TestNotNull(TEXT("Shared-group missile channel exists"), SecondaryMissile)
		|| !TestNotNull(TEXT("Independent missile channel exists"), IndependentMissile))
	{
		return false;
	}
	TestTrue(TEXT("Two active channels explicitly share one cooldown group"),
		PrimaryMissile->CooldownGroupId == SecondaryMissile->CooldownGroupId);
	TestTrue(TEXT("Independent active channel has a distinct cooldown group"),
		PrimaryMissile->CooldownGroupId != IndependentMissile->CooldownGroupId);

	UGuLiCombatHealthComponent* EnemyHealth =
		Fixture.Enemy->FindComponentByClass<UGuLiCombatHealthComponent>();
	if (!TestNotNull(TEXT("Enemy health component remains registered"), EnemyHealth)
		|| !TestTrue(TEXT("Direct-damage provenance fixture resets enemy health"),
			EnemyHealth->InitializeServerHealth(30.0f, false)))
	{
		return false;
	}
	const FGuLiWingmanHandle Emitter = Fixture.Accepted.Samples[0].Wingman;
	const FGuLiWingmanBasicFireResult First = Fixture.Coordinator.SubmitBasicFireIntent(
		Fixture.OwnerGuid,
		Fixture.WeaponIntent(*PrimaryBasic, Emitter, Fixture.EnemyTarget, 1u), 0.12);
	TestTrue(TEXT("Primary automatic slot commits"), First.WasCommitted());
	TestEqual(TEXT("Primary slot uses only its committed runtime damage"),
		First.DamageResult.AppliedDamage, PrimaryBasic->Runtime.Damage);

	const FGuLiWingmanBasicFireResult Second = Fixture.Coordinator.SubmitBasicFireIntent(
		Fixture.OwnerGuid,
		Fixture.WeaponIntent(*SecondaryBasic, Emitter, Fixture.EnemyTarget, 2u), 0.12);
	TestTrue(TEXT("Same member may commit a different automatic slot at the same time"),
		Second.WasCommitted());
	TestEqual(TEXT("Secondary slot uses only its committed profile damage"),
		Second.DamageResult.AppliedDamage, SecondaryBasic->Runtime.Damage);
	TestTrue(TEXT("Secondary slot produces the lethal event"), Second.DamageResult.bKilled);

	const FGuLiWingmanBasicFireResult PrimaryCooldown = Fixture.Coordinator.SubmitBasicFireIntent(
		Fixture.OwnerGuid,
		Fixture.WeaponIntent(*PrimaryBasic, Emitter, Fixture.FarEnemyTarget, 3u), 0.13);
	TestTrue(TEXT("Only the reused primary slot observes its own cooldown"),
		PrimaryCooldown.RelayResult.RejectReason == EGuLiWingmanRejectReason::CooldownActive);

	FGuLiDeathCommitRecord Death;
	if (!TestTrue(TEXT("Lethal direct damage records one authoritative death provenance row"),
		Fixture.Ledger->TryGetDeathRecord(Second.DamageResult.DeathEventId, Death)))
	{
		return false;
	}
	TestTrue(TEXT("Death provenance carries the exact weapon binding"),
		Death.WeaponBinding == SecondaryBasic->Binding);
	TestEqual(TEXT("Death provenance carries the exact SkillId"), Death.SkillId, SecondaryBasic->SkillId);
	TestEqual(TEXT("Death provenance carries the committed LoadoutRevision"),
		Death.LoadoutRevision, Fixture.Config.LoadoutRevision);
	TestEqual(TEXT("Death provenance carries the committed ProfileRevision"),
		Death.ProfileRevision, SecondaryBasic->ProfileRevision);
	TestEqual(TEXT("Direct damage roots the death in its stable shot"), Death.RootEventId, Second.ShotId);

	TArray<FGuLiLogicalMissileState> LaunchedStates;
	const FDelegateHandle LaunchHandle = Fixture.Missiles->OnLaunch.AddLambda(
		[&LaunchedStates](const FGuLiLogicalMissileState& State)
		{
			LaunchedStates.Add(State);
		});
	const FGuLiWingmanMissileSalvoRequest PrimaryRequest =
		Fixture.MissileRequestForChannel(*PrimaryMissile, 100u);
	const FGuLiWingmanMissileSalvoResult PrimarySalvo =
		Fixture.Coordinator.ActivateMissileSalvo(PrimaryRequest, 0.12);
	TestTrue(TEXT("Primary active channel launches one legal Flight"), PrimarySalvo.WasLaunched());
	TestEqual(TEXT("Primary active channel launches at most and exactly one full Flight here"),
		PrimarySalvo.LaunchedCount, static_cast<int32>(GULI_WINGMAN_MEMBERS_PER_FLIGHT));

	const FGuLiWingmanMissileSalvoResult SharedGroupBlocked = Fixture.Coordinator.ActivateMissileSalvo(
		Fixture.MissileRequestForChannel(*SecondaryMissile, 101u), 0.13);
	TestTrue(TEXT("A second active channel in the same cooldown group is blocked"),
		SharedGroupBlocked.RejectReason == EGuLiWingmanRejectReason::CooldownActive);

	const FGuLiWingmanMissileSalvoRequest IndependentRequest =
		Fixture.MissileRequestForChannel(*IndependentMissile, 102u);
	const FGuLiWingmanMissileSalvoResult IndependentSalvo =
		Fixture.Coordinator.ActivateMissileSalvo(IndependentRequest, 0.13);
	TestTrue(TEXT("A different active cooldown group launches independently"),
		IndependentSalvo.WasLaunched());
	TestTrue(TEXT("Legacy default missile cooldown remains active without blocking another group"),
		Fixture.ASC->IsMissileCooldownActive());

	const FGuLiLogicalMissileState* IndependentLaunch = LaunchedStates.FindByPredicate(
		[IndependentMissile](const FGuLiLogicalMissileState& State)
		{
			return State.WeaponBinding == IndependentMissile->Binding;
		});
	if (TestNotNull(TEXT("Logical missile launch exposes the independent channel snapshot"), IndependentLaunch))
	{
		TestEqual(TEXT("Logical missile keeps its activation root"),
			IndependentLaunch->RootEventId, IndependentRequest.ActivationId);
		TestTrue(TEXT("Logical missile keeps the exact binding"),
			IndependentLaunch->WeaponBinding == IndependentMissile->Binding);
		TestEqual(TEXT("Logical missile keeps SkillId"),
			IndependentLaunch->SkillId, IndependentMissile->SkillId);
		TestEqual(TEXT("Logical missile keeps LoadoutRevision"),
			IndependentLaunch->LoadoutRevision, Fixture.Config.LoadoutRevision);
		TestEqual(TEXT("Logical missile keeps ProfileRevision"),
			IndependentLaunch->ProfileRevision, IndependentMissile->ProfileRevision);
		TestEqual(TEXT("Logical missile freezes committed damage"),
			IndependentLaunch->Damage, IndependentMissile->Runtime.Damage);
	}
	Fixture.Missiles->OnLaunch.Remove(LaunchHandle);

	const FGuLiWingmanHandle OldGeneration = Fixture.Accepted.Samples[10].Wingman;
	const uint32 NewGeneration = OldGeneration.EntityGeneration + 1u;
	TestTrue(TEXT("Server marks one stable-slot generation dead"),
		Fixture.Relay.MarkWingmanDead(OldGeneration));
	TestTrue(TEXT("Server replenishes that stable slot with a new generation"),
		Fixture.Relay.ReplenishWingman(
			OldGeneration.Flight.FlightIndex, OldGeneration.MemberIndex, NewGeneration));
	TestEqual(TEXT("Roster synchronization removes only the old generation state"),
		Fixture.Coordinator.SynchronizeRosterState(), 1);

	FGuLiWingmanHandle NewEmitter = OldGeneration;
	NewEmitter.EntityGeneration = NewGeneration;
	FGuLiWingmanAcceptedBatch ReplenishedSource = Fixture.Accepted;
	FGuLiWingmanCandidateSample* ReplacedSample = ReplenishedSource.Samples.FindByPredicate(
		[&OldGeneration](const FGuLiWingmanCandidateSample& Sample)
		{
			return Sample.Wingman == OldGeneration;
		});
	if (!TestNotNull(TEXT("Accepted fixture contains the replenished stable slot"), ReplacedSample))
	{
		return false;
	}
	ReplacedSample->Wingman = NewEmitter;
	ReplenishedSource.ServerAcceptedTimeSeconds = 0.15;
	ReplenishedSource.RefreshHash();
	FGuLiWingmanFireIntent ReplenishedIntent = Fixture.WeaponIntent(
		*PrimaryBasic, NewEmitter, Fixture.FarEnemyTarget, 1u);
	TestTrue(TEXT("New generation is denied before its full server-side first interval"),
		Fixture.Coordinator.ValidateBasicFireIntent(ReplenishedIntent, ReplenishedSource, 0.16)
			== EGuLiWingmanRejectReason::CooldownActive);

	FGuLiWingmanAcceptedBatch DueSource = ReplenishedSource;
	DueSource.ServerAcceptedTimeSeconds = 2.15;
	DueSource.RefreshHash();
	TestTrue(TEXT("New generation becomes eligible after the full server-side first interval"),
		Fixture.Coordinator.ValidateBasicFireIntent(ReplenishedIntent, DueSource, 2.16)
			== EGuLiWingmanRejectReason::None);
	return true;
}
#endif
