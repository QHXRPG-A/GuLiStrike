// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Battle/Relay/GuLiWingmanRelayTypes.h"
#include "Gameplay/Wingman/Behavior/GuLiWingmanBehaviorStateTree.h"
#include "Gameplay/Wingman/Behavior/GuLiWingmanGroupBehaviorRunner.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySet.h"
#include "Gameplay/Wingman/Mass/GuLiWingmanMassProcessors.h"
#include "Components/StateTreeComponentSchema.h"
#include "MassProcessingTypes.h"
#include "Misc/AutomationTest.h"
#include "StateTree.h"

#if WITH_EDITOR
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#endif

namespace GuLiWingmanMassSimulationTests
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
			if (!Test.TestNotNull(TEXT("Engine exists for Wingman Mass test World"), GEngine))
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

	bool BuildValidV7AbilityConfig(
		FAutomationTestBase& Test,
		UObject* Outer,
		const FGuLiWingmanGroupHandle& Group,
		FGuLiGroupAbilityConfigSnapshot& OutConfig)
	{
		UGuLiShipAbilitySet* AbilitySet = UGuLiShipAbilitySet::CreateNativeV1Transient(Outer);
		if (!Test.TestNotNull(TEXT("Native v1 Ship ability set exists"), AbilitySet))
		{
			return false;
		}

		TArray<FGuLiShipAbilityGrant> Grants;
		FString Error;
		if (!Test.TestTrue(FString::Printf(TEXT("Native v1 loadout resolves: %s"), *Error),
			AbilitySet->ResolveLoadout(FGuLiShipAbilityLoadoutState::MakeNativeV1(), Grants, &Error)))
		{
			Test.AddError(Error);
			return false;
		}

		OutConfig = FGuLiGroupAbilityConfigSnapshot();
		OutConfig.ShipInstanceId = Group.ShipInstanceId;
		OutConfig.ShipGeneration = Group.ShipGeneration;
		OutConfig.GroupGeneration = Group.GroupGeneration;
		OutConfig.AbilitySetRevision = AbilitySet->Revision;
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
				break;
			case EGuLiShipAbilitySlot::BasicWeapon:
				OutConfig.BasicWeaponAbilityId = Grant.AbilityId;
				OutConfig.BasicWeaponDefinitionRevision = Grant.GetDefinitionRevision();
				OutConfig.BasicWeaponDefinitionChecksum = Grant.GetDefinitionChecksum();
				break;
			case EGuLiShipAbilitySlot::Missile:
				OutConfig.MissileAbilityId = Grant.AbilityId;
				OutConfig.MissileDefinitionRevision = Grant.GetDefinitionRevision();
				OutConfig.MissileDefinitionChecksum = Grant.GetDefinitionChecksum();
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

	void VerifyClientOnlyProcessor(
		FAutomationTestBase& Test,
		const TCHAR* ProcessorName,
		const UMassProcessor* Processor)
	{
		if (!Test.TestNotNull(FString::Printf(TEXT("%s CDO exists"), ProcessorName), Processor))
		{
			return;
		}

		const EProcessorExecutionFlags ExpectedFlags =
			EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone;
		Test.TestEqual(FString::Printf(TEXT("%s has exactly Client and Standalone execution flags"), ProcessorName),
			static_cast<uint8>(Processor->GetExecutionFlags()), static_cast<uint8>(ExpectedFlags));
		Test.TestTrue(FString::Printf(TEXT("%s executes for Client"), ProcessorName),
			Processor->ShouldExecute(EProcessorExecutionFlags::Client));
		Test.TestTrue(FString::Printf(TEXT("%s executes for Standalone"), ProcessorName),
			Processor->ShouldExecute(EProcessorExecutionFlags::Standalone));
		Test.TestFalse(FString::Printf(TEXT("%s never executes for Dedicated Server"), ProcessorName),
			Processor->ShouldExecute(EProcessorExecutionFlags::Server));
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
	FGuLiWingmanMassProcessorExecutionDomainTest,
	"GuLiStrike.Wingman.Mass.ClientOnlyProcessorExecutionDomain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanMassProcessorExecutionDomainTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanMassSimulationTests;
	VerifyClientOnlyProcessor(*this, TEXT("Mode"), GetDefault<UGuLiWingmanModeProcessor>());
	VerifyClientOnlyProcessor(*this, TEXT("FormationGuidance"), GetDefault<UGuLiWingmanFormationGuidanceProcessor>());
	VerifyClientOnlyProcessor(*this, TEXT("Avoidance"), GetDefault<UGuLiWingmanAvoidanceProcessor>());
	VerifyClientOnlyProcessor(*this, TEXT("FlightIntegration"), GetDefault<UGuLiWingmanFlightIntegrationProcessor>());
	const int32 TransformWriterCount =
		(UGuLiWingmanModeProcessor::IsOwnerTransformWriter() ? 1 : 0)
		+ (UGuLiWingmanFormationGuidanceProcessor::IsOwnerTransformWriter() ? 1 : 0)
		+ (UGuLiWingmanAvoidanceProcessor::IsOwnerTransformWriter() ? 1 : 0)
		+ (UGuLiWingmanFlightIntegrationProcessor::IsOwnerTransformWriter() ? 1 : 0);
	TestEqual(TEXT("Exactly one owner Mass processor declares Transform write access"),
		TransformWriterCount, 1);
	TestTrue(TEXT("FlightIntegration is the sole declared owner Transform writer"),
		UGuLiWingmanFlightIntegrationProcessor::IsOwnerTransformWriter());
	TestTrue(TEXT("Formation consumes the navigation fragment as a read-only Guidance input"),
		UGuLiWingmanFormationGuidanceProcessor::ConsumesNavigationGuidanceReadOnly());
	TestTrue(TEXT("Group behavior runner allows autonomous Client"),
		AGuLiWingmanGroupBehaviorRunner::CanRunInNetMode(NM_Client));
	TestTrue(TEXT("Group behavior runner allows Standalone"),
		AGuLiWingmanGroupBehaviorRunner::CanRunInNetMode(NM_Standalone));
	TestTrue(TEXT("Group behavior runner allows Listen host owner simulation"),
		AGuLiWingmanGroupBehaviorRunner::CanRunInNetMode(NM_ListenServer));
	TestFalse(TEXT("Group behavior runner is hard-disabled on Dedicated Server"),
		AGuLiWingmanGroupBehaviorRunner::CanRunInNetMode(NM_DedicatedServer));
	TestFalse(TEXT("Simulation subsystem shares the Dedicated Server execution gate"),
		UGuLiWingmanSimulationSubsystem::IsOwnerSimulationNetMode(NM_DedicatedServer));
	TestFalse(TEXT("Remote client context cannot start a group behavior runner"),
		AGuLiWingmanGroupBehaviorRunner::CanRunForOwnerContext(NM_Client, false));
	TestFalse(TEXT("Dedicated context cannot start a runner even if marked owner"),
		AGuLiWingmanGroupBehaviorRunner::CanRunForOwnerContext(NM_DedicatedServer, true));
	TestTrue(TEXT("Autonomous local client context can start a runner"),
		AGuLiWingmanGroupBehaviorRunner::CanRunForOwnerContext(NM_Client, true));
	TestFalse(TEXT("StateTree policy task never writes Mass Transform"),
		FGuLiWingmanBehaviorPolicyTask::WritesMassTransform());
	TestFalse(TEXT("StateTree policy task never writes flight velocity"),
		FGuLiWingmanBehaviorPolicyTask::WritesFlightVelocity());
	TestFalse(TEXT("StateTree policy task never writes health or damage"),
		FGuLiWingmanBehaviorPolicyTask::WritesHealthOrDamage());
	TestTrue(TEXT("StateTree policy requests existing Guidance through FlightMode"),
		FGuLiWingmanBehaviorPolicyTask::RequestsGuidanceThroughFlightMode());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanAuthoredStateTreeAssetContractTest,
	"GuLiStrike.Wingman.StateTree.AuthoredAssetContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanAuthoredStateTreeAssetContractTest::RunTest(const FString& Parameters)
{
	constexpr TCHAR StateTreeObjectPath[] =
		TEXT("/Game/GuLiStrike/Wingman/ST_WingmanGroupBehavior.ST_WingmanGroupBehavior");
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
		TEXT("JoiningEscort"), TEXT("EscortOrbit"), TEXT("EmergencyAvoid"),
		TEXT("OwnerUnavailable"), TEXT("Dead")};
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

	int32 PolicyTaskCount = 0;
	int32 PolicyConditionCount = 0;
	for (const FConstStructView Node : StateTree->GetNodes())
	{
		PolicyTaskCount += Node.GetScriptStruct() == FGuLiWingmanBehaviorPolicyTask::StaticStruct() ? 1 : 0;
		PolicyConditionCount +=
			Node.GetScriptStruct() == FGuLiWingmanBehaviorPolicyCondition::StaticStruct() ? 1 : 0;
	}
	TestEqual(TEXT("Compiled asset contains one policy task per named state"), PolicyTaskCount, 5);
	TestEqual(TEXT("Compiled asset contains one entry condition per named state"), PolicyConditionCount, 5);

#if WITH_EDITOR
	const UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
	if (TestNotNull(TEXT("Authored asset retains editor graph data"), EditorData)
		&& TestEqual(TEXT("Editor graph contains one root"), EditorData->SubTrees.Num(), 1)
		&& TestNotNull(TEXT("Editor root exists"), EditorData->SubTrees[0].Get()))
	{
		const UStateTreeState* Root = EditorData->SubTrees[0];
		TestEqual(TEXT("Root exposes five event signal transitions"), Root->Transitions.Num(), 5);
		TestEqual(TEXT("Root exposes five conditioned policy states"), Root->Children.Num(), 5);
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
	using namespace GuLiWingmanMassSimulationTests;
	FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	UGuLiWingmanSimulationSubsystem* Simulation =
		Fixture.World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	if (!TestNotNull(TEXT("Standalone owner simulation exists"), Simulation)) return false;
	Simulation->SetNavigationRequirementBypassForTests(true);
	UStateTree* StateTree = LoadObject<UStateTree>(nullptr,
		TEXT("/Game/GuLiStrike/Wingman/ST_WingmanGroupBehavior.ST_WingmanGroupBehavior"));
	if (!TestNotNull(TEXT("Runtime test authored StateTree loads"), StateTree)) return false;
	Simulation->SetGroupBehaviorStateTreeForTests(StateTree);

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
	TestTrue(TEXT("UStateTreeComponent, not fallback, drives the local owner group"),
		Simulation->IsGroupUsingStateTree(Group));
	TestFalse(TEXT("Controlled fallback stays disabled for a valid compiled asset"),
		Simulation->IsGroupUsingControlledFallback(Group));
	const AGuLiWingmanGroupBehaviorRunner* Runner = Simulation->GetBehaviorRunnerForTests(Group);
	if (TestNotNull(TEXT("Group owns a transient StateTree runner"), Runner))
	{
		TestEqual(TEXT("Initial authored state is stable EscortOrbit"),
			Runner->GetObservedPolicy(), EGuLiWingmanBehaviorPolicy::EscortOrbit);
		TestTrue(TEXT("Authored StateTree task entered and applied its mode policy"),
			Runner->GetStateTreePolicyApplyCount() > 0u);
	}
	TestTrue(TEXT("Runtime owner group cleanup succeeds"), Simulation->DestroyOwnedGroup(Group));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanOwnerMassCandidateLifecycleTest,
	"GuLiStrike.Wingman.Mass.OwnerGroupCandidateLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanOwnerMassCandidateLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanMassSimulationTests;
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
	Simulation->SetGroupBehaviorStateTreeForTests(nullptr);
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
	TestEqual(TEXT("Rejected navigation gate creates no Mass entities"),
		Simulation->GetTotalOwnedEntityCount(), 0);
	Simulation->SetNavigationRequirementBypassForTests(true);
	if (!TestTrue(TEXT("Valid v7 config creates one complete owner group"),
		Simulation->CreateOrResetOwnedGroup(
			Group, AbilityConfig, FTransform::Identity, FVector::ZeroVector, CarrierSource)))
	{
		return false;
	}
	TestEqual(TEXT("Owner group contains exactly 25 Mass entities"),
		Simulation->GetOwnedEntityCount(Group), static_cast<int32>(GULI_WINGMAN_GROUP_SIZE));
	TestEqual(TEXT("No hidden extra owner entities were created"),
		Simulation->GetTotalOwnedEntityCount(), static_cast<int32>(GULI_WINGMAN_GROUP_SIZE));
	TestTrue(TEXT("No authored StateTree selects the explicit controlled fallback"),
		Simulation->IsGroupUsingControlledFallback(Group));
	TestFalse(TEXT("Fallback group does not claim a running StateTree"),
		Simulation->IsGroupUsingStateTree(Group));
	TestTrue(TEXT("Navigation coordination ticks independently of the fallback/StateTree policy choice"),
		Simulation->IsGroupNavigationCoordinationEnabled(Group));

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

	TestTrue(TEXT("Invalidation clears group ability authorization"),
		Simulation->InvalidateOwnedGroupAbilities(Group));
	TestEqual(TEXT("Invalidation preserves the 25 local entities for presentation/fade-out"),
		Simulation->GetOwnedEntityCount(Group), static_cast<int32>(GULI_WINGMAN_GROUP_SIZE));

	FGuLiWingmanCandidateBatch CandidateAfterInvalidation;
	CandidateAfterInvalidation.Samples.AddDefaulted();
	TestFalse(TEXT("An invalidated group cannot build another Candidate"),
		Simulation->BuildCandidate(Group, 7u, 5u, 2u, 32u, CandidateAfterInvalidation));
	TestEqual(TEXT("A failed post-invalidation build leaves no stale samples"),
		CandidateAfterInvalidation.Samples.Num(), 0);

	TestTrue(TEXT("Test group cleanup succeeds"), Simulation->DestroyOwnedGroup(Group));
	TestEqual(TEXT("Test group cleanup removes every owner entity"),
		Simulation->GetTotalOwnedEntityCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanFlightNavigationLifecycleTest,
	"GuLiStrike.Wingman.Mass.FlightNavigationAsyncStateLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanFlightNavigationLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanMassSimulationTests;
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
	if (!TestTrue(TEXT("Navigation lifecycle group creates 25 owner entities"),
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
	TestEqual(TEXT("Path publication never teleports the first owner entity"),
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
	TestEqual(TEXT("No owner entities survive group destruction"),
		Simulation->GetTotalOwnedEntityCount(), 0);
	FGuLiWingmanNavigationDiagnostics DestroyedDiagnostics;
	TestFalse(TEXT("Destroyed group has no navigation runtime"),
		Simulation->GetNavigationDiagnostics(Group, DestroyedDiagnostics));
	TestEqual(TEXT("Group destruction cancels its final pending request token"),
		DestroyedDiagnostics.PendingRequestsCancelled, static_cast<uint64>(2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanBasicWeaponIntentStateTest,
	"GuLiStrike.Wingman.Mass.BasicWeaponIndependentAcceptedState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanBasicWeaponIntentStateTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanMassSimulationTests;
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
	FGuLiWingmanFireIntent NoAcceptedIntent;
	TestFalse(TEXT("Emitter without a confirmed accepted state cannot fire"),
		Simulation->TryBuildBasicFireIntent(Group, Candidate.Samples[0].Wingman, 7u, 5u,
			Target, FVector(1000000.0, 0.0, 0.0), 10.0, 0.5, 32u, true, NoAcceptedIntent));

	const FGuLiWingmanAcceptedBatch Accepted = MakeAcceptedBatch(Candidate, 2.0);
	if (!TestTrue(TEXT("Confirmed accepted batch updates emitter source refs"),
		Simulation->ApplyAcceptedBatch(Accepted))) return false;

	TSet<FGuLiWingmanHandle> Emitters;
	for (const FGuLiWingmanCandidateSample& Sample : Candidate.Samples)
	{
		FGuLiWingmanFireIntent Intent;
		if (!TestTrue(TEXT("Each accepted emitter independently produces an intent"),
			Simulation->TryBuildBasicFireIntent(Group, Sample.Wingman, 7u, 5u,
				Target, FVector(1000000.0, 0.0, 0.0), 10.0, 0.5, 32u, true, Intent)))
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
			Target, FVector(1000000.0, 0.0, 0.0), 10.25, 0.5, 33u, true, CoolingDownIntent));
	FGuLiWingmanFireIntent SecondIntent;
	TestTrue(TEXT("Emitter advances its own domain after its cooldown"),
		Simulation->TryBuildBasicFireIntent(Group, Candidate.Samples[0].Wingman, 7u, 5u,
			Target, FVector(1000000.0, 0.0, 0.0), 10.5, 0.5, 34u, true, SecondIntent));
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
			Target, FVector(1000000.0, 0.0, 0.0), 20.0, 0.5, 35u, true, OldSourceIntent));

	TestTrue(TEXT("Cleanup 25-emitter test group"), Simulation->DestroyOwnedGroup(Group));
	return true;
}

#endif
