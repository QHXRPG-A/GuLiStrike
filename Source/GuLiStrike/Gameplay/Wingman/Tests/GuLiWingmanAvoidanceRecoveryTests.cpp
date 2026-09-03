// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"

#include "Battle/Relay/GuLiWingmanRelayTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Gameplay/Wingman/Behavior/GuLiWingmanBehaviorStateTree.h"
#include "Gameplay/Wingman/Mass/GuLiWingmanMassFragments.h"
#include "Gameplay/Wingman/Mass/GuLiWingmanMassProcessors.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "Misc/AutomationTest.h"

namespace GuLiWingmanAvoidanceRecoveryTests
{
	struct FScopedFlightNavSegmentValidator
	{
		explicit FScopedFlightNavSegmentValidator(
			UWorld* TransientTestWorld,
			GuLiWingmanAvoidance::FFlightNavSegmentValidatorForTests Validator)
		{
			GuLiWingmanAvoidance::SetFlightNavSegmentValidatorForTests(
				TransientTestWorld, MoveTemp(Validator));
		}

		~FScopedFlightNavSegmentValidator()
		{
			GuLiWingmanAvoidance::ResetFlightNavSegmentValidatorForTests();
		}
	};

	struct FScopedWorldObstacleSegmentProbe
	{
		explicit FScopedWorldObstacleSegmentProbe(
			UWorld* TransientTestWorld,
			GuLiWingmanAvoidance::FWorldObstacleSegmentProbeForTests Probe)
		{
			GuLiWingmanAvoidance::SetWorldObstacleSegmentProbeForTests(
				TransientTestWorld, MoveTemp(Probe));
		}

		~FScopedWorldObstacleSegmentProbe()
		{
			GuLiWingmanAvoidance::ResetWorldObstacleSegmentProbeForTests();
		}
	};

	struct FTransientGameWorldFixture
	{
		UWorld* World = nullptr;
		bool bRegistered = false;

		~FTransientGameWorldFixture()
		{
			if (!World) return;
			World->DestroyWorld(false);
			if (bRegistered && GEngine) GEngine->DestroyWorldContext(World);
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Engine exists"), GEngine)) return false;
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("Transient owner World exists"), World)) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bRegistered = true;
			return Test.TestTrue(TEXT("Transient World is standalone"), World->GetNetMode() == NM_Standalone);
		}
	};

	FGuLiWingmanGroupHandle MakeGroup(const uint32 Seed)
	{
		FGuLiWingmanGroupHandle Group;
		Group.ShipInstanceId = FGuid(Seed, Seed + 1u, Seed + 2u, Seed + 3u);
		Group.ShipGeneration = 2u;
		Group.GroupGeneration = 3u;
		return Group;
	}

	FGuLiWingmanHandle MakeWingman(
		const FGuLiWingmanGroupHandle& Group,
		const uint8 FlightIndex,
		const uint8 MemberIndex)
	{
		FGuLiWingmanHandle Handle;
		Handle.Flight.Group = Group;
		Handle.Flight.FlightIndex = FlightIndex;
		Handle.MemberIndex = MemberIndex;
		Handle.EntityGeneration = 1u;
		return Handle;
	}

	FGuLiGroupAbilityConfigSnapshot MakeConfig(const FGuLiWingmanGroupHandle& Group)
	{
		FGuLiGroupAbilityConfigSnapshot Config;
		Config.ShipInstanceId = Group.ShipInstanceId;
		Config.ShipGeneration = Group.ShipGeneration;
		Config.GroupGeneration = Group.GroupGeneration;
		Config.AbilitySetRevision = 1u;
		Config.SnapshotRevision = 1u;
		Config.bGroupAbilitiesValid = true;
		Config.FormationAbilityId = TAG_GuLi_ShipAbility_Formation_DoubleRing;
		Config.BasicWeaponAbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
		Config.MissileAbilityId = TAG_GuLi_ShipAbility_Weapon_Missile_Salvo;
		Config.FormationDefinitionRevision = 1u;
		Config.FormationDefinitionChecksum = 0x1010101010101010ull;
		Config.BasicWeaponDefinitionRevision = 1u;
		Config.BasicWeaponDefinitionChecksum = 0x2020202020202020ull;
		Config.MissileDefinitionRevision = 1u;
		Config.MissileDefinitionChecksum = 0x3030303030303030ull;
		Config.FormationCommandRevision = 1u;
		Config.EffectiveClientSimTick = 30u;
		Config.RefreshHash();
		return Config;
	}

	TMap<FGuLiWingmanHandle, FVector> ComputeBruteForce(
		const TArray<GuLiWingmanAvoidance::FSpatialSample>& Samples)
	{
		TMap<FGuLiWingmanHandle, FVector> Result;
		for (int32 LeftIndex = 0; LeftIndex < Samples.Num(); ++LeftIndex)
		{
			const GuLiWingmanAvoidance::FSpatialSample& Left = Samples[LeftIndex];
			FVector Acceleration = FVector::ZeroVector;
			if (Left.bAlive && Left.SeparationRadiusCentimeters > UE_SMALL_NUMBER)
			{
				for (int32 RightIndex = 0; RightIndex < Samples.Num(); ++RightIndex)
				{
					const GuLiWingmanAvoidance::FSpatialSample& Right = Samples[RightIndex];
					if (LeftIndex == RightIndex || !Right.bAlive
						|| Left.Handle.Flight.Group != Right.Handle.Flight.Group)
					{
						continue;
					}
					FVector Delta = Left.Position - Right.Position;
					const double Distance = Delta.Size();
					if (Distance >= Left.SeparationRadiusCentimeters) continue;
					if (Distance <= UE_DOUBLE_SMALL_NUMBER)
					{
						const float Angle = static_cast<float>(Left.Handle.GetGroupMemberIndex())
							* UE_TWO_PI / static_cast<float>(GULI_WINGMAN_GROUP_SIZE);
						Delta = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.25f).GetSafeNormal();
					}
					else
					{
						Delta /= Distance;
					}
					Acceleration += Delta * Left.MaximumAccelerationCentimetersPerSecondSquared
						* static_cast<float>(1.0 - Distance / Left.SeparationRadiusCentimeters);
				}
			}
			Result.Add(Left.Handle, Acceleration.GetClampedToMaxSize(
				Left.MaximumAccelerationCentimetersPerSecondSquared));
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanSpatialHashEquivalenceTest,
	"GuLiStrike.Wingman.Avoidance.SpatialHashEquivalentAndGroupScoped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanSpatialHashEquivalenceTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanAvoidanceRecoveryTests;
	const FGuLiWingmanGroupHandle GroupA = MakeGroup(0x1000u);
	const FGuLiWingmanGroupHandle GroupB = MakeGroup(0x2000u);
	TArray<GuLiWingmanAvoidance::FSpatialSample> Samples;
	for (uint8 Index = 0u; Index < 25u; ++Index)
	{
		GuLiWingmanAvoidance::FSpatialSample& Sample = Samples.AddDefaulted_GetRef();
		Sample.Handle = MakeWingman(GroupA, Index / 5u, Index % 5u);
		Sample.Position = FVector(
			static_cast<double>(Index % 5u) * 850.0,
			static_cast<double>((Index / 5u) % 5u) * 850.0,
			static_cast<double>(Index % 3u) * 300.0);
		Sample.SeparationRadiusCentimeters = 1000.0f;
		Sample.MaximumAccelerationCentimetersPerSecondSquared = 800.0f;
		Sample.bAlive = true;
	}
	// This other group deliberately overlaps GroupA. It must never contribute.
	GuLiWingmanAvoidance::FSpatialSample& CrossGroup = Samples.AddDefaulted_GetRef();
	CrossGroup.Handle = MakeWingman(GroupB, 0u, 0u);
	CrossGroup.Position = Samples[0].Position + FVector(1.0, 0.0, 0.0);
	CrossGroup.SeparationRadiusCentimeters = 1000.0f;
	CrossGroup.MaximumAccelerationCentimetersPerSecondSquared = 800.0f;
	CrossGroup.bAlive = true;

	const TMap<FGuLiWingmanHandle, FVector> BruteForce = ComputeBruteForce(Samples);
	TArray<GuLiWingmanAvoidance::FSpatialResult> Hashed;
	GuLiWingmanAvoidance::ComputeSpatialHashSeparation(Samples, Hashed);
	TestEqual(TEXT("Hash returns one stable result per sample"), Hashed.Num(), Samples.Num());
	uint64 HashedNeighborTests = 0u;
	for (const GuLiWingmanAvoidance::FSpatialResult& Result : Hashed)
	{
		const FVector* Expected = BruteForce.Find(Result.Handle);
		if (TestNotNull(TEXT("Brute-force result exists for stable handle"), Expected))
		{
			TestTrue(TEXT("27-cell hash matches group-scoped brute force"),
				Result.Acceleration.Equals(*Expected, 0.01f));
		}
		HashedNeighborTests += Result.NeighborTests;
		TestEqual(TEXT("Grid cell size follows the group maximum separation radius"),
			Result.CellSizeCentimeters, 1000.0f);
	}
	TestTrue(TEXT("Sparse spatial hash examines fewer pairs than all-group O(N^2)"),
		HashedNeighborTests < static_cast<uint64>(Samples.Num() * (Samples.Num() - 1)));
	const GuLiWingmanAvoidance::FSpatialResult* CrossGroupResult = Hashed.FindByPredicate(
		[&CrossGroup](const GuLiWingmanAvoidance::FSpatialResult& Result)
		{
			return Result.Handle == CrossGroup.Handle;
		});
	if (TestNotNull(TEXT("Overlapping cross-group sample has a result"), CrossGroupResult))
	{
		TestTrue(TEXT("Overlapping group is isolated from separation"),
			CrossGroupResult->Acceleration.IsNearlyZero());
	}

	Algo::Reverse(Samples);
	TArray<GuLiWingmanAvoidance::FSpatialResult> ReversedInputResults;
	GuLiWingmanAvoidance::ComputeSpatialHashSeparation(Samples, ReversedInputResults);
	TestEqual(TEXT("Stable sorting makes input order irrelevant"), ReversedInputResults.Num(), Hashed.Num());
	for (int32 Index = 0; Index < Hashed.Num() && Index < ReversedInputResults.Num(); ++Index)
	{
		TestTrue(TEXT("Stable result handle order is deterministic"),
			Hashed[Index].Handle == ReversedInputResults[Index].Handle);
		TestTrue(TEXT("Stable accumulation is deterministic"),
			Hashed[Index].Acceleration.Equals(ReversedInputResults[Index].Acceleration, UE_KINDA_SMALL_NUMBER));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanObstacleHeadingSelectionTest,
	"GuLiStrike.Wingman.Avoidance.StaticDynamicHeadingSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanObstacleHeadingSelectionTest::RunTest(const FString& Parameters)
{
	FGuLiWingmanFormationRuntimeConfig Tuning;
	Tuning.MinimumSpeedCentimetersPerSecond = 100.0f;
	Tuning.AgentRadiusCentimeters = 50.0f;
	Tuning.ObstacleLookAheadCentimeters = 1000.0f;
	Tuning.MaximumTurnRateDegreesPerSecond = 60.0f;
	const auto StaticDynamicProbe = [](const FVector& Direction, const float Distance)
	{
		GuLiWingmanAvoidance::FHeadingProbeResult Result;
		Result.ClearanceCentimeters = Distance;
		if (Direction.X > 0.8f)
		{
			Result.ClearanceCentimeters = 40.0f;
			Result.bWorldStatic = true;
		}
		else if (Direction.Y > 0.2f)
		{
			Result.ClearanceCentimeters = 50.0f;
			Result.bWorldDynamic = true;
		}
		return Result;
	};
	const GuLiWingmanAvoidance::FHeadingSelection Selection =
		GuLiWingmanAvoidance::SelectSafeHeading(
			FVector::ForwardVector, FVector::ForwardVector, 500.0f, Tuning, StaticDynamicProbe);
	TestTrue(TEXT("A complete-lookahead safe alternative is selected"), Selection.bFullLookAheadSafe);
	TestTrue(TEXT("The published heading is also immediately safe"), Selection.bImmediateStepSafe);
	TestTrue(TEXT("Forward static obstacle was observed"), Selection.bEncounteredWorldStatic);
	TestTrue(TEXT("Right-side dynamic obstacle was observed during deterministic sampling"),
		Selection.bEncounteredWorldDynamic);
	TestTrue(TEXT("Stable scoring selects a finite non-forward escape"),
		!Selection.Direction.ContainsNaN() && Selection.Direction.X < 0.8f);
	const GuLiWingmanAvoidance::FHeadingSelection Repeat =
		GuLiWingmanAvoidance::SelectSafeHeading(
			FVector::ForwardVector, FVector::ForwardVector, 500.0f, Tuning, StaticDynamicProbe);
	TestTrue(TEXT("Heading score tie-break is deterministic"),
		Selection.Direction.Equals(Repeat.Direction, UE_KINDA_SMALL_NUMBER)
		&& Selection.ProbeCount == Repeat.ProbeCount);

	const auto ImmediateOnlyProbe = [](const FVector&, const float)
	{
		GuLiWingmanAvoidance::FHeadingProbeResult Result;
		Result.ClearanceCentimeters = 200.0f;
		Result.bWorldStatic = true;
		return Result;
	};
	const GuLiWingmanAvoidance::FHeadingSelection Immediate =
		GuLiWingmanAvoidance::SelectSafeHeading(
			FVector::ForwardVector, FVector::ForwardVector, 500.0f, Tuning, ImmediateOnlyProbe);
	TestFalse(TEXT("Short fallback does not claim full-lookahead safety"), Immediate.bFullLookAheadSafe);
	TestTrue(TEXT("Short fallback is published only when swept clearance covers its movement horizon"),
		Immediate.bImmediateStepSafe);

	const auto NoSafeProbe = [](const FVector&, const float)
	{
		GuLiWingmanAvoidance::FHeadingProbeResult Result;
		Result.ClearanceCentimeters = 25.0f;
		Result.bWorldDynamic = true;
		return Result;
	};
	const GuLiWingmanAvoidance::FHeadingSelection None =
		GuLiWingmanAvoidance::SelectSafeHeading(
			FVector::ForwardVector, FVector::ForwardVector, 500.0f, Tuning, NoSafeProbe);
	TestFalse(TEXT("An unswept immediate direction is never published"), None.bImmediateStepSafe);

	const auto BoundaryTurnProbe = [](const FVector& Direction, const float Distance)
	{
		GuLiWingmanAvoidance::FHeadingProbeResult Result;
		Result.ClearanceCentimeters = Distance;
		// Model a baked-navigation boundary ahead and to starboard. Only a port turn remains in bounds.
		Result.bFlightNavSegmentValid = Direction.Y < -0.2f;
		return Result;
	};
	const GuLiWingmanAvoidance::FHeadingSelection BoundaryTurn =
		GuLiWingmanAvoidance::SelectSafeHeading(
			FVector::ForwardVector, FVector::ForwardVector, 500.0f, Tuning, BoundaryTurnProbe);
	TestTrue(TEXT("FlightNav boundary participates in deterministic heading rejection"),
		BoundaryTurn.bEncounteredFlightNavBoundary);
	TestTrue(TEXT("A full-lookahead heading inside FlightNav is selected"),
		BoundaryTurn.bFullLookAheadSafe);
	TestTrue(TEXT("The FlightNav-safe heading also has a validated immediate step"),
		BoundaryTurn.bImmediateStepSafe);
	TestTrue(TEXT("Boundary sampling selects the only in-bounds turn"),
		BoundaryTurn.Direction.Y < -0.2f);

	const auto BoundaryEverywhereProbe = [](const FVector&, const float Distance)
	{
		GuLiWingmanAvoidance::FHeadingProbeResult Result;
		Result.ClearanceCentimeters = Distance;
		Result.bFlightNavSegmentValid = false;
		return Result;
	};
	const GuLiWingmanAvoidance::FHeadingSelection BoundaryEverywhere =
		GuLiWingmanAvoidance::SelectSafeHeading(
			FVector::ForwardVector, FVector::ForwardVector, 500.0f, Tuning, BoundaryEverywhereProbe);
	TestTrue(TEXT("An all-directions FlightNav boundary is diagnosed"),
		BoundaryEverywhere.bEncounteredFlightNavBoundary);
	TestFalse(TEXT("No out-of-navigation full-lookahead heading is accepted"),
		BoundaryEverywhere.bFullLookAheadSafe);
	TestFalse(TEXT("No out-of-navigation short fallback is accepted"),
		BoundaryEverywhere.bImmediateStepSafe);

	FGuLiWingmanAvoidanceFragment PersistedThreats;
	GuLiWingmanAvoidance::AccumulateHeadingThreatsForTests(PersistedThreats, BoundaryEverywhere);
	TestTrue(TEXT("Normal heading boundary persists as an immediate Mass threat signal"),
		PersistedThreats.bDetectedFlightNavBoundary);
	GuLiWingmanAvoidance::FHeadingSelection RecoveryThreat;
	RecoveryThreat.bEncounteredWorldDynamic = true;
	GuLiWingmanAvoidance::AccumulateHeadingThreatsForTests(PersistedThreats, RecoveryThreat);
	TestTrue(TEXT("Recovery heading diagnostics accumulate without clearing the normal FlightNav threat"),
		PersistedThreats.bDetectedFlightNavBoundary && PersistedThreats.bDetectedWorldDynamic);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanControlledRecoveryClockTest,
	"GuLiStrike.Wingman.Avoidance.ThreeSecondSafePointRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanControlledRecoveryClockTest::RunTest(const FString& Parameters)
{
	GuLiWingmanAvoidance::FRecoveryClockState State;
	for (int32 Step = 0; Step < 11; ++Step)
	{
		GuLiWingmanAvoidance::AdvanceRecoveryClock(State, 0.25f, false, false);
	}
	TestFalse(TEXT("2.75 seconds blocked is below the recovery threshold"), State.bControlledRecovery);
	GuLiWingmanAvoidance::AdvanceRecoveryClock(State, 0.25f, false, false);
	TestTrue(TEXT("Exactly three seconds blocked enters controlled recovery"), State.bControlledRecovery);
	TestEqual(TEXT("Entry timer reaches the defined threshold"), State.ConsecutiveBlockedSeconds, 3.0f);

	for (int32 Step = 0; Step < 4; ++Step)
	{
		GuLiWingmanAvoidance::AdvanceRecoveryClock(State, 0.25f, true, false);
	}
	TestTrue(TEXT("Clear obstacle probes alone cannot exit without a verified FlightNav reopen"),
		State.bControlledRecovery);
	GuLiWingmanAvoidance::AdvanceRecoveryClock(State, 0.25f, true, true);
	TestTrue(TEXT("One verified clear sample is held for hysteresis"), State.bControlledRecovery);
	GuLiWingmanAvoidance::AdvanceRecoveryClock(State, 0.25f, true, true);
	TestFalse(TEXT("Half a second of verified clear path exits recovery"), State.bControlledRecovery);

	const FVector Position(1000.0, 0.0, 0.0);
	const FVector SafePoint = FVector::ZeroVector;
	const FVector Orbit = GuLiWingmanAvoidance::BuildRecoveryOrbitDirection(
		Position, FVector::RightVector, SafePoint, 1000.0f);
	TestTrue(TEXT("Safe-point holding direction is finite and nonzero"),
		!Orbit.ContainsNaN() && Orbit.IsNormalized());
	TestTrue(TEXT("Safe-point recovery orbits instead of flying straight through the anchor"),
		FMath::Abs(FVector::DotProduct(Orbit, (SafePoint - Position).GetSafeNormal())) < 0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanBehaviorPolicyEmergencySignalTest,
	"GuLiStrike.Wingman.StateTree.NormalSteeringIsNotEmergency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanBehaviorPolicyEmergencySignalTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanAvoidanceRecoveryTests;
	FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	UGuLiWingmanSimulationSubsystem* Simulation =
		Fixture.World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	if (!TestNotNull(TEXT("Owner simulation exists"), Simulation)) return false;
	Simulation->SetNavigationRequirementBypassForTests(true);
	Simulation->SetGroupBehaviorStateTreeForTests(nullptr);

	const FGuLiWingmanGroupHandle Group = MakeGroup(0x2a00u);
	const FGuLiGroupAbilityConfigSnapshot Config = MakeConfig(Group);
	FGuLiCarrierSourceRef CarrierSource;
	CarrierSource.CanonicalEpoch = 1u;
	CarrierSource.MoveRevision = 1u;
	if (!TestTrue(TEXT("Create owner group for behavior policy test"),
		Simulation->CreateOrResetOwnedGroup(
			Group, Config, FTransform::Identity, FVector::ZeroVector, CarrierSource)))
	{
		return false;
	}

	const auto TestEveryMode = [this, Simulation, &Group](
		const TCHAR* Description, const EGuLiWingmanFlightMode ExpectedMode, const uint32 Sequence)
	{
		FGuLiWingmanCandidateBatch Candidate;
		if (!TestTrue(FString::Printf(TEXT("%s candidate builds"), Description),
			Simulation->BuildCandidate(Group, 1u, 1u, Sequence, Sequence, Candidate)))
		{
			return;
		}
		TestEqual(FString::Printf(TEXT("%s includes the full group"), Description),
			Candidate.Samples.Num(), GULI_WINGMAN_GROUP_SIZE);
		for (const FGuLiWingmanCandidateSample& Sample : Candidate.Samples)
		{
			TestEqual(Description, Sample.FlightMode, static_cast<uint8>(ExpectedMode));
		}
	};

	const FVector NormalSteeringAcceleration(450.0, -120.0, 35.0);
	TestTrue(TEXT("Inject ordinary non-zero formation steering"),
		Simulation->SetGroupBehaviorPolicyInputsForTests(
			Group, NormalSteeringAcceleration, 0.0f, false, false, false, false));
	TestEqual(TEXT("Ordinary steering remains stable escort policy"),
		Simulation->EvaluateBehaviorPolicy(Group), EGuLiWingmanBehaviorPolicy::EscortOrbit);
	TestTrue(TEXT("Stable escort policy applies while carrier is stationary"),
		Simulation->ApplyStateTreePolicy(Group, EGuLiWingmanBehaviorPolicy::EscortOrbit, 0.2f));
	TestEveryMode(TEXT("Ordinary steering preserves Orbit"), EGuLiWingmanFlightMode::Orbit, 1u);

	TestTrue(TEXT("Carrier motion updates without changing avoidance observations"),
		Simulation->UpdateOwnedGroupCarrier(
			Group, FTransform::Identity, FVector(1000.0, 0.0, 0.0), CarrierSource));
	TestEqual(TEXT("Ordinary steering with a moving carrier remains stable escort policy"),
		Simulation->EvaluateBehaviorPolicy(Group), EGuLiWingmanBehaviorPolicy::EscortOrbit);
	TestTrue(TEXT("Stable escort policy applies while carrier is moving"),
		Simulation->ApplyStateTreePolicy(Group, EGuLiWingmanBehaviorPolicy::EscortOrbit, 0.2f));
	TestEveryMode(TEXT("Ordinary steering preserves Follow"), EGuLiWingmanFlightMode::Follow, 2u);

	TestTrue(TEXT("Inject a genuine blocked interval"),
		Simulation->SetGroupBehaviorPolicyInputsForTests(
			Group, NormalSteeringAcceleration, 0.1f, false, false, false, false));
	TestEqual(TEXT("A genuine block selects emergency avoidance"),
		Simulation->EvaluateBehaviorPolicy(Group), EGuLiWingmanBehaviorPolicy::EmergencyAvoid);
	TestTrue(TEXT("Emergency policy applies for a genuine block"),
		Simulation->ApplyStateTreePolicy(Group, EGuLiWingmanBehaviorPolicy::EmergencyAvoid, 0.2f));
	TestEveryMode(TEXT("A genuine block enters Recover"), EGuLiWingmanFlightMode::Recover, 3u);

	TestTrue(TEXT("Inject controlled recovery without a current blocked interval"),
		Simulation->SetGroupBehaviorPolicyInputsForTests(
			Group, FVector::ZeroVector, 0.0f, true, false, false, false));
	TestEqual(TEXT("Controlled recovery remains emergency avoidance"),
		Simulation->EvaluateBehaviorPolicy(Group), EGuLiWingmanBehaviorPolicy::EmergencyAvoid);
	TestTrue(TEXT("Emergency policy applies during controlled recovery"),
		Simulation->ApplyStateTreePolicy(Group, EGuLiWingmanBehaviorPolicy::EmergencyAvoid, 0.2f));
	TestEveryMode(TEXT("Controlled recovery preserves Recover"), EGuLiWingmanFlightMode::Recover, 4u);

	TestTrue(TEXT("Inject an explicit world threat"),
		Simulation->SetGroupBehaviorPolicyInputsForTests(
			Group, FVector::ZeroVector, 0.0f, false, true, false, false));
	TestEqual(TEXT("Explicit world threat remains emergency avoidance"),
		Simulation->EvaluateBehaviorPolicy(Group), EGuLiWingmanBehaviorPolicy::EmergencyAvoid);

	TestTrue(TEXT("Inject an explicit navigation safe-fallback threat"),
		Simulation->SetGroupBehaviorPolicyInputsForTests(
			Group, FVector::ZeroVector, 0.0f, false, false, false, true));
	TestEqual(TEXT("Navigation safe fallback remains emergency avoidance"),
		Simulation->EvaluateBehaviorPolicy(Group), EGuLiWingmanBehaviorPolicy::EmergencyAvoid);

	TestTrue(TEXT("Cleanup behavior policy owner group"), Simulation->DestroyOwnedGroup(Group));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanAvoidanceMassWriterAndDiagnosticsTest,
	"GuLiStrike.Wingman.Avoidance.OwnerOnlyDiagnosticsAndSoleIntegrationWriter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanAvoidanceMassWriterAndDiagnosticsTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanAvoidanceRecoveryTests;
	FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	FScopedFlightNavSegmentValidator AllowAllNavigationSegments(
		Fixture.World,
		[](const FVector&, const FVector&, const float)
		{
			return true;
		});
	UGuLiWingmanSimulationSubsystem* Simulation =
		Fixture.World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	UMassEntitySubsystem* MassSubsystem = Fixture.World->GetSubsystem<UMassEntitySubsystem>();
	if (!TestNotNull(TEXT("Owner simulation exists"), Simulation)
		|| !TestNotNull(TEXT("Mass subsystem exists"), MassSubsystem)) return false;
	Simulation->SetNavigationRequirementBypassForTests(true);
	Simulation->SetGroupBehaviorStateTreeForTests(nullptr);

	const FGuLiWingmanGroupHandle Group = MakeGroup(0x3000u);
	const FGuLiGroupAbilityConfigSnapshot Config = MakeConfig(Group);
	FGuLiCarrierSourceRef CarrierSource;
	CarrierSource.CanonicalEpoch = 1u;
	CarrierSource.MoveRevision = 1u;
	if (!TestTrue(TEXT("Create 25-member owner group"), Simulation->CreateOrResetOwnedGroup(
		Group, Config, FTransform::Identity, FVector::ZeroVector, CarrierSource))) return false;

	FGuLiWingmanCandidateBatch Before;
	if (!TestTrue(TEXT("Capture pre-avoidance transform cut"),
		Simulation->BuildCandidate(Group, 1u, 1u, 1u, 1u, Before))) return false;
	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	UGuLiWingmanFormationGuidanceProcessor* Guidance =
		NewObject<UGuLiWingmanFormationGuidanceProcessor>(MassSubsystem);
	UGuLiWingmanAvoidanceProcessor* Avoidance = NewObject<UGuLiWingmanAvoidanceProcessor>(MassSubsystem);
	UGuLiWingmanFlightIntegrationProcessor* Integration =
		NewObject<UGuLiWingmanFlightIntegrationProcessor>(MassSubsystem);
	if (!TestNotNull(TEXT("Guidance processor exists"), Guidance)
		|| !TestNotNull(TEXT("Avoidance processor exists"), Avoidance)
		|| !TestNotNull(TEXT("Integration processor exists"), Integration)) return false;
	const TSharedRef<FMassEntityManager> SharedManager = EntityManager.AsShared();
	Guidance->CallInitialize(MassSubsystem, SharedManager);
	Avoidance->CallInitialize(MassSubsystem, SharedManager);
	Integration->CallInitialize(MassSubsystem, SharedManager);
	{
		UMassProcessor* ReadOnlyMotionProcessors[] = {Guidance, Avoidance};
		UE::Mass::FProcessingContext ProcessingContext(EntityManager, 1.0f / 30.0f);
		UE::Mass::Executor::RunProcessorsView(MakeArrayView(ReadOnlyMotionProcessors), ProcessingContext);
	}

	FGuLiWingmanCandidateBatch AfterAvoidance;
	TestTrue(TEXT("Capture post-avoidance transform cut"),
		Simulation->BuildCandidate(Group, 1u, 1u, 2u, 2u, AfterAvoidance));
	TestEqual(TEXT("Avoidance preserves the full candidate sample count"),
		AfterAvoidance.Samples.Num(), Before.Samples.Num());
	for (int32 Index = 0; Index < Before.Samples.Num() && Index < AfterAvoidance.Samples.Num(); ++Index)
	{
		TestEqual(TEXT("Guidance and Avoidance never write Transform"),
			AfterAvoidance.Samples[Index].PositionCentimeters, Before.Samples[Index].PositionCentimeters);
	}

	FGuLiWingmanAvoidanceDiagnostics Diagnostics;
	if (TestTrue(TEXT("Group avoidance diagnostics resolve"),
		Simulation->GetAvoidanceDiagnostics(Group, Diagnostics)))
	{
		TestEqual(TEXT("All alive owner fragments were evaluated"), Diagnostics.EvaluatedEntities, 25);
		TestEqual(TEXT("Empty transient World gives every member one safe primary probe"),
			Diagnostics.SafeHeadingEntities, 25);
		TestEqual(TEXT("No member enters recovery on the clear first frame"),
			Diagnostics.ControlledRecoveryEntities, 0);
		TestTrue(TEXT("Spatial-hash fragment diagnostics are populated"),
			Diagnostics.OccupiedSpatialCells > 0 && Diagnostics.HeadingProbes >= 25u);
	}

	{
		UMassProcessor* SoleWriter[] = {Integration};
		UE::Mass::FProcessingContext ProcessingContext(EntityManager, 1.0f / 30.0f);
		UE::Mass::Executor::RunProcessorsView(MakeArrayView(SoleWriter), ProcessingContext);
	}
	FGuLiWingmanCandidateBatch AfterIntegration;
	TestTrue(TEXT("Capture post-Integration cut"),
		Simulation->BuildCandidate(Group, 1u, 1u, 3u, 3u, AfterIntegration));
	bool bAnyMoved = false;
	for (int32 Index = 0; Index < Before.Samples.Num() && Index < AfterIntegration.Samples.Num(); ++Index)
	{
		bAnyMoved |= AfterIntegration.Samples[Index].PositionCentimeters
			!= Before.Samples[Index].PositionCentimeters;
	}
	TestTrue(TEXT("Only the explicitly run Integration processor advances Transform"), bAnyMoved);
	TestFalse(TEXT("Avoidance processor is excluded from Dedicated Server execution"),
		Avoidance->ShouldExecute(EProcessorExecutionFlags::Server));
	TestTrue(TEXT("Integration remains the sole declared owner Transform writer"),
		!UGuLiWingmanAvoidanceProcessor::IsOwnerTransformWriter()
		&& UGuLiWingmanFlightIntegrationProcessor::IsOwnerTransformWriter());
	TestTrue(TEXT("Cleanup owner group"), Simulation->DestroyOwnedGroup(Group));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanFiniteTurnActualStepPhysicalGateTest,
	"GuLiStrike.Wingman.Avoidance.FiniteTurnActualStepPhysicalGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanFiniteTurnActualStepPhysicalGateTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanAvoidanceRecoveryTests;
	FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	FScopedFlightNavSegmentValidator AllowAllNavigationSegments(
		Fixture.World,
		[](const FVector&, const FVector&, const float)
		{
			return true;
		});

	int32 HeadingProbeCalls = 0;
	int32 ActualStepCalls = 0;
	int32 ActualStepFrontObstacleCalls = 0;
	FScopedWorldObstacleSegmentProbe FrontObstacleProbe(
		Fixture.World,
		[&HeadingProbeCalls, &ActualStepCalls, &ActualStepFrontObstacleCalls](
			const FVector& Start, const FVector& End, const float)
		{
			GuLiWingmanAvoidance::FHeadingProbeResult Result;
			const FVector Segment = End - Start;
			const float Distance = static_cast<float>(Segment.Size());
			Result.ClearanceCentimeters = Distance;
			const FVector Radial(Start.X, Start.Y, 0.0);
			const bool bInnerRing = Radial.SizeSquared() < FMath::Square(75000.0);
			const FVector RadialDirection = Radial.GetSafeNormal();
			const FVector InitialDirection = bInnerRing
				? FVector(RadialDirection.Y, -RadialDirection.X, 0.0)
				: FVector(-RadialDirection.Y, RadialDirection.X, 0.0);
			const bool bStillFacingFrontObstacle = FVector::DotProduct(
				Segment.GetSafeNormal(), InitialDirection) > 0.99;
			if (Distance < 500.0f)
			{
				++ActualStepCalls;
				ActualStepFrontObstacleCalls += bStillFacingFrontObstacle ? 1 : 0;
			}
			else
			{
				++HeadingProbeCalls;
			}
			if (bStillFacingFrontObstacle)
			{
				Result.ClearanceCentimeters = 0.0f;
				Result.bWorldStatic = true;
			}
			return Result;
		});

	UGuLiWingmanSimulationSubsystem* Simulation =
		Fixture.World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	UMassEntitySubsystem* MassSubsystem = Fixture.World->GetSubsystem<UMassEntitySubsystem>();
	if (!TestNotNull(TEXT("Owner simulation exists"), Simulation)
		|| !TestNotNull(TEXT("Mass subsystem exists"), MassSubsystem)) return false;
	Simulation->SetNavigationRequirementBypassForTests(true);
	Simulation->SetGroupBehaviorStateTreeForTests(nullptr);

	const FGuLiWingmanGroupHandle Group = MakeGroup(0x3800u);
	const FGuLiGroupAbilityConfigSnapshot Config = MakeConfig(Group);
	FGuLiCarrierSourceRef CarrierSource;
	CarrierSource.CanonicalEpoch = 1u;
	CarrierSource.MoveRevision = 1u;
	if (!TestTrue(TEXT("Create owner group for final physical-step gate"),
		Simulation->CreateOrResetOwnedGroup(
			Group, Config, FTransform::Identity, FVector::ZeroVector, CarrierSource)))
	{
		return false;
	}

	FGuLiWingmanCandidateBatch Before;
	if (!TestTrue(TEXT("Capture transform cut before the blocked actual step"),
		Simulation->BuildCandidate(Group, 1u, 1u, 1u, 1u, Before)))
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	UGuLiWingmanFormationGuidanceProcessor* Guidance =
		NewObject<UGuLiWingmanFormationGuidanceProcessor>(MassSubsystem);
	UGuLiWingmanAvoidanceProcessor* Avoidance = NewObject<UGuLiWingmanAvoidanceProcessor>(MassSubsystem);
	UGuLiWingmanFlightIntegrationProcessor* Integration =
		NewObject<UGuLiWingmanFlightIntegrationProcessor>(MassSubsystem);
	if (!TestNotNull(TEXT("Guidance processor exists"), Guidance)
		|| !TestNotNull(TEXT("Avoidance processor exists"), Avoidance)
		|| !TestNotNull(TEXT("Integration processor exists"), Integration))
	{
		return false;
	}
	const TSharedRef<FMassEntityManager> SharedManager = EntityManager.AsShared();
	Guidance->CallInitialize(MassSubsystem, SharedManager);
	Avoidance->CallInitialize(MassSubsystem, SharedManager);
	Integration->CallInitialize(MassSubsystem, SharedManager);
	{
		UMassProcessor* HeadingProcessors[] = {Guidance, Avoidance};
		UE::Mass::FProcessingContext ProcessingContext(EntityManager, 1.0f / 30.0f);
		UE::Mass::Executor::RunProcessorsView(MakeArrayView(HeadingProcessors), ProcessingContext);
	}

	FGuLiWingmanAvoidanceDiagnostics AfterHeadingDiagnostics;
	if (TestTrue(TEXT("Heading diagnostics resolve before Integration"),
		Simulation->GetAvoidanceDiagnostics(Group, AfterHeadingDiagnostics)))
	{
		TestEqual(TEXT("Avoidance finds a swept-clear turn around the front obstacle"),
			AfterHeadingDiagnostics.SafeHeadingEntities, GULI_WINGMAN_GROUP_SIZE);
		TestEqual(TEXT("The front WorldStatic obstacle was observed during heading selection"),
			AfterHeadingDiagnostics.WorldStaticThreatEntities, GULI_WINGMAN_GROUP_SIZE);
	}

	{
		UMassProcessor* SoleWriter[] = {Integration};
		UE::Mass::FProcessingContext ProcessingContext(EntityManager, 1.0f / 30.0f);
		UE::Mass::Executor::RunProcessorsView(MakeArrayView(SoleWriter), ProcessingContext);
	}

	FGuLiWingmanCandidateBatch After;
	TestTrue(TEXT("Capture transform cut after the rejected actual step"),
		Simulation->BuildCandidate(Group, 1u, 1u, 2u, 2u, After));
	TestEqual(TEXT("Final physical gate preserves the complete candidate group"),
		After.Samples.Num(), Before.Samples.Num());
	for (int32 Index = 0; Index < Before.Samples.Num() && Index < After.Samples.Num(); ++Index)
	{
		TestEqual(TEXT("Finite-turn step into WorldStatic never mutates Transform"),
			After.Samples[Index].PositionCentimeters, Before.Samples[Index].PositionCentimeters);
		TestEqual(TEXT("A rejected actual step immediately enters Recover"),
			After.Samples[Index].FlightMode, static_cast<uint8>(EGuLiWingmanFlightMode::Recover));
	}
	TestTrue(TEXT("Avoidance evaluated long candidate-heading sweeps"),
		HeadingProbeCalls >= GULI_WINGMAN_GROUP_SIZE);
	TestEqual(TEXT("Integration re-sweeps exactly one actual fixed step per member"),
		ActualStepCalls, GULI_WINGMAN_GROUP_SIZE);
	TestEqual(TEXT("Turn-rate-limited actual steps still face the front obstacle"),
		ActualStepFrontObstacleCalls, GULI_WINGMAN_GROUP_SIZE);

	FGuLiWingmanAvoidanceDiagnostics AfterGateDiagnostics;
	if (TestTrue(TEXT("Persisted final-step diagnostics resolve"),
		Simulation->GetAvoidanceDiagnostics(Group, AfterGateDiagnostics)))
	{
		TestEqual(TEXT("Final-step failure clears every previously safe heading"),
			AfterGateDiagnostics.SafeHeadingEntities, 0);
		TestEqual(TEXT("Final-step failure persists an immediate blocked interval"),
			AfterGateDiagnostics.FullLookAheadBlockedEntities, GULI_WINGMAN_GROUP_SIZE);
		TestEqual(TEXT("Final-step WorldStatic threat remains visible to StateTree policy"),
			AfterGateDiagnostics.WorldStaticThreatEntities, GULI_WINGMAN_GROUP_SIZE);
	}
	TestTrue(TEXT("Cleanup physical-step gate owner group"), Simulation->DestroyOwnedGroup(Group));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanFlightNavBoundaryIntegrationGateTest,
	"GuLiStrike.Wingman.Avoidance.FlightNavBoundaryBlocksIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanFlightNavBoundaryIntegrationGateTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanAvoidanceRecoveryTests;
	FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	UGuLiWingmanSimulationSubsystem* Simulation =
		Fixture.World->GetSubsystem<UGuLiWingmanSimulationSubsystem>();
	UMassEntitySubsystem* MassSubsystem = Fixture.World->GetSubsystem<UMassEntitySubsystem>();
	if (!TestNotNull(TEXT("Owner simulation exists"), Simulation)
		|| !TestNotNull(TEXT("Mass subsystem exists"), MassSubsystem)) return false;
	Simulation->SetNavigationRequirementBypassForTests(true);
	Simulation->SetGroupBehaviorStateTreeForTests(nullptr);

	const FGuLiWingmanGroupHandle Group = MakeGroup(0x4000u);
	const FGuLiGroupAbilityConfigSnapshot Config = MakeConfig(Group);
	FGuLiCarrierSourceRef CarrierSource;
	CarrierSource.CanonicalEpoch = 1u;
	CarrierSource.MoveRevision = 1u;
	if (!TestTrue(TEXT("Create owner group beside synthetic FlightNav boundary"),
		Simulation->CreateOrResetOwnedGroup(
			Group, Config, FTransform::Identity, FVector::ZeroVector, CarrierSource)))
	{
		return false;
	}

	FGuLiWingmanCandidateBatch Before;
	if (!TestTrue(TEXT("Capture transform cut before FlightNav rejection"),
		Simulation->BuildCandidate(Group, 1u, 1u, 1u, 1u, Before)))
	{
		return false;
	}

	int32 ValidationCalls = 0;
	FScopedFlightNavSegmentValidator RejectEverySegment(
		Fixture.World,
		[&ValidationCalls](const FVector&, const FVector&, const float)
		{
			++ValidationCalls;
			return false;
		});

	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	UGuLiWingmanFormationGuidanceProcessor* Guidance =
		NewObject<UGuLiWingmanFormationGuidanceProcessor>(MassSubsystem);
	UGuLiWingmanAvoidanceProcessor* Avoidance = NewObject<UGuLiWingmanAvoidanceProcessor>(MassSubsystem);
	UGuLiWingmanFlightIntegrationProcessor* Integration =
		NewObject<UGuLiWingmanFlightIntegrationProcessor>(MassSubsystem);
	if (!TestNotNull(TEXT("Guidance processor exists"), Guidance)
		|| !TestNotNull(TEXT("Avoidance processor exists"), Avoidance)
		|| !TestNotNull(TEXT("Integration processor exists"), Integration))
	{
		return false;
	}
	const TSharedRef<FMassEntityManager> SharedManager = EntityManager.AsShared();
	Guidance->CallInitialize(MassSubsystem, SharedManager);
	Avoidance->CallInitialize(MassSubsystem, SharedManager);
	Integration->CallInitialize(MassSubsystem, SharedManager);
	UMassProcessor* OwnerMotionProcessors[] = {Guidance, Avoidance, Integration};
	for (int32 Step = 0; Step < 92; ++Step)
	{
		UE::Mass::FProcessingContext ProcessingContext(EntityManager, 1.0f / 30.0f);
		UE::Mass::Executor::RunProcessorsView(MakeArrayView(OwnerMotionProcessors), ProcessingContext);
	}

	FGuLiWingmanCandidateBatch After;
	TestTrue(TEXT("Capture transform cut after repeated FlightNav rejection"),
		Simulation->BuildCandidate(Group, 1u, 1u, 2u, 2u, After));
	TestEqual(TEXT("Boundary gate preserves the complete candidate group"),
		After.Samples.Num(), Before.Samples.Num());
	for (int32 Index = 0; Index < Before.Samples.Num() && Index < After.Samples.Num(); ++Index)
	{
		TestEqual(TEXT("Invalid next step is never published and never teleports"),
			After.Samples[Index].PositionCentimeters, Before.Samples[Index].PositionCentimeters);
		TestEqual(TEXT("Persistently blocked entities enter controlled Recover mode"),
			After.Samples[Index].FlightMode, static_cast<uint8>(EGuLiWingmanFlightMode::Recover));
	}
	TestTrue(TEXT("Both heading probes and the final Integration gate invoke FlightNav validation"),
		ValidationCalls > GULI_WINGMAN_GROUP_SIZE);

	FGuLiWingmanAvoidanceDiagnostics Diagnostics;
	if (TestTrue(TEXT("Boundary recovery diagnostics resolve"),
		Simulation->GetAvoidanceDiagnostics(Group, Diagnostics)))
	{
		TestEqual(TEXT("Every owner entity reports no safe FlightNav heading"),
			Diagnostics.SafeHeadingEntities, 0);
		TestEqual(TEXT("Every owner entity enters controlled recovery after three seconds"),
			Diagnostics.ControlledRecoveryEntities, GULI_WINGMAN_GROUP_SIZE);
		TestEqual(TEXT("Every owner entity reports a blocked full lookahead"),
			Diagnostics.FullLookAheadBlockedEntities, GULI_WINGMAN_GROUP_SIZE);
	}
	TestFalse(TEXT("Avoidance still cannot execute on Dedicated Server"),
		Avoidance->ShouldExecute(EProcessorExecutionFlags::Server));
	TestTrue(TEXT("FlightNav boundary handling introduces no second Transform writer"),
		!UGuLiWingmanAvoidanceProcessor::IsOwnerTransformWriter()
		&& UGuLiWingmanFlightIntegrationProcessor::IsOwnerTransformWriter());
	TestTrue(TEXT("Cleanup FlightNav boundary owner group"), Simulation->DestroyOwnedGroup(Group));
	return true;
}

#endif
