// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/Movement/GuLiWingmanSteering.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/Wingman/GuLiWingmanRuntimeTypes.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanObstacleHeadingSelectionTest,
	"GuLiStrike.Wingman.Actor.Avoidance.StableThreeDimensionalHeading",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanObstacleHeadingSelectionTest::RunTest(const FString& Parameters)
{
	FGuLiWingmanFormationRuntimeConfig Tuning;
	Tuning.MinimumSpeedCentimetersPerSecond = 100.0f;
	Tuning.AgentRadiusCentimeters = 50.0f;
	Tuning.ObstacleLookAheadCentimeters = 1000.0f;
	Tuning.MaximumTurnRateDegreesPerSecond = 60.0f;
	const auto Probe = [](const FVector& Direction, const float Distance)
	{
		GuLiWingmanSteering::FHeadingProbeResult Result;
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
	const auto Selection = GuLiWingmanSteering::SelectSafeHeading(
		FVector::ForwardVector, FVector::ForwardVector, 500.0f, Tuning, Probe);
	const auto Repeat = GuLiWingmanSteering::SelectSafeHeading(
		FVector::ForwardVector, FVector::ForwardVector, 500.0f, Tuning, Probe);
	TestTrue(TEXT("Safe alternative is selected"),
		Selection.bFullLookAheadSafe && Selection.bImmediateStepSafe);
	TestTrue(TEXT("Static and dynamic threats are diagnosed"),
		Selection.bEncounteredWorldStatic && Selection.bEncounteredWorldDynamic);
	TestTrue(TEXT("Heading selection is deterministic"),
		Selection.Direction.Equals(Repeat.Direction, UE_KINDA_SMALL_NUMBER)
		&& Selection.ProbeCount == Repeat.ProbeCount);

	FGuLiWingmanAvoidanceState PersistedThreats;
	GuLiWingmanSteering::AccumulateHeadingThreatsForTests(
		PersistedThreats, Selection);
	TestTrue(TEXT("Threats persist into per-Pawn diagnostics"),
		PersistedThreats.bDetectedWorldStatic
		&& PersistedThreats.bDetectedWorldDynamic);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanRecoveryClockTest,
	"GuLiStrike.Wingman.Actor.Avoidance.HalfSecondRecoveryHysteresis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanRecoveryClockTest::RunTest(const FString& Parameters)
{
	GuLiWingmanSteering::FRecoveryClockState State;
	// Runtime clamps one observation to 0.25 s, so accumulate the boundary
	// through multiple fixed-step-compatible observations.
	GuLiWingmanSteering::AdvanceRecoveryClock(State, 0.25f, false, false);
	GuLiWingmanSteering::AdvanceRecoveryClock(State, 0.24f, false, false);
	TestFalse(TEXT("Recovery does not enter before half a second"),
		State.bControlledRecovery);
	GuLiWingmanSteering::AdvanceRecoveryClock(State, 0.01f, false, false);
	TestTrue(TEXT("Half a second without progress enters recovery"),
		State.bControlledRecovery);
	GuLiWingmanSteering::AdvanceRecoveryClock(State, 0.25f, true, true);
	TestTrue(TEXT("One clear sample is held for hysteresis"), State.bControlledRecovery);
	GuLiWingmanSteering::AdvanceRecoveryClock(State, 0.25f, true, true);
	TestFalse(TEXT("Half a second of progress exits recovery"),
		State.bControlledRecovery);

	const FVector Orbit = GuLiWingmanSteering::BuildRecoveryOrbitDirection(
		FVector(1000.0, 0.0, 0.0), FVector::RightVector,
		FVector::ZeroVector, 1000.0f);
	TestTrue(TEXT("Emergency hover-turn direction is finite"),
		!Orbit.ContainsNaN() && Orbit.IsNormalized());
	return true;
}

#endif
