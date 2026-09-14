// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GuLiFlightNavigationData.h"
#include "GuLiFlightNavigationSubsystem.h"
#include "GuLiFlightNavigationVolume.h"
#include "Battle/Relay/GuLiWingmanRelayTypes.h"
#include "Gameplay/Wingman/Behavior/GuLiWingmanMemberBehavior.h"
#include "Gameplay/Wingman/GuLiWingmanPawn.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanFlightMovementComponent.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySet.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StateTreeComponentSchema.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodySetup.h"
#include "StateTree.h"

#if WITH_EDITOR
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#endif

namespace GuLiWingmanPawnSimulationTests
{
	struct FTransientGameWorldFixture
	{
		UWorld* World = nullptr;
		bool bWorldContextRegistered = false;

		~FTransientGameWorldFixture()
		{
			if (!World)
			{
				return;
			}

			World->DestroyWorld(false);
			if (bWorldContextRegistered && GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			World = nullptr;
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Engine exists for Wingman Pawn test World"), GEngine))
			{
				return false;
			}

			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("Transient Standalone game World exists"), World))
			{
				return false;
			}

			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bWorldContextRegistered = true;
			return Test.TestTrue(TEXT("Transient test World uses Standalone net mode"),
				World->IsGameWorld() && World->GetNetMode() == NM_Standalone);
		}
	};

	FGuLiWingmanGroupHandle MakeGroup()
	{
		FGuLiWingmanGroupHandle Group;
		Group.ShipInstanceId = FGuid(0x13572468, 0x24681357, 0x11223344, 0x55667788);
		Group.ShipGeneration = 3u;
		Group.GroupGeneration = 9u;
		return Group;
	}

	bool AddSingleCellFlightNavigation(
		FAutomationTestBase& Test,
		UWorld* World,
		const float HalfExtent)
	{
		UGuLiFlightNavigationSubsystem* Navigation = World
			? World->GetSubsystem<UGuLiFlightNavigationSubsystem>() : nullptr;
		if (!Test.TestNotNull(TEXT("Flight navigation subsystem exists for attack execution"), Navigation))
		{
			return false;
		}
		AGuLiFlightNavigationVolume* Volume = World->SpawnActor<AGuLiFlightNavigationVolume>(
			FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("Flight navigation volume exists for attack execution"), Volume))
		{
			return false;
		}
		UBrushComponent* Brush = Volume->GetBrushComponent();
		if (!Test.TestNotNull(TEXT("Flight navigation volume has bounds"), Brush))
		{
			return false;
		}
		Brush->BrushBodySetup = NewObject<UBodySetup>(Brush);
		FKBoxElem& Bounds = Brush->BrushBodySetup->AggGeom.BoxElems.AddDefaulted_GetRef();
		Bounds.X = HalfExtent * 2.0f;
		Bounds.Y = HalfExtent * 2.0f;
		Bounds.Z = HalfExtent * 2.0f;
		Brush->UpdateBounds();

		UGuLiFlightNavigationData* Data = NewObject<UGuLiFlightNavigationData>(Volume);
		Data->Metadata.FormatVersion = GuLiFlightNavigation::CurrentDataFormatVersion;
		Data->Metadata.DefinitionRevision = 1u;
		Data->Metadata.BakeId = FGuid(101u, 102u, 103u, 104u);
		Data->Metadata.Bounds = FBox::BuildAABB(FVector::ZeroVector, FVector(HalfExtent));
		Data->Metadata.MinimumCellSize = HalfExtent * 2.0f;
		Data->Metadata.BakedAgentRadius = 2000.0f;
		Data->Metadata.GeometrySignature = 0x10203040ull;
		Data->Metadata.SettingsHash = 0x50607080ull;
		FGuLiFlightNavOctreeNode& Node = Data->Nodes.AddDefaulted_GetRef();
		Node.Center = FVector::ZeroVector;
		Node.Extent = FVector(HalfExtent);
		Node.LeafCellIndex = 0;
		FGuLiFlightNavCell& Cell = Data->Cells.AddDefaulted_GetRef();
		Cell.Center = FVector::ZeroVector;
		Cell.Extent = FVector(HalfExtent);
		Cell.Clearance = 2000.0f;
		Cell.ComponentId = 0;
		Cell.StableId = 1u;
		Data->Metadata.ContentChecksum = Data->ComputeContentChecksum();
		Volume->NavigationData = Data;
		Navigation->RegisterVolume(Volume);
		return Test.TestEqual(TEXT("Attack fixture navigation accepts its interior"),
			Navigation->ValidateAuthoritativeSegment(
				FVector::ZeroVector, FVector(150000.0, 0.0, 20000.0), 1500.0f),
			EGuLiFlightNavSegmentStatus::Valid);
	}

	bool BuildValidV7AbilityConfig(
		FAutomationTestBase& Test,
		UObject* Outer,
		const FGuLiWingmanGroupHandle& Group,
		FGuLiGroupAbilityConfigSnapshot& OutConfig,
		const bool bUseModernAttackChannels = false)
	{
		UGuLiShipAbilitySet* AbilitySet = bUseModernAttackChannels
			? UGuLiShipAbilitySet::CreateNativeV3Transient(Outer)
			: UGuLiShipAbilitySet::CreateNativeV1Transient(Outer);
		if (!Test.TestNotNull(TEXT("Native Ship ability set exists"), AbilitySet))
		{
			return false;
		}

		FGuLiShipAbilityLoadoutState Loadout = bUseModernAttackChannels
			? FGuLiShipAbilityLoadoutState::MakeNativeV3()
			: FGuLiShipAbilityLoadoutState::MakeNativeV1();
		if (bUseModernAttackChannels)
		{
			Loadout.AbilityIds.Remove(TAG_GuLi_ShipAbility_Formation_SwarmOrbit);
			Loadout.AbilityIds.Add(TAG_GuLi_ShipAbility_Formation_DoubleRing);
			Loadout.Normalize();
		}
		TArray<FGuLiShipAbilityGrant> Grants;
		FString Error;
		if (!Test.TestTrue(FString::Printf(TEXT("Native v1 loadout resolves: %s"), *Error),
			AbilitySet->ResolveLoadout(Loadout, Grants, &Error)))
		{
			Test.AddError(Error);
			return false;
		}

		OutConfig = FGuLiGroupAbilityConfigSnapshot();
		OutConfig.ShipInstanceId = Group.ShipInstanceId;
		OutConfig.MatchEpoch = 7u;
		OutConfig.Team = EGuLiTeam::Red;
		OutConfig.OwnerPlayerGuid = FGuid(11u, 12u, 13u, 14u);
		OutConfig.WingmanTypeId = AbilitySet->WingmanTypeId;
		OutConfig.ShipGeneration = Group.ShipGeneration;
		OutConfig.GroupGeneration = Group.GroupGeneration;
		OutConfig.AbilitySetRevision = AbilitySet->Revision;
		OutConfig.LoadoutRevision = Loadout.Revision;
		OutConfig.SnapshotRevision = 1u;
		OutConfig.bGroupAbilitiesValid = true;
		for (const FGuLiShipAbilityGrant& Grant : Grants)
		{
			switch (Grant.Slot)
			{
			case EGuLiShipAbilitySlot::Formation:
				OutConfig.FormationAbilityId = Grant.AbilityId;
				OutConfig.FormationDefinitionRevision = Grant.GetDefinitionRevision();
				OutConfig.FormationDefinitionChecksum = Grant.GetDefinitionChecksum();
				if (!Grant.FormationDefinition
					|| !Grant.FormationDefinition->BuildRuntimeConfig(
						1u, OutConfig.FormationRuntime, &Error))
				{
					Test.AddError(Error);
					return false;
				}
				break;
			case EGuLiShipAbilitySlot::BasicWeapon:
			case EGuLiShipAbilitySlot::Missile:
				if (!Grant.WeaponDefinition)
				{
					return false;
				}
				{
					FGuLiWingmanWeaponChannelConfig& Channel =
						OutConfig.WeaponChannels.AddDefaulted_GetRef();
					Channel.Binding = FGuLiWeaponBindingKey::Wingman(
						OutConfig.MatchEpoch, OutConfig.Team, OutConfig.OwnerPlayerGuid,
						OutConfig.WingmanTypeId, Grant.GetEffectiveWeaponSlotId());
					Channel.SkillId = Grant.GetEffectiveSkillId();
					Channel.AbilityId = Grant.AbilityId;
					Channel.Kind = Grant.WeaponDefinition->Kind;
					Channel.CooldownGroupId = Grant.GetEffectiveCooldownGroupId();
					Channel.bEnabled = true;
					Channel.ProfileRevision = Grant.ProfileRevision;
					Channel.DefinitionRevision = Grant.GetDefinitionRevision();
					Channel.DefinitionChecksum = Grant.GetDefinitionChecksum();
					if (!Grant.WeaponDefinition->BuildRuntimeConfig(Channel.Runtime, &Error))
					{
						Test.AddError(Error);
						return false;
					}
					if (Grant.Slot == EGuLiShipAbilitySlot::BasicWeapon
						&& !OutConfig.BasicWeaponAbilityId.IsValid())
					{
						OutConfig.BasicWeaponAbilityId = Grant.AbilityId;
						OutConfig.BasicWeaponDefinitionRevision = Grant.GetDefinitionRevision();
						OutConfig.BasicWeaponDefinitionChecksum = Grant.GetDefinitionChecksum();
						OutConfig.BasicWeaponRuntime = Channel.Runtime;
					}
					else if (Grant.Slot == EGuLiShipAbilitySlot::Missile
						&& !OutConfig.MissileAbilityId.IsValid())
					{
						OutConfig.MissileAbilityId = Grant.AbilityId;
						OutConfig.MissileDefinitionRevision = Grant.GetDefinitionRevision();
						OutConfig.MissileDefinitionChecksum = Grant.GetDefinitionChecksum();
					}
				}
				break;
			default:
				break;
			}
		}
		OutConfig.FormationCommandRevision = 1u;
		OutConfig.EffectiveClientSimTick = 30u;
		OutConfig.RefreshHash();
		return Test.TestTrue(TEXT("Config is a complete usable protocol-v7 projection"),
			OutConfig.ProtocolVersion == GULI_WINGMAN_PROTOCOL_VERSION
			&& OutConfig.IsUsableByLeaseOwner());
	}

	FGuLiGroupAbilityConfigSnapshot BuildValidV9AbilityConfig(
		const FGuLiWingmanGroupHandle& Group)
	{
		FGuLiGroupAbilityConfigSnapshot Config;
		Config.ShipInstanceId = Group.ShipInstanceId;
		Config.MatchEpoch = 7u;
		Config.Team = EGuLiTeam::Red;
		Config.OwnerPlayerGuid = FGuid(0x11112222u, 0x33334444u, 0x55556666u, 0x77778888u);
		Config.WingmanTypeId = TEXT("TestWingman");
		Config.ShipGeneration = Group.ShipGeneration;
		Config.GroupGeneration = Group.GroupGeneration;
		Config.AbilitySetRevision = 1u;
		Config.LoadoutRevision = 1u;
		Config.SnapshotRevision = 1u;
		Config.bGroupAbilitiesValid = true;
		Config.FormationAbilityId = TAG_GuLi_ShipAbility_Formation_DoubleRing;
		Config.FormationDefinitionRevision = 1u;
		Config.FormationDefinitionChecksum = 0x1111222233334444ull;
		Config.FormationCommandRevision = 1u;
		Config.EffectiveClientSimTick = 30u;

		FGuLiWingmanWeaponChannelConfig& Primary = Config.WeaponChannels.AddDefaulted_GetRef();
		Primary.Binding = FGuLiWeaponBindingKey::Wingman(
			Config.MatchEpoch, Config.Team, Config.OwnerPlayerGuid, Config.WingmanTypeId, TEXT("PrimaryWeapon"));
		Primary.SkillId = TEXT("Test.Primary.Auto");
		Primary.AbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
		Primary.Kind = EGuLiWingmanWeaponKind::BasicAutomatic;
		Primary.bEnabled = true;
		Primary.ProfileRevision = 1u;
		Primary.DefinitionRevision = 1u;
		Primary.DefinitionChecksum = 0x2222333344445555ull;
		Primary.Runtime.CooldownSeconds = 2.0f;

		const FGuLiWingmanWeaponChannelConfig PrimaryCopy = Primary;
		FGuLiWingmanWeaponChannelConfig& Secondary = Config.WeaponChannels.AddDefaulted_GetRef();
		Secondary = PrimaryCopy;
		Secondary.Binding.SlotId = TEXT("SecondaryWeapon");
		Secondary.SkillId = TEXT("Test.Secondary.Auto");
		Secondary.AbilityId = TAG_GuLi_ShipWingman_Weapon_Basic;
		Secondary.ProfileRevision = 2u;
		Secondary.DefinitionRevision = 2u;
		Secondary.DefinitionChecksum = 0x3333444455556666ull;

		Config.BasicWeaponAbilityId = Config.WeaponChannels[0].AbilityId;
		Config.BasicWeaponDefinitionRevision = Config.WeaponChannels[0].DefinitionRevision;
		Config.BasicWeaponDefinitionChecksum = Config.WeaponChannels[0].DefinitionChecksum;
		Config.BasicWeaponRuntime = Config.WeaponChannels[0].Runtime;
		Config.RefreshHash();
		return Config;
	}

	FGuLiWingmanAcceptedBatch MakeAcceptedBatch(
		const FGuLiWingmanCandidateBatch& Candidate,
		const double AcceptedTimeSeconds = 1.0)
	{
		FGuLiWingmanAcceptedBatch Accepted;
		Accepted.Group = Candidate.Group;
		Accepted.StateRef.MatchEpoch = Candidate.MatchEpoch;
		Accepted.StateRef.GroupGeneration = Candidate.Group.GroupGeneration;
		Accepted.StateRef.AcceptedSequence = Candidate.CandidateSequence;
		Accepted.StateRef.ClientSimTick = Candidate.ClientSimTick;
		Accepted.CarrierSource = Candidate.CarrierSource;
		Accepted.Samples = Candidate.Samples;
		Accepted.AbilitySetRevision = Candidate.AbilitySetRevision;
		Accepted.FormationCommandRevision = Candidate.FormationCommandRevision;
		Accepted.FormationDefinitionChecksum = Candidate.FormationDefinitionChecksum;
		Accepted.ServerAcceptedTimeSeconds = AcceptedTimeSeconds;
		Accepted.RefreshHash();
		return Accepted;
	}

	FGuLiTargetHandle MakeTarget()
	{
		FGuLiTargetHandle Target;
		Target.Kind = EGuLiTargetKind::Ship;
		Target.AuthorityId = FGuid(0xdeadbeef, 0x12345678, 0x90abcdef, 0x11223344);
		Target.Generation = 2u;
		Target.LocalId = 1u;
		return Target;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanPawnExecutionDomainTest,
	"GuLiStrike.Wingman.Actor.ClientOnlyPawnExecutionDomain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanPawnExecutionDomainTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Client can own Pawn simulation"),
		UGuLiWingmanSimulationSubsystem::IsOwnerSimulationNetMode(NM_Client));
	TestTrue(TEXT("Standalone can own Pawn simulation"),
		UGuLiWingmanSimulationSubsystem::IsOwnerSimulationNetMode(NM_Standalone));
	TestTrue(TEXT("Listen host can own Pawn simulation"),
		UGuLiWingmanSimulationSubsystem::IsOwnerSimulationNetMode(NM_ListenServer));
	TestFalse(TEXT("Dedicated Server never owns Wingman Pawn simulation"),
		UGuLiWingmanSimulationSubsystem::IsOwnerSimulationNetMode(NM_DedicatedServer));
	TestFalse(TEXT("Native StateTree task never writes Pawn Transform"),
		FGuLiWingmanMemberBehaviorTask::WritesTransform());
	TestFalse(TEXT("Native StateTree task never writes flight velocity"),
		FGuLiWingmanMemberBehaviorTask::WritesVelocity());
	const AGuLiWingmanPawn* PawnDefaults = GetDefault<AGuLiWingmanPawn>();
	const USphereComponent* CollisionDefaults = PawnDefaults
		? PawnDefaults->FindComponentByClass<USphereComponent>() : nullptr;
	if (TestNotNull(TEXT("Wingman Pawn owns a collision probe"), CollisionDefaults))
	{
		TestEqual(TEXT("Wingman Pawns never block each other"),
			CollisionDefaults->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
	}
	const UStaticMeshComponent* VisualDefaults = PawnDefaults
		? PawnDefaults->FindComponentByClass<UStaticMeshComponent>() : nullptr;
	if (TestNotNull(TEXT("Wingman Pawn owns a visual mesh"), VisualDefaults))
	{
		TestTrue(TEXT("Only the visual mesh is yaw-corrected by 180 degrees"),
			FMath::IsNearlyEqual(
				FMath::Abs(FRotator::NormalizeAxis(VisualDefaults->GetRelativeRotation().Yaw)),
				180.0f,
				0.01f));
		TestTrue(TEXT("Visual correction leaves actor +X as the logical forward axis"),
			PawnDefaults->GetActorForwardVector().Equals(FVector::ForwardVector, 0.001f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanAuthoredStateTreeAssetContractTest,
	"GuLiStrike.Wingman.StateTree.AuthoredAssetContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanAuthoredStateTreeAssetContractTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR StateTreeObjectPath[] =
		TEXT("/Game/GuLiStrike/Wingman/ST_WingmanMemberBehavior.ST_WingmanMemberBehavior");
	UStateTree* StateTree = LoadObject<UStateTree>(nullptr, StateTreeObjectPath);
	if (!TestNotNull(TEXT("Authored Wingman StateTree asset loads"), StateTree))
	{
		return false;
	}
	TestTrue(TEXT("Authored Wingman StateTree is compiled and runnable"), StateTree->IsReadyToRun());
	TestTrue(TEXT("Runtime schema is compatible with UStateTreeComponent"),
		StateTree->GetSchema()
		&& StateTree->GetSchema()->GetClass()->IsChildOf(UStateTreeComponentSchema::StaticClass()));

	const TSet<FName> RequiredStates = {
		TEXT("Dead"), TEXT("EmergencyAvoid"), TEXT("GroundAttack"), TEXT("AirAttack"),
		TEXT("Rejoin"), TEXT("EscortOrbit")};
	TSet<FName> CompiledStateNames;
	for (const FCompactStateTreeState& State : StateTree->GetStates())
	{
		CompiledStateNames.Add(State.Name);
	}
	for (const FName StateName : RequiredStates)
	{
		TestTrue(FString::Printf(TEXT("Compiled StateTree contains %s"), *StateName.ToString()),
			CompiledStateNames.Contains(StateName));
	}

	int32 MemberTaskCount = 0;
	int32 MemberConditionCount = 0;
	for (const FConstStructView Node : StateTree->GetNodes())
	{
		MemberTaskCount += Node.GetScriptStruct()
			== FGuLiWingmanMemberBehaviorTask::StaticStruct() ? 1 : 0;
		MemberConditionCount += Node.GetScriptStruct()
			== FGuLiWingmanMemberBehaviorCondition::StaticStruct() ? 1 : 0;
	}
	TestEqual(TEXT("Compiled asset contains one native task per named state"),
		MemberTaskCount, 6);
	TestEqual(TEXT("Compiled asset contains one native entry condition per named state"),
		MemberConditionCount, 6);

#if WITH_EDITOR
	const UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
	if (TestNotNull(TEXT("Authored asset retains editor graph data"), EditorData)
		&& TestEqual(TEXT("Editor graph contains one root"), EditorData->SubTrees.Num(), 1)
		&& TestNotNull(TEXT("Editor root exists"), EditorData->SubTrees[0].Get()))
	{
		const UStateTreeState* Root = EditorData->SubTrees[0];
		TestEqual(TEXT("Root exposes six event signal transitions"), Root->Transitions.Num(), 6);
		TestEqual(TEXT("Root exposes six conditioned member states"), Root->Children.Num(), 6);
	}
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanAuthoredStateTreeOwnerRuntimeTest,
	"GuLiStrike.Wingman.StateTree.OwnerRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanAuthoredStateTreeOwnerRuntimeTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanPawnSimulationTests;
	FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	UGuLiWingmanSimulationSubsystem* Simulation =
		Fixture.World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	if (!TestNotNull(TEXT("Standalone owner simulation exists"), Simulation)) return false;
	Simulation->SetNavigationRequirementBypassForTests(true);
	UStateTree* StateTree = LoadObject<UStateTree>(nullptr,
		TEXT("/Game/GuLiStrike/Wingman/ST_WingmanMemberBehavior.ST_WingmanMemberBehavior"));
	if (!TestNotNull(TEXT("Runtime test authored StateTree loads"), StateTree)) return false;
	Simulation->SetMemberBehaviorStateTreeForTests(StateTree);

	const FGuLiWingmanGroupHandle Group = MakeGroup();
	FGuLiGroupAbilityConfigSnapshot AbilityConfig;
	if (!BuildValidV7AbilityConfig(*this, Simulation, Group, AbilityConfig)) return false;
	FGuLiCarrierSourceRef CarrierSource;
	CarrierSource.CanonicalEpoch = 4u;
	CarrierSource.MoveRevision = 12u;
	if (!TestTrue(TEXT("Local owner creates the 25-member group"),
		Simulation->CreateOrResetOwnedGroup(
			Group, AbilityConfig, FTransform::Identity, FVector::ZeroVector, CarrierSource)))
	{
		return false;
	}
	TestEqual(TEXT("Owner group contains 25 independent Pawns"),
		Simulation->GetOwnedPawnCount(Group), GULI_WINGMAN_GROUP_SIZE);
	FGuLiWingmanCandidateBatch Candidate;
	if (TestTrue(TEXT("Owner Pawn group produces a complete candidate"),
		Simulation->BuildCandidate(Group, 7u, 5u, 1u, 1u, Candidate)))
	{
		for (const FGuLiWingmanCandidateSample& Sample : Candidate.Samples)
		{
			const AGuLiWingmanPawn* Pawn = Simulation->FindOwnedPawn(Sample.Wingman);
			if (TestNotNull(TEXT("Each identity resolves to its Pawn"), Pawn))
			{
				TestTrue(TEXT("Each owner Pawn runs UE native StateTree"),
					Pawn->IsStateTreeRunning());
			}
		}
		if (!Candidate.Samples.IsEmpty())
		{
			AGuLiWingmanPawn* FirstPawn =
				Simulation->FindOwnedPawn(Candidate.Samples[0].Wingman);
			if (TestNotNull(TEXT("State transition fixture resolves one owner Pawn"), FirstPawn))
			{
				FirstPawn->GetMutableRuntimeState().Avoidance.bControlledRecovery = true;
				FirstPawn->UpdateBehaviorObservation(0.1f);
				FirstPawn->TickStateTreeForTests(0.2f);
				TestEqual(TEXT("This Pawn independently enters EmergencyAvoid"),
					FirstPawn->GetSelectedBehavior(),
					EGuLiWingmanMemberBehavior::EmergencyAvoid);
				FirstPawn->GetMutableRuntimeState().Avoidance.bControlledRecovery = false;
				FirstPawn->UpdateBehaviorObservation(0.1f);
				FirstPawn->TickStateTreeForTests(0.2f);
				TestEqual(TEXT("This Pawn independently transitions from recovery to Rejoin"),
					FirstPawn->GetSelectedBehavior(),
					EGuLiWingmanMemberBehavior::Rejoin);
			}
		}
	}
	TestTrue(TEXT("Runtime owner group cleanup succeeds"), Simulation->DestroyOwnedGroup(Group));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanOwnerPawnCandidateLifecycleTest,
	"GuLiStrike.Wingman.Pawn.OwnerGroupCandidateLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanOwnerPawnCandidateLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanPawnSimulationTests;
	FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UGuLiWingmanSimulationSubsystem* Simulation =
		Fixture.World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	if (!TestNotNull(TEXT("Standalone World creates the owner Wingman simulation subsystem"), Simulation))
	{
		return false;
	}
	// This case intentionally verifies the explicit fallback contract. The
	// authored asset has its own runtime test above.
	Simulation->SetMemberBehaviorStateTreeForTests(nullptr);
	const FGuLiWingmanGroupHandle Group = MakeGroup();
	FGuLiGroupAbilityConfigSnapshot AbilityConfig;
	if (!BuildValidV7AbilityConfig(*this, Simulation, Group, AbilityConfig))
	{
		return false;
	}
	AbilityConfig.FormationRuntime.InnerRingRadiusCentimeters = 61000.0f;
	AbilityConfig.FormationRuntime.OuterRingRadiusCentimeters = 92000.0f;
	AbilityConfig.FormationRuntime.InnerRingHeightCentimeters = 16000.0f;
	AbilityConfig.FormationRuntime.OuterRingHeightCentimeters = -17000.0f;
	AbilityConfig.RefreshHash();

	FGuLiCarrierSourceRef CarrierSource;
	CarrierSource.CanonicalEpoch = 4u;
	CarrierSource.MoveRevision = 12u;
	TestFalse(TEXT("Missing baked navigation rejects group creation in the production path"),
		Simulation->CreateOrResetOwnedGroup(
			Group, AbilityConfig, FTransform::Identity, FVector::ZeroVector, CarrierSource));
	TestEqual(TEXT("Rejected navigation gate creates no Pawns"),
		Simulation->GetTotalOwnedPawnCount(), 0);
	Simulation->SetNavigationRequirementBypassForTests(true);
	if (!TestTrue(TEXT("Valid v7 config creates one complete owner group"),
		Simulation->CreateOrResetOwnedGroup(
			Group, AbilityConfig, FTransform::Identity, FVector::ZeroVector, CarrierSource)))
	{
		return false;
	}
	TestEqual(TEXT("Owner group contains exactly 25 Pawns"),
		Simulation->GetOwnedPawnCount(Group), static_cast<int32>(GULI_WINGMAN_GROUP_SIZE));
	TestEqual(TEXT("No hidden extra owner Pawns were created"),
		Simulation->GetTotalOwnedPawnCount(), static_cast<int32>(GULI_WINGMAN_GROUP_SIZE));

	FGuLiWingmanCandidateBatch Candidate;
	if (!TestTrue(TEXT("Active owner group builds a protocol-v7 Candidate"),
		Simulation->BuildCandidate(Group, 7u, 5u, 1u, 31u, Candidate)))
	{
		return false;
	}
	TestEqual(TEXT("Candidate carries exactly 25 owner-produced samples"),
		Candidate.Samples.Num(), static_cast<int32>(GULI_WINGMAN_GROUP_SIZE));
	TestTrue(TEXT("The 25-sample Candidate remains well formed"), Candidate.IsWellFormed());
	TestEqual(TEXT("Candidate carries the committed ability-set revision"),
		Candidate.AbilitySetRevision, AbilityConfig.AbilitySetRevision);
	TestEqual(TEXT("Candidate carries the committed formation revision"),
		Candidate.FormationCommandRevision, AbilityConfig.FormationCommandRevision);
	TestEqual(TEXT("Inner-ring spawn consumes projected radius"),
		Candidate.Samples[0].PositionCentimeters.X, 61000);
	TestEqual(TEXT("Inner-ring spawn consumes projected height"),
		Candidate.Samples[0].PositionCentimeters.Z, 16000);
	TestEqual(TEXT("Outer-ring spawn consumes projected radius"),
		Candidate.Samples[13].PositionCentimeters.X, 92000);
	TestEqual(TEXT("Outer-ring spawn consumes projected height"),
		Candidate.Samples[13].PositionCentimeters.Z, -17000);

	FGuLiGroupAbilityConfigSnapshot SwitchedConfig = AbilityConfig;
	++SwitchedConfig.SnapshotRevision;
	++SwitchedConfig.FormationCommandRevision;
	SwitchedConfig.FormationRuntime.InnerRingRadiusCentimeters = 70000.0f;
	SwitchedConfig.RefreshHash();
	TestTrue(TEXT("Atomic formation commit updates Guidance inputs"),
		Simulation->ApplyCommittedAbilityConfig(Group, SwitchedConfig));
	FGuLiWingmanCandidateBatch CandidateAfterSwitch;
	TestTrue(TEXT("Committed formation remains candidate-authorized"),
		Simulation->BuildCandidate(Group, 7u, 5u, 2u, 32u, CandidateAfterSwitch));
	TestEqual(TEXT("Formation switch never teleports existing Wingman transforms"),
		CandidateAfterSwitch.Samples[0].PositionCentimeters.X, 61000);

	TestTrue(TEXT("Invalidation clears group combat authorization"),
		Simulation->InvalidateOwnedGroupAbilities(Group));
	TestEqual(TEXT("Combat invalidation preserves all 25 owner Pawns"),
		Simulation->GetOwnedPawnCount(Group), static_cast<int32>(GULI_WINGMAN_GROUP_SIZE));

	FGuLiWingmanCandidateBatch CandidateAfterInvalidation;
	TestTrue(TEXT("Combat invalidation cannot interrupt client pose production"),
		Simulation->BuildCandidate(Group, 7u, 5u, 3u, 33u, CandidateAfterInvalidation));
	TestEqual(TEXT("Post-invalidation pose still covers all live owner Pawns"),
		CandidateAfterInvalidation.Samples.Num(),
		static_cast<int32>(GULI_WINGMAN_GROUP_SIZE));
	const FVector FirstLocationBefore = FVector(
		CandidateAfterInvalidation.Samples[0].PositionCentimeters);
	TestTrue(TEXT("Combat invalidation cannot suspend the local fixed-step simulation"),
		Simulation->AdvanceOwnedGroup(Group, 0.2f));
	FGuLiWingmanCandidateBatch MovingAfterInvalidation;
	TestTrue(TEXT("Moving invalidated group remains publishable"),
		Simulation->BuildCandidate(Group, 7u, 5u, 4u, 37u, MovingAfterInvalidation));
	TestTrue(TEXT("A live owner Pawn keeps moving after combat authorization lapses"),
		FVector::Distance(FirstLocationBefore,
			FVector(MovingAfterInvalidation.Samples[0].PositionCentimeters)) > 1.0);

	TestTrue(TEXT("Test group cleanup succeeds"), Simulation->DestroyOwnedGroup(Group));
	TestEqual(TEXT("Test group cleanup removes every owner Pawn"),
		Simulation->GetTotalOwnedPawnCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanLocalContinuityAndNoPeerCollisionTest,
	"GuLiStrike.Wingman.Pawn.LocalContinuityAndNoPeerCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanLocalContinuityAndNoPeerCollisionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanPawnSimulationTests;
	FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	UGuLiWingmanSimulationSubsystem* Simulation =
		Fixture.World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	if (!TestNotNull(TEXT("Local continuity simulation exists"), Simulation)) return false;
	Simulation->SetNavigationRequirementBypassForTests(true);
	Simulation->SetMemberBehaviorStateTreeForTests(nullptr);

	const FGuLiWingmanGroupHandle Group = MakeGroup();
	FGuLiGroupAbilityConfigSnapshot AbilityConfig;
	if (!BuildValidV7AbilityConfig(*this, Simulation, Group, AbilityConfig)) return false;
	FGuLiCarrierSourceRef CarrierSource;
	CarrierSource.CanonicalEpoch = 4u;
	CarrierSource.MoveRevision = 12u;
	const FTransform InitialCarrier(
		FRotator(8.0, 42.0, -3.0), FVector(100000.0, -50000.0, 20000.0));
	if (!TestTrue(TEXT("Moving and rotated Ship creates 25 local Pawns"),
		Simulation->CreateOrResetOwnedGroup(
			Group, AbilityConfig, InitialCarrier, FVector(2500.0, 700.0, 100.0),
			CarrierSource)))
	{
		return false;
	}

	FGuLiWingmanCandidateBatch Initial;
	if (!TestTrue(TEXT("Continuity fixture captures the owner identities"),
		Simulation->BuildCandidate(Group, 7u, 5u, 1u, 31u, Initial))
		|| Initial.Samples.Num() < 2)
	{
		return false;
	}
	AGuLiWingmanPawn* First = Simulation->FindOwnedPawn(Initial.Samples[0].Wingman);
	AGuLiWingmanPawn* Second = Simulation->FindOwnedPawn(Initial.Samples[1].Wingman);
	if (!TestNotNull(TEXT("First local Pawn exists"), First)
		|| !TestNotNull(TEXT("Second local Pawn exists"), Second)
		|| !TestNotNull(TEXT("First Pawn owns its flight component"),
			First ? First->GetFlightMovement() : nullptr))
	{
		return false;
	}

	const FVector BeforeRecovery = First->GetActorLocation();
	TestTrue(TEXT("Deadlock recovery is executed locally without an authority request"),
		First->GetFlightMovement()->PerformLocalDeadlockRecoveryForTests());
	TestTrue(TEXT("Local recovery immediately leaves the failed point"),
		FVector::Distance(BeforeRecovery, First->GetActorLocation())
			>= AbilityConfig.FormationRuntime.AgentRadiusCentimeters * 2.0f);
	TestTrue(TEXT("Local recovery restores at least minimum fixed-wing speed"),
		First->GetRuntimeState().Dynamics.Velocity.Size() + UE_KINDA_SMALL_NUMBER
			>= AbilityConfig.FormationRuntime.MinimumSpeedCentimetersPerSecond);
	TestFalse(TEXT("Local recovery never waits for an authority rebase"),
		First->GetRuntimeState().Avoidance.bAwaitingRebase);
	TestFalse(TEXT("Local recovery emits no automatic rebase request"),
		First->GetRuntimeState().Avoidance.bRebaseRequested);
	TestFalse(TEXT("Local recovery never marks the owner Pawn stale"),
		First->GetRuntimeState().Dynamics.bStale);

	const FGuLiCarrierSourceRef MissingNetworkSource;
	const FTransform MovedCarrier(
		FRotator(-12.0, 137.0, 5.0), FVector(220000.0, 90000.0, 45000.0));
	TestTrue(TEXT("A transient server source gap cannot block local Ship tracking"),
		Simulation->UpdateOwnedGroupCarrier(
			Group, MovedCarrier, FVector(3200.0, -400.0, 250.0), MissingNetworkSource));
	TestTrue(TEXT("Owner Pawn consumes the live local Ship position and rotation"),
		First->GetRuntimeState().Carrier.Transform.Equals(MovedCarrier, 0.01));
	TestTrue(TEXT("Last publishable carrier source survives the network source gap"),
		First->GetRuntimeState().Carrier.Source.CanonicalEpoch == CarrierSource.CanonicalEpoch
			&& First->GetRuntimeState().Carrier.Source.MoveRevision == CarrierSource.MoveRevision);

	const FTransform SharedStart = First->GetActorTransform();
	Second->SetActorTransform(SharedStart, false, nullptr, ETeleportType::TeleportPhysics);
	const FVector FirstOverlapStart = First->GetActorLocation();
	const FVector SecondOverlapStart = Second->GetActorLocation();
	TestTrue(TEXT("Overlapping Wingman Pawns still advance independently"),
		Simulation->AdvanceOwnedGroup(Group, 0.2f));
	TestTrue(TEXT("First overlapping Pawn is not blocked by its peer"),
		FVector::Distance(FirstOverlapStart, First->GetActorLocation()) > 1.0);
	TestTrue(TEXT("Second overlapping Pawn is not blocked by its peer"),
		FVector::Distance(SecondOverlapStart, Second->GetActorLocation()) > 1.0);
	TestTrue(TEXT("Neither overlapping Pawn enters an authority-wait state"),
		!First->GetRuntimeState().Avoidance.bAwaitingRebase
			&& !Second->GetRuntimeState().Avoidance.bAwaitingRebase);

	TestTrue(TEXT("Local continuity fixture cleans up"),
		Simulation->DestroyOwnedGroup(Group));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanFlightNavigationLifecycleTest,
	"GuLiStrike.Wingman.Pawn.FlightNavigationAsyncStateLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanFlightNavigationLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanPawnSimulationTests;
	FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	UGuLiWingmanSimulationSubsystem* Simulation =
		Fixture.World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	if (!TestNotNull(TEXT("Standalone navigation test simulation exists"), Simulation)) return false;
	Simulation->SetNavigationRequirementBypassForTests(true);

	const FGuLiWingmanGroupHandle Group = MakeGroup();
	FGuLiGroupAbilityConfigSnapshot AbilityConfig;
	if (!BuildValidV7AbilityConfig(*this, Simulation, Group, AbilityConfig)) return false;
	FGuLiCarrierSourceRef CarrierSource;
	CarrierSource.CanonicalEpoch = 4u;
	CarrierSource.MoveRevision = 12u;
	if (!TestTrue(TEXT("Navigation lifecycle group creates 25 owner Pawns"),
		Simulation->CreateOrResetOwnedGroup(
			Group, AbilityConfig, FTransform::Identity, FVector::ZeroVector, CarrierSource)))
	{
		return false;
	}

	FGuLiWingmanCandidateBatch BeforeNavigation;
	if (!TestTrue(TEXT("Capture pre-navigation transform cut"),
		Simulation->BuildCandidate(Group, 7u, 5u, 1u, 31u, BeforeNavigation)))
	{
		return false;
	}

	for (uint8 FlightIndex = 0u; FlightIndex < GULI_WINGMAN_FLIGHT_COUNT; ++FlightIndex)
	{
		const double LateralOffset = static_cast<double>(FlightIndex) * 10000.0;
		const TArray<FVector> SmoothedPath = {
			FVector(0.0, LateralOffset, 0.0),
			FVector(25000.0, LateralOffset + 5000.0, 10000.0),
			FVector(50000.0, LateralOffset, 15000.0)
		};
		if (!TestTrue(TEXT("Each of five Flights owns an independent accepted path state"),
			Simulation->PrimeFlightNavigationStateForTests(
				Group, FlightIndex, SmoothedPath, FlightIndex == 0u)))
		{
			return false;
		}
	}

	FGuLiWingmanNavigationDiagnostics ActiveDiagnostics;
	if (!TestTrue(TEXT("Navigation diagnostics resolve the active group"),
		Simulation->GetNavigationDiagnostics(Group, ActiveDiagnostics))) return false;
	TestEqual(TEXT("All five Flights retain independent navigation paths"),
		ActiveDiagnostics.ActiveFlightPaths, static_cast<int32>(GULI_WINGMAN_FLIGHT_COUNT));
	TestEqual(TEXT("One simulated request is pending before the revision switch"),
		ActiveDiagnostics.PendingFlightRequests, 1);
	TestEqual(TEXT("One waypoint cut guides all five members of every Flight"),
		ActiveDiagnostics.NavigationGuidedEntities, static_cast<int32>(GULI_WINGMAN_GROUP_SIZE));

	FGuLiWingmanCandidateBatch AfterPathPublication;
	TestTrue(TEXT("Publishing path Guidance keeps candidate production valid"),
		Simulation->BuildCandidate(Group, 7u, 5u, 2u, 32u, AfterPathPublication));
	TestEqual(TEXT("Path publication never teleports the first owner Pawn"),
		AfterPathPublication.Samples[0].PositionCentimeters, BeforeNavigation.Samples[0].PositionCentimeters);

	FGuLiGroupAbilityConfigSnapshot SwitchedConfig = AbilityConfig;
	++SwitchedConfig.SnapshotRevision;
	++SwitchedConfig.FormationCommandRevision;
	SwitchedConfig.FormationRuntime.InnerRingRadiusCentimeters += 10000.0f;
	SwitchedConfig.RefreshHash();
	if (!TestTrue(TEXT("Formation revision switch commits without a Transform write"),
		Simulation->ApplyCommittedAbilityConfig(Group, SwitchedConfig))) return false;

	FGuLiWingmanNavigationDiagnostics ClearedDiagnostics;
	if (!TestTrue(TEXT("Post-switch navigation diagnostics resolve the group"),
		Simulation->GetNavigationDiagnostics(Group, ClearedDiagnostics))) return false;
	TestEqual(TEXT("Formation switch clears all old Flight paths"), ClearedDiagnostics.ActiveFlightPaths, 0);
	TestEqual(TEXT("Formation switch clears every pending Flight request"),
		ClearedDiagnostics.PendingFlightRequests, 0);
	TestEqual(TEXT("Formation switch removes stale navigation Guidance from all entities"),
		ClearedDiagnostics.NavigationGuidedEntities, 0);
	TestEqual(TEXT("Formation switch cancels the old request token exactly once"),
		ClearedDiagnostics.PendingRequestsCancelled, static_cast<uint64>(1));

	FGuLiWingmanCandidateBatch AfterFormationSwitch;
	TestTrue(TEXT("New formation cut remains candidate-authorized"),
		Simulation->BuildCandidate(Group, 7u, 5u, 3u, 33u, AfterFormationSwitch));
	TestEqual(TEXT("Formation switch changes Guidance only and never teleports"),
		AfterFormationSwitch.Samples[0].PositionCentimeters,
		BeforeNavigation.Samples[0].PositionCentimeters);

	TestTrue(TEXT("Seed one more pending request for destruction cleanup"),
		Simulation->PrimeFlightNavigationStateForTests(Group, 2u, TArray<FVector>(), true));
	TestTrue(TEXT("Destroying the group clears entities and cancels navigation"),
		Simulation->DestroyOwnedGroup(Group));
	TestEqual(TEXT("No owner Pawns survive group destruction"),
		Simulation->GetTotalOwnedPawnCount(), 0);
	FGuLiWingmanNavigationDiagnostics DestroyedDiagnostics;
	TestFalse(TEXT("Destroyed group has no navigation runtime"),
		Simulation->GetNavigationDiagnostics(Group, DestroyedDiagnostics));
	TestEqual(TEXT("Group destruction cancels its final pending request token"),
		DestroyedDiagnostics.PendingRequestsCancelled, static_cast<uint64>(2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanBasicWeaponIntentStateTest,
	"GuLiStrike.Wingman.Pawn.BasicWeaponIndependentAcceptedState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanBasicWeaponIntentStateTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanPawnSimulationTests;
	FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	UGuLiWingmanSimulationSubsystem* Simulation = Fixture.World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	if (!TestNotNull(TEXT("Standalone simulation exists"), Simulation)) return false;
	Simulation->SetNavigationRequirementBypassForTests(true);

	const FGuLiWingmanGroupHandle Group = MakeGroup();
	FGuLiGroupAbilityConfigSnapshot AbilityConfig;
	if (!BuildValidV7AbilityConfig(*this, Simulation, Group, AbilityConfig)) return false;
	FGuLiCarrierSourceRef CarrierSource;
	CarrierSource.CanonicalEpoch = 4u;
	CarrierSource.MoveRevision = 12u;
	if (!TestTrue(TEXT("Create 25 emitters"), Simulation->CreateOrResetOwnedGroup(
		Group, AbilityConfig, FTransform::Identity, FVector::ZeroVector, CarrierSource))) return false;

	FGuLiWingmanCandidateBatch Candidate;
	if (!TestTrue(TEXT("Build source candidate"),
		Simulation->BuildCandidate(Group, 7u, 5u, 1u, 31u, Candidate))) return false;
	TestEqual(TEXT("Source candidate covers all emitters"), Candidate.Samples.Num(), 25);
	const FGuLiTargetHandle Target = MakeTarget();
	FGuLiWingmanAttackTarget AssignedTarget;
	AssignedTarget.Target = Target;
	AssignedTarget.Location = FVector(1000000.0, 0.0, 0.0);
	AssignedTarget.Revision = 1u;
	FGuLiWingmanFireIntent NoAcceptedIntent;
	TestFalse(TEXT("Emitter without a confirmed accepted state cannot fire"),
		Simulation->TryBuildBasicFireIntent(Group, Candidate.Samples[0].Wingman, 7u, 5u,
			AssignedTarget, 10.0, 0.5, 32u, true, NoAcceptedIntent));

	const FGuLiWingmanAcceptedBatch Accepted = MakeAcceptedBatch(Candidate, 2.0);
	if (!TestTrue(TEXT("Confirmed accepted batch updates emitter source refs"),
		Simulation->ApplyAcceptedBatch(Accepted))) return false;

	TSet<FGuLiWingmanHandle> Emitters;
	for (const FGuLiWingmanCandidateSample& Sample : Candidate.Samples)
	{
		FGuLiWingmanFireIntent Intent;
		if (!TestTrue(TEXT("Each accepted emitter independently produces an intent"),
			Simulation->TryBuildBasicFireIntent(Group, Sample.Wingman, 7u, 5u,
				AssignedTarget, 10.0, 0.5, 32u, true, Intent)))
		{
			return false;
		}
		TestEqual(TEXT("First sequence in each emitter domain is one"), Intent.DomainFireSequence, 1u);
		TestTrue(TEXT("Intent carries exact accepted source ref"),
			Intent.SourceAcceptedState.AcceptedSequence == Accepted.StateRef.AcceptedSequence
			&& Intent.SourceAcceptedState.ClientSimTick == Accepted.StateRef.ClientSimTick);
		TestEqual(TEXT("Intent carries the actual client fire fixed-step"), Intent.ClientFireTick, 32u);
		TestTrue(TEXT("Intent carries client LOS only as a prediction hint"),
			Intent.bClientPredictedLineOfSight);
		TestEqual(TEXT("Intent carries confirmed basic ability revision"),
			Intent.AbilitySetRevision, AbilityConfig.AbilitySetRevision);
		TestTrue(TEXT("Protocol-v7 basic intent is complete"), Intent.IsWellFormed());
		Emitters.Add(Intent.Emitter);
	}
	TestEqual(TEXT("All 25 stable emitter domains were exercised"), Emitters.Num(), 25);

	FGuLiWingmanFireIntent CoolingDownIntent;
	TestFalse(TEXT("One emitter's own cooldown blocks only that emitter request"),
		Simulation->TryBuildBasicFireIntent(Group, Candidate.Samples[0].Wingman, 7u, 5u,
			AssignedTarget, 10.25, 0.5, 33u, true, CoolingDownIntent));
	const FGuLiWingmanWeaponChannelConfig* BasicChannel =
		AbilityConfig.FindFirstWeaponChannel(EGuLiWingmanWeaponKind::BasicAutomatic);
	if (!TestNotNull(TEXT("Basic intent fixture exposes its authoritative channel"), BasicChannel))
	{
		return false;
	}
	FGuLiWingmanFireIntent SecondIntent;
	TestTrue(TEXT("Emitter advances its own domain after its cooldown"),
		Simulation->TryBuildBasicFireIntent(Group, Candidate.Samples[0].Wingman, 7u, 5u,
			AssignedTarget, 10.0 + BasicChannel->Runtime.CooldownSeconds,
			0.5, 34u, true, SecondIntent));
	TestEqual(TEXT("Only successful builds advance that emitter's sequence"),
		SecondIntent.DomainFireSequence, 2u);

	FGuLiGroupAbilityConfigSnapshot NewAbilityConfig = AbilityConfig;
	++NewAbilityConfig.AbilitySetRevision;
	++NewAbilityConfig.SnapshotRevision;
	NewAbilityConfig.RefreshHash();
	if (!TestTrue(TEXT("New committed ability version is applied"),
		Simulation->ApplyCommittedAbilityConfig(Group, NewAbilityConfig))) return false;
	TestFalse(TEXT("Accepted batch from old ability version is rejected"),
		Simulation->ApplyAcceptedBatch(Accepted));
	FGuLiWingmanFireIntent OldSourceIntent;
	TestFalse(TEXT("Old accepted ref cannot fire after ability version changes"),
		Simulation->TryBuildBasicFireIntent(Group, Candidate.Samples[1].Wingman, 7u, 5u,
			AssignedTarget, 20.0, 0.5, 35u, true, OldSourceIntent));

	TestTrue(TEXT("Cleanup 25-emitter test group"), Simulation->DestroyOwnedGroup(Group));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanPerMemberAttackExecutionTest,
	"GuLiStrike.Wingman.Pawn.PerMemberAirGroundAttackExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanPerMemberAttackExecutionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanPawnSimulationTests;
	FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	UGuLiWingmanSimulationSubsystem* Simulation =
		Fixture.World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	if (!TestNotNull(TEXT("Standalone attack simulation exists"), Simulation)) return false;
	Simulation->SetNavigationRequirementBypassForTests(true);
	Simulation->SetMemberBehaviorStateTreeForTests(nullptr);

	const FGuLiWingmanGroupHandle Group = MakeGroup();
	FGuLiGroupAbilityConfigSnapshot Config;
	if (!BuildValidV7AbilityConfig(*this, Simulation, Group, Config, true)) return false;
	FGuLiCarrierSourceRef CarrierSource;
	CarrierSource.CanonicalEpoch = 4u;
	CarrierSource.MoveRevision = 12u;
	if (!TestTrue(TEXT("Per-member attack fixture creates 25 entities"),
		Simulation->CreateOrResetOwnedGroup(
			Group, Config, FTransform::Identity, FVector::ZeroVector, CarrierSource)))
	{
		return false;
	}
	FGuLiWingmanCandidateBatch Candidate;
	if (!TestTrue(TEXT("Per-member attack fixture captures identities and poses"),
		Simulation->BuildCandidate(Group, 7u, 5u, 1u, 31u, Candidate)))
	{
		return false;
	}
	if (!AddSingleCellFlightNavigation(*this, Fixture.World, 500000.0f)) return false;
	AActor* Floor = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Per-member ground run floor exists"), Floor)) return false;
	UBoxComponent* FloorBox = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(FloorBox);
	Floor->AddInstanceComponent(FloorBox);
	FloorBox->SetBoxExtent(FVector(300000.0, 300000.0, 100.0));
	FloorBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FloorBox->SetCollisionObjectType(ECC_WorldStatic);
	FloorBox->SetCollisionResponseToAllChannels(ECR_Block);
	FloorBox->RegisterComponent();
	Floor->SetActorLocation(FVector(0.0, 0.0, -100.0));

	FGuLiWingmanAttackAuthorityState State;
	State.Revision = 1u;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FGuLiWingmanAutoTargetAssignment& Assignment = State.AutomaticTargets.AddDefaulted_GetRef();
		Assignment.Emitter = Candidate.Samples[Index].Wingman;
		Assignment.Target.Target.Kind = Index == 0
			? EGuLiTargetKind::CommanderSoldier : EGuLiTargetKind::Ship;
		Assignment.Target.Target.AuthorityId = FGuid(90u, 91u, 92u, static_cast<uint32>(Index + 1));
		Assignment.Target.Target.Generation = 1u;
		Assignment.Target.Target.LocalId = static_cast<uint32>(Index + 1);
		Assignment.Target.Location = Index == 0
			? FVector(100000.0, 0.0, 0.0)
			: FVector(Candidate.Samples[Index].PositionCentimeters)
				+ (Index == 2 ? FVector(0.0, 100000.0, 0.0) : FVector(100000.0, 0.0, 0.0));
		Assignment.Target.Radius = 100.0f;
		Assignment.Target.bGround = Index == 0;
		Assignment.Target.Revision = static_cast<uint32>(10 + Index);
		Assignment.Target.ServerTime = 1.0;
	}
	if (!TestTrue(TEXT("Mixed per-member attack table is well formed"), State.IsWellFormed(Group)))
	{
		return false;
	}
	Simulation->TickAttackRuns(Group, State, 5u, 32u, 1.0, true);
	TArray<FGuLiWingmanAttackDiagnostic> Diagnostics;
	Simulation->GetAttackDiagnostics(Diagnostics);
	const auto FindDiagnostic = [&Diagnostics](const FGuLiWingmanHandle& Emitter)
	{
		return Diagnostics.FindByPredicate([&Emitter](const FGuLiWingmanAttackDiagnostic& Entry)
		{
			return Entry.Wingman == Emitter;
		});
	};
	const FGuLiWingmanAttackDiagnostic* Ground = FindDiagnostic(Candidate.Samples[0].Wingman);
	const FGuLiWingmanAttackDiagnostic* AirX = FindDiagnostic(Candidate.Samples[1].Wingman);
	const FGuLiWingmanAttackDiagnostic* AirY = FindDiagnostic(Candidate.Samples[2].Wingman);
	const FGuLiWingmanAttackDiagnostic* Unassigned = FindDiagnostic(Candidate.Samples[3].Wingman);
	if (!TestNotNull(TEXT("Ground member diagnostics exist"), Ground)
		|| !TestNotNull(TEXT("First air member diagnostics exist"), AirX)
		|| !TestNotNull(TEXT("Second air member diagnostics exist"), AirY)
		|| !TestNotNull(TEXT("Unassigned member diagnostics exist"), Unassigned))
	{
		return false;
	}
	TestEqual(TEXT("Ground assignment selects the existing ingress state"), Ground->Phase,
		static_cast<uint8>(EGuLiWingmanAttackPhase::Ingress));
	TestEqual(TEXT("Air assignment selects the existing dogfight approach state"), AirX->Phase,
		static_cast<uint8>(EGuLiWingmanAttackPhase::AirApproachFire));
	TestTrue(TEXT("Distinct air members steer toward their own assigned targets"),
		FVector::DotProduct(AirX->PreferredVelocity.GetSafeNormal(), FVector::ForwardVector) > 0.99
		&& FVector::DotProduct(AirY->PreferredVelocity.GetSafeNormal(), FVector::RightVector) > 0.99);
	TestFalse(TEXT("A member without an automatic assignment does not attack"), Unassigned->bGuiding);

	const FGuLiWingmanAttackTarget FrozenGroundTarget = Ground->Target;
	const FName FrozenGroundSlot = Ground->SlotId;
	const FVector FrozenGroundEntry = Ground->Entry;
	State.AutomaticTargets[0].Target.Location += FVector(0.0, 2000.0, 0.0);
	State.AutomaticTargets[0].Target.ServerTime = 1.05;
	++State.Revision;
	Simulation->TickAttackRuns(Group, State, 5u, 33u, 1.05, true);
	Diagnostics.Reset();
	Simulation->GetAttackDiagnostics(Diagnostics);
	Ground = FindDiagnostic(Candidate.Samples[0].Wingman);
	TestTrue(TEXT("A same-revision position refresh does not rebuild a prepared ground run"),
		Ground
		&& Ground->Target.Location.Equals(FrozenGroundTarget.Location, 0.01)
		&& Ground->Entry.Equals(FrozenGroundEntry, 0.01)
		&& Ground->Phase == static_cast<uint8>(EGuLiWingmanAttackPhase::Ingress));
	if (!TestTrue(TEXT("Prepared ground run can enter its frozen dive phase"),
		Simulation->PrimeGroundDiveForTests(Group, Candidate.Samples[0].Wingman, 1.1)))
	{
		return false;
	}
	FGuLiWingmanAttackTarget NextAirTarget = State.AutomaticTargets[1].Target;
	NextAirTarget.Target.AuthorityId = FGuid(93u, 94u, 95u, 96u);
	NextAirTarget.Target.LocalId = 30u;
	NextAirTarget.Location = FVector(0.0, -100000.0, 5000.0);
	NextAirTarget.Revision = 30u;
	NextAirTarget.ServerTime = 1.1;
	State.AutomaticTargets[0].Target = NextAirTarget;
	++State.Revision;
	Simulation->TickAttackRuns(Group, State, 5u, 34u, 1.1, true);
	Diagnostics.Reset();
	Simulation->GetAttackDiagnostics(Diagnostics);
	Ground = FindDiagnostic(Candidate.Samples[0].Wingman);
	TestTrue(TEXT("Dive keeps its frozen target and ground channel while a new assignment arrives"),
		Ground
		&& Ground->Phase == static_cast<uint8>(EGuLiWingmanAttackPhase::Dive)
		&& Ground->Target.Target == FrozenGroundTarget.Target
		&& Ground->SlotId == FrozenGroundSlot);

	Simulation->TickAttackRuns(Group, State, 5u, 35u, 1.2, false);
	State.AutomaticTargets[0].Target.ServerTime = 2.3;
	++State.Revision;
	Simulation->TickAttackRuns(Group, State, 5u, 36u, 2.3, true);
	Diagnostics.Reset();
	Simulation->GetAttackDiagnostics(Diagnostics);
	Ground = FindDiagnostic(Candidate.Samples[0].Wingman);
	TestTrue(TEXT("After the frozen run ends the member consumes its latest air assignment"),
		Ground
		&& Ground->Phase == static_cast<uint8>(EGuLiWingmanAttackPhase::AirApproachFire)
		&& Ground->Target.Target == NextAirTarget.Target
		&& Ground->SlotId != FrozenGroundSlot);

	State.AutomaticTargets.Reset();
	State.Target.Target.Kind = EGuLiTargetKind::Ship;
	State.Target.Target.AuthorityId = FGuid(95u, 96u, 97u, 98u);
	State.Target.Target.Generation = 1u;
	State.Target.Location = FVector(0.0, -1000000.0, 0.0);
	State.Target.Radius = 100.0f;
	State.Target.bSpecified = true;
	State.Target.Revision = 20u;
	State.Target.ServerTime = 3.4;
	++State.Revision;
	Simulation->TickAttackRuns(Group, State, 5u, 37u, 3.4, true);
	Diagnostics.Reset();
	Simulation->GetAttackDiagnostics(Diagnostics);
	Ground = FindDiagnostic(Candidate.Samples[0].Wingman);
	AirX = FindDiagnostic(Candidate.Samples[1].Wingman);
	if (!TestNotNull(TEXT("Ground member remains observable after manual override"), Ground)
		|| !TestNotNull(TEXT("Air member remains observable after manual override"), AirX))
	{
		return false;
	}
	TestTrue(TEXT("Manual target replaces the ground member's prior automatic target"),
		Ground->Target.Target == State.Target.Target);
	TestTrue(TEXT("Manual target replaces the air member's prior automatic target"),
		AirX->Target.Target == State.Target.Target);
	TestEqual(TEXT("Ground member uses the manual target's air attack channel"), Ground->Phase,
		static_cast<uint8>(EGuLiWingmanAttackPhase::AirApproachFire));
	TestEqual(TEXT("Air member keeps the manual target's air attack channel"), AirX->Phase,
		static_cast<uint8>(EGuLiWingmanAttackPhase::AirApproachFire));
	TestTrue(TEXT("Ground member steers toward the shared manual target"),
		FVector::DotProduct(Ground->PreferredVelocity.GetSafeNormal(), -FVector::RightVector) > 0.99);

	TestTrue(TEXT("Per-member attack fixture cleans up"), Simulation->DestroyOwnedGroup(Group));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanWeaponOnlyConfigPreservesFormationTest,
	"GuLiStrike.Wingman.Simulation.WeaponOnlyConfigPreservesFormation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanWeaponOnlyConfigPreservesFormationTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanPawnSimulationTests;
	FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	UGuLiWingmanSimulationSubsystem* Simulation =
		Fixture.World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	if (!TestNotNull(TEXT("Standalone owner simulation exists"), Simulation))
	{
		return false;
	}
	Simulation->SetNavigationRequirementBypassForTests(true);
	Simulation->SetMemberBehaviorStateTreeForTests(nullptr);

	const FGuLiWingmanGroupHandle Group = MakeGroup();
	FGuLiGroupAbilityConfigSnapshot Config = BuildValidV9AbilityConfig(Group);
	if (!TestTrue(TEXT("Protocol-v9 two-channel fixture is valid"), Config.IsUsableByLeaseOwner()))
	{
		return false;
	}
	FGuLiCarrierSourceRef CarrierSource;
	CarrierSource.CanonicalEpoch = 4u;
	CarrierSource.MoveRevision = 12u;
	if (!TestTrue(TEXT("Create one complete owner group"),
		Simulation->CreateOrResetOwnedGroup(
			Group, Config, FTransform::Identity, FVector::ZeroVector, CarrierSource)))
	{
		return false;
	}

	FGuLiWingmanCandidateBatch InitialCandidate;
	if (!TestTrue(TEXT("Capture the initial member and formation pose cut"),
		Simulation->BuildCandidate(Group, Config.MatchEpoch, 5u, 1u, 31u, InitialCandidate)))
	{
		return false;
	}
	const FGuLiWingmanHandle OriginalEmitter = InitialCandidate.Samples[0].Wingman;
	const FIntVector OriginalPosition = InitialCandidate.Samples[0].PositionCentimeters;
	const FGuLiWingmanAcceptedBatch InitialAccepted = MakeAcceptedBatch(InitialCandidate, 0.0);
	if (!TestTrue(TEXT("Initial accepted cut authorizes weapon state"),
		Simulation->ApplyAcceptedBatch(InitialAccepted)))
	{
		return false;
	}

	const TArray<FVector> NavigationPath = {
		FVector(0.0, 0.0, 0.0), FVector(25000.0, 5000.0, 10000.0), FVector(50000.0, 0.0, 15000.0)};
	if (!TestTrue(TEXT("Seed one pending navigation path"),
		Simulation->PrimeFlightNavigationStateForTests(Group, 0u, NavigationPath, true)))
	{
		return false;
	}
	FGuLiWingmanNavigationDiagnostics NavigationBefore;
	if (!TestTrue(TEXT("Read navigation state before the weapon-only commit"),
		Simulation->GetNavigationDiagnostics(Group, NavigationBefore)))
	{
		return false;
	}

	const FGuLiTargetHandle Target = MakeTarget();
	const FVector TargetLocation(1000000.0, 0.0, 0.0);
	FGuLiWingmanAttackTarget AssignedTarget;
	AssignedTarget.Target = Target;
	AssignedTarget.Location = TargetLocation;
	AssignedTarget.Revision = 1u;
	FGuLiWingmanFireIntent PrimaryIntent;
	FGuLiWingmanFireIntent SecondaryIntent;
	TestTrue(TEXT("Primary slot may fire at its own initial cadence"),
		Simulation->TryBuildWeaponFireIntent(Group, OriginalEmitter, Config.MatchEpoch, 5u,
			Config.WeaponChannels[0], AssignedTarget, 0.0, 32u, true, PrimaryIntent));
	TestTrue(TEXT("Secondary slot on the same member is independently ready"),
		Simulation->TryBuildWeaponFireIntent(Group, OriginalEmitter, Config.MatchEpoch, 5u,
			Config.WeaponChannels[1], AssignedTarget, 0.0, 33u, true, SecondaryIntent));
	TestEqual(TEXT("One emitter sequence advances across both slots"),
		SecondaryIntent.DomainFireSequence, PrimaryIntent.DomainFireSequence + 1u);

	FGuLiGroupAbilityConfigSnapshot WeaponOnly = Config;
	++WeaponOnly.LoadoutRevision;
	++WeaponOnly.SnapshotRevision;
	++WeaponOnly.WeaponChannels[0].ProfileRevision;
	WeaponOnly.WeaponChannels[0].Runtime.CooldownSeconds = 4.0f;
	FGuLiWingmanWeaponChannelConfig Tertiary = WeaponOnly.WeaponChannels[0];
	Tertiary.Binding.SlotId = TEXT("TertiaryWeapon");
	Tertiary.SkillId = TEXT("Test.Tertiary.Auto");
	Tertiary.AbilityId = TAG_GuLi_ShipAbility_Reticle_Omni;
	Tertiary.ProfileRevision = 3u;
	Tertiary.DefinitionRevision = 3u;
	Tertiary.DefinitionChecksum = 0x4444555566667777ull;
	Tertiary.Runtime.CooldownSeconds = 3.0f;
	WeaponOnly.WeaponChannels.Add(Tertiary);
	WeaponOnly.BasicWeaponRuntime = WeaponOnly.WeaponChannels[0].Runtime;
	WeaponOnly.RefreshHash();
	if (!TestTrue(TEXT("Weapon-only channel add and cooldown change commit"),
		Simulation->ApplyCommittedAbilityConfig(Group, WeaponOnly)))
	{
		return false;
	}

	FGuLiWingmanNavigationDiagnostics NavigationAfter;
	TestTrue(TEXT("Navigation diagnostics remain available after weapon-only commit"),
		Simulation->GetNavigationDiagnostics(Group, NavigationAfter));
	TestEqual(TEXT("Weapon-only commit preserves the active path"),
		NavigationAfter.ActiveFlightPaths, NavigationBefore.ActiveFlightPaths);
	TestEqual(TEXT("Weapon-only commit preserves the pending request"),
		NavigationAfter.PendingFlightRequests, NavigationBefore.PendingFlightRequests);
	TestEqual(TEXT("Weapon-only commit cancels no navigation request"),
		NavigationAfter.PendingRequestsCancelled, NavigationBefore.PendingRequestsCancelled);
	FGuLiWingmanCandidateBatch AfterWeaponOnlyCandidate;
	TestTrue(TEXT("Weapon-only commit preserves candidate production and accepted pose"),
		Simulation->BuildCandidate(Group, Config.MatchEpoch, 5u, 2u, 34u, AfterWeaponOnlyCandidate));
	TestTrue(TEXT("Weapon-only commit preserves stable member identity"),
		AfterWeaponOnlyCandidate.Samples[0].Wingman == OriginalEmitter);
	TestEqual(TEXT("Weapon-only commit never teleports the member"),
		AfterWeaponOnlyCandidate.Samples[0].PositionCentimeters, OriginalPosition);

	FGuLiWingmanFireIntent Intent;
	TestFalse(TEXT("Changed primary cadence migrates its remaining interval proportionally"),
		Simulation->TryBuildWeaponFireIntent(Group, OriginalEmitter, Config.MatchEpoch, 5u,
			WeaponOnly.WeaponChannels[0], AssignedTarget, 2.0, 35u, true, Intent));
	TestTrue(TEXT("Unchanged sibling slot keeps its original cadence"),
		Simulation->TryBuildWeaponFireIntent(Group, OriginalEmitter, Config.MatchEpoch, 5u,
			WeaponOnly.WeaponChannels[1], AssignedTarget, 2.0, 36u, true, Intent));
	TestEqual(TEXT("Only successful slot requests advance the emitter sequence"),
		Intent.DomainFireSequence, 3u);
	TestFalse(TEXT("Newly added slot receives one complete initial interval"),
		Simulation->TryBuildWeaponFireIntent(Group, OriginalEmitter, Config.MatchEpoch, 5u,
			WeaponOnly.WeaponChannels[2], AssignedTarget, 2.99, 37u, true, Intent));
	TestTrue(TEXT("Newly added slot becomes ready after its complete interval"),
		Simulation->TryBuildWeaponFireIntent(Group, OriginalEmitter, Config.MatchEpoch, 5u,
			WeaponOnly.WeaponChannels[2], AssignedTarget, 3.0, 38u, true, Intent));
	TestEqual(TEXT("New slot shares the same emitter sequence domain"), Intent.DomainFireSequence, 4u);
	TestTrue(TEXT("Migrated primary slot becomes ready at the proportional due time"),
		Simulation->TryBuildWeaponFireIntent(Group, OriginalEmitter, Config.MatchEpoch, 5u,
			WeaponOnly.WeaponChannels[0], AssignedTarget, 4.0, 39u, true, Intent));
	TestEqual(TEXT("Primary slot success advances after sibling traffic"), Intent.DomainFireSequence, 5u);

	FGuLiGroupAbilityConfigSnapshot Replacement = WeaponOnly;
	++Replacement.LoadoutRevision;
	++Replacement.SnapshotRevision;
	Replacement.WeaponChannels[1].SkillId = TEXT("Test.Secondary.Replacement");
	++Replacement.WeaponChannels[1].ProfileRevision;
	++Replacement.WeaponChannels[1].DefinitionRevision;
	Replacement.WeaponChannels[1].DefinitionChecksum ^= 0x55u;
	Replacement.WeaponChannels[1].Runtime.CooldownSeconds = 5.0f;
	Replacement.RefreshHash();
	if (!TestTrue(TEXT("A slot replacement commits without disturbing siblings"),
		Simulation->ApplyCommittedAbilityConfig(Group, Replacement)))
	{
		return false;
	}
	TestFalse(TEXT("Replacement slot receives a complete first interval"),
		Simulation->TryBuildWeaponFireIntent(Group, OriginalEmitter, Config.MatchEpoch, 5u,
			Replacement.WeaponChannels[1], AssignedTarget, 4.0, 40u, true, Intent));
	TestTrue(TEXT("Replacement slot becomes ready at its own due time"),
		Simulation->TryBuildWeaponFireIntent(Group, OriginalEmitter, Config.MatchEpoch, 5u,
			Replacement.WeaponChannels[1], AssignedTarget, 5.0, 41u, true, Intent));
	TestFalse(TEXT("Replacement never shortens the primary sibling cooldown"),
		Simulation->TryBuildWeaponFireIntent(Group, OriginalEmitter, Config.MatchEpoch, 5u,
			Replacement.WeaponChannels[0], AssignedTarget, 5.0, 42u, true, Intent));

	TArray<FGuLiWingmanRosterEntry> Roster;
	Roster.Reserve(InitialCandidate.Samples.Num());
	for (const FGuLiWingmanCandidateSample& Sample : InitialCandidate.Samples)
	{
		FGuLiWingmanRosterEntry& Entry = Roster.AddDefaulted_GetRef();
		Entry.Wingman = Sample.Wingman;
		Entry.WingmanTypeId = Replacement.WingmanTypeId;
	}
	FGuLiWingmanHandle ReplacementEmitter = OriginalEmitter;
	++ReplacementEmitter.EntityGeneration;
	Roster[0].Wingman = ReplacementEmitter;
	if (!TestTrue(TEXT("Roster cut installs a new member generation in the stable slot"),
		Simulation->ApplyRosterCut(Group, Roster)))
	{
		return false;
	}
	FGuLiWingmanCandidateBatch ReplenishedCandidate;
	if (!TestTrue(TEXT("Replenished generation inherits pose but builds a fresh candidate"),
		Simulation->BuildCandidate(Group, Config.MatchEpoch, 5u, 3u, 43u, ReplenishedCandidate)))
	{
		return false;
	}
	TestTrue(TEXT("Stable slot now exposes only the new generation"),
		ReplenishedCandidate.Samples[0].Wingman == ReplacementEmitter);
	TestEqual(TEXT("Replenishment preserves the stable-slot pose"),
		ReplenishedCandidate.Samples[0].PositionCentimeters, OriginalPosition);
	const FGuLiWingmanAcceptedBatch ReplenishedAccepted = MakeAcceptedBatch(ReplenishedCandidate, 0.0);
	TestTrue(TEXT("New generation receives its own accepted source"),
		Simulation->ApplyAcceptedBatch(ReplenishedAccepted));
	TestFalse(TEXT("Old generation cannot reuse the replacement's slot state"),
		Simulation->TryBuildWeaponFireIntent(Group, OriginalEmitter, Config.MatchEpoch, 5u,
			Replacement.WeaponChannels[2], AssignedTarget, 10.0, 44u, true, Intent));
	TestFalse(TEXT("New generation cannot fire before its complete tertiary interval"),
		Simulation->TryBuildWeaponFireIntent(Group, ReplacementEmitter, Config.MatchEpoch, 5u,
			Replacement.WeaponChannels[2], AssignedTarget, 2.99, 45u, true, Intent));
	TestTrue(TEXT("New generation owns a fresh tertiary cooldown and sequence domain"),
		Simulation->TryBuildWeaponFireIntent(Group, ReplacementEmitter, Config.MatchEpoch, 5u,
			Replacement.WeaponChannels[2], AssignedTarget, 3.0, 46u, true, Intent));
	TestEqual(TEXT("Replenished generation starts DomainFireSequence at one"), Intent.DomainFireSequence, 1u);

	TestTrue(TEXT("Cleanup weapon-only simulation fixture"), Simulation->DestroyOwnedGroup(Group));
	return true;
}

#endif
