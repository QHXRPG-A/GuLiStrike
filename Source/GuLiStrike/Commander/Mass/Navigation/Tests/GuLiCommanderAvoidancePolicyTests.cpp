// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/Navigation/GuLiCommanderAvoidancePolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Avoidance/MassAvoidanceFragments.h"
#include "MassMovementFragments.h"
#include "Misc/AutomationTest.h"

namespace GuLiCommanderAvoidancePolicyTests
{
	using namespace GuLiCommanderAvoidancePolicy;

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderPredictiveAvoidanceCadenceTest,
		"GuLiStrike.Commander.Mass.Navigation.PredictiveAvoidance.Cadence",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderPredictiveAvoidanceCadenceTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		TArray<int32> SolvesBySoldier;
		SolvesBySoldier.Init(0, 501);
		TArray<int32> SoldiersByPhase;
		SoldiersByPhase.Init(0, SolvePhaseCount);
		for (uint32 SoldierId = 1u; SoldierId <= 500u; ++SoldierId)
		{
			++SoldiersByPhase[ResolveSolvePhase(SoldierId)];
		}
		TestEqual(TEXT("Phase 0 contains 166 Soldiers"), SoldiersByPhase[0], 166);
		TestEqual(TEXT("Phase 1 contains 167 Soldiers"), SoldiersByPhase[1], 167);
		TestEqual(TEXT("Phase 2 contains 167 Soldiers"), SoldiersByPhase[2], 167);

		double Accumulator = 0.0;
		uint64 NextSequence = 0u;
		for (int32 Step = 0; Step < 30; ++Step)
		{
			const FPhaseAdvanceResult Advance = AdvancePhases(
				FixedStepSeconds,
				Accumulator,
				NextSequence);
			TestEqual(TEXT("A normal update crosses one 30 Hz step"), Advance.FixedSteps, 1);
			int32 SolvesThisStep = 0;
			for (uint32 SoldierId = 1u; SoldierId <= 500u; ++SoldierId)
			{
				if (ShouldSolve(Advance.PhaseMask, SoldierId, 7u, 7u, true))
				{
					++SolvesBySoldier[SoldierId];
					++SolvesThisStep;
				}
			}
			TestTrue(TEXT("Each phase is a balanced third"),
				SolvesThisStep == 166 || SolvesThisStep == 167);
		}
		for (uint32 SoldierId = 1u; SoldierId <= 500u; ++SoldierId)
		{
			TestEqual(TEXT("Every Soldier solves exactly ten times per second"),
				SolvesBySoldier[SoldierId], 10);
		}

		const FPhaseAdvanceResult HitchAdvance = AdvancePhases(1.0, Accumulator, NextSequence);
		TestEqual(TEXT("Hitch catch-up is capped at three fixed steps"), HitchAdvance.FixedSteps, 3);
		TestEqual(TEXT("Three crossed phases are merged into one phase mask"),
			HitchAdvance.PhaseMask, static_cast<uint8>(0x7u));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderPredictiveAvoidanceRevisionTest,
		"GuLiStrike.Commander.Mass.Navigation.PredictiveAvoidance.OrderRevision",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderPredictiveAvoidanceRevisionTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		const uint8 PhaseZeroMask = 1u << 0u;
		TestFalse(TEXT("Soldier 1 waits when its phase is absent and its revision is unchanged"),
			ShouldSolve(PhaseZeroMask, 1u, 10u, 10u, true));
		TestTrue(TEXT("A new revision bypasses the absent phase on the next avoidance step"),
			ShouldSolve(PhaseZeroMask, 1u, 11u, 10u, true));
		TestFalse(TEXT("After the forced solve, Soldier 1 returns to its stable phase"),
			ShouldSolve(PhaseZeroMask, 1u, 11u, 11u, true));
		TestTrue(TEXT("The stable phase schedules Soldier 1 again"),
			ShouldSolve(1u << 1u, 1u, 11u, 11u, true));
		TestFalse(TEXT("Stopped Soldiers never retain scheduled predictive work"),
			ShouldSolve(1u << 1u, 1u, 12u, 11u, false));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderPredictiveAvoidanceGridBoundaryTest,
		"GuLiStrike.Commander.Mass.Navigation.PredictiveAvoidance.GridBoundaries",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderPredictiveAvoidanceGridBoundaryTest::RunTest(const FString& Parameters)
	{
	// Scale020 fixture: spatial values use final centimeters.

		(void)Parameters;
		TArray<FAgentSnapshot> Agents = {
			{1u, FVector(299.0, 10.0, 0.0), FVector::ZeroVector, 150.0f, true, true},
			{2u, FVector(1499.0, 10.0, 0.0), FVector::ZeroVector, 150.0f, true, true},
			{3u, FVector(1499.1, 10.0, 0.0), FVector::ZeroVector, 150.0f, true, true},
			{4u, FVector(299.0, 10.0, 300.1), FVector::ZeroVector, 150.0f, true, true},
			{5u, FVector(-299.0, -10.0, 0.0), FVector::ZeroVector, 150.0f, true, true},
			{6u, FVector(-1499.0, -10.0, 0.0), FVector::ZeroVector, 150.0f, true, true}
		};
		FAvoidanceSpatialGrid Grid;
		TestEqual(TEXT("The two deliberately adjacent out-of-range samples share one bucket"),
			BuildSpatialGrid(Agents, Grid), 2);

		FNearestCandidateList Candidates;
		const FCandidateQueryMetrics PositiveMetrics = SelectNearestCandidates(0, Agents, Grid, Candidates);
		TestTrue(TEXT("A neighbor exactly 1200 cm away across four cells is retained"),
			Candidates.ContainsByPredicate([](const FNearestCandidate& Candidate)
			{
				return Candidate.StableKey == 2u;
			}));
		TestFalse(TEXT("A neighbor just outside 1200 cm is filtered"),
			Candidates.ContainsByPredicate([](const FNearestCandidate& Candidate)
			{
				return Candidate.StableKey == 3u;
			}));
		TestFalse(TEXT("A neighbor beyond the vertical limit is filtered"),
			Candidates.ContainsByPredicate([](const FNearestCandidate& Candidate)
			{
				return Candidate.StableKey == 4u;
			}));
		TestTrue(TEXT("The query visits local bucket entries before exact filtering"),
			PositiveMetrics.BucketEntriesVisited >= PositiveMetrics.ExactCandidates);

		SelectNearestCandidates(4, Agents, Grid, Candidates);
		TestTrue(TEXT("Negative-coordinate cell boundaries retain a 1200 cm neighbor"),
			Candidates.ContainsByPredicate([](const FNearestCandidate& Candidate)
			{
				return Candidate.StableKey == 6u;
			}));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderPredictiveAvoidanceCandidateSelectionTest,
		"GuLiStrike.Commander.Mass.Navigation.PredictiveAvoidance.DeterministicCandidates",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderPredictiveAvoidanceCandidateSelectionTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		TArray<FAgentSnapshot> ForwardAgents;
		ForwardAgents.Add({1u, FVector::ZeroVector, FVector(1000.0, 0.0, 0.0), 750.0f, true, true});
		for (uint64 StableKey = 2u; StableKey <= 31u; ++StableKey)
		{
			const float Distance = 100.0f + static_cast<float>((StableKey - 2u) / 2u) * 100.0f;
			const float Sign = (StableKey & 1u) == 0u ? 1.0f : -1.0f;
			ForwardAgents.Add({StableKey, FVector(Sign * Distance, 0.0, 0.0),
				FVector::ZeroVector, 750.0f, true, false});
		}

		auto CollectStableKeys = [](const TArray<FAgentSnapshot>& Agents)
		{
			FAvoidanceSpatialGrid Grid;
			BuildSpatialGrid(Agents, Grid);
			const int32 OriginIndex = Agents.IndexOfByPredicate([](const FAgentSnapshot& Agent)
			{
				return Agent.StableKey == 1u;
			});
			FNearestCandidateList Candidates;
			SelectNearestCandidates(OriginIndex, Agents, Grid, Candidates);
			TArray<uint64> Keys;
			for (const FNearestCandidate& Candidate : Candidates)
			{
				Keys.Add(Candidate.StableKey);
			}
			return Keys;
		};

		const TArray<uint64> ForwardKeys = CollectStableKeys(ForwardAgents);
		TArray<FAgentSnapshot> ReverseAgents = ForwardAgents;
		Algo::Reverse(ReverseAgents);
		const TArray<uint64> ReverseKeys = CollectStableKeys(ReverseAgents);
		TestEqual(TEXT("Only the nearest 24 candidates are retained"),
			ForwardKeys.Num(), MaximumNearestCandidates);
		TestTrue(TEXT("Candidate identity and order do not depend on bucket insertion order"),
			ForwardKeys == ReverseKeys);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderPredictiveAvoidanceForceTest,
		"GuLiStrike.Commander.Mass.Navigation.PredictiveAvoidance.Force",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderPredictiveAvoidanceForceTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		FMassMovingAvoidanceParameters AvoidanceA;
		AvoidanceA.PredictiveAvoidanceDistance = 262.5f;
		FMassMovingAvoidanceParameters AvoidanceB = AvoidanceA;
		AvoidanceB.ObstacleSeparationStiffness = AvoidanceA.ObstacleSeparationStiffness * 100.0f;
		FMassMovementParameters Movement;
		Movement.MaxSpeed = 3600.0f;
		Movement.MaxAcceleration = 100.0f;
		const FPredictiveParameters PredictiveA = MakePredictiveParameters(AvoidanceA, Movement);
		const FPredictiveParameters PredictiveB = MakePredictiveParameters(AvoidanceB, Movement);

		TArray<FAgentSnapshot> Agents = {
			{1u, FVector::ZeroVector, FVector(1000.0, 0.0, 0.0), 750.0f, true, true},
			{2u, FVector(3000.0, 0.0, 0.0), FVector(-1000.0, 0.0, 0.0), 750.0f, true, true}
		};
		FNearestCandidate Candidate;
		Candidate.AgentIndex = 1;
		Candidate.DistanceSquared = 9000000.0;
		Candidate.StableKey = 2u;
		TArray<FNearestCandidate> Candidates = {Candidate};
		int32 ColliderEvaluationsA = 0;
		const FVector ApproachingForce = CalculatePredictiveAvoidance(
			Agents[0], Agents, Candidates, PredictiveA, 1.0f, ColliderEvaluationsA);
		int32 ColliderEvaluationsB = 0;
		const FVector SeparationChangedForce = CalculatePredictiveAvoidance(
			Agents[0], Agents, Candidates, PredictiveB, 1.0f, ColliderEvaluationsB);
		TestTrue(TEXT("Approaching units produce a finite predictive force"),
			!ApproachingForce.ContainsNaN() && !ApproachingForce.IsNearlyZero());
		TestTrue(TEXT("Predictive force respects maximum acceleration"),
			ApproachingForce.Size() <= Movement.MaxAcceleration + UE_KINDA_SMALL_NUMBER);
		TestTrue(TEXT("Instant-separation stiffness does not affect predictive output"),
			ApproachingForce.Equals(SeparationChangedForce, UE_KINDA_SMALL_NUMBER));
		TestEqual(TEXT("One selected collider is evaluated"), ColliderEvaluationsA, 1);

		Agents[0].Velocity = FVector(-1000.0, 0.0, 0.0);
		Agents[1].Velocity = FVector(1000.0, 0.0, 0.0);
		int32 DivergingEvaluations = 0;
		const FVector DivergingForce = CalculatePredictiveAvoidance(
			Agents[0], Agents, Candidates, PredictiveA, 1.0f, DivergingEvaluations);
		TestTrue(TEXT("Separated units moving farther apart produce no predictive force"),
			DivergingForce.IsNearlyZero());

		TArray<FAgentSnapshot> DenseAgents;
		DenseAgents.Add(Agents[0]);
		TArray<FNearestCandidate> DenseCandidates;
		for (int32 Index = 0; Index < 10; ++Index)
		{
			const int32 OtherIndex = DenseAgents.Add({
				static_cast<uint64>(Index + 2),
				FVector(1000.0 + Index * 10.0, 0.0, 0.0),
				FVector(-1000.0, 0.0, 0.0),
				750.0f,
				true,
				true});
			DenseCandidates.Add({OtherIndex, FMath::Square(1000.0 + Index * 10.0),
				static_cast<uint64>(Index + 2)});
		}
		int32 DenseEvaluations = 0;
		CalculatePredictiveAvoidance(
			DenseAgents[0], DenseAgents, DenseCandidates, PredictiveA, 1.0f, DenseEvaluations);
		TestEqual(TEXT("Predictive solving evaluates at most six Colliders"),
			DenseEvaluations, MaximumColliders);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderPredictiveAvoidanceFormationCostTest,
		"GuLiStrike.Commander.Mass.Navigation.PredictiveAvoidance.FormationCost",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderPredictiveAvoidanceFormationCostTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		TArray<FAgentSnapshot> Agents;
		Agents.Reserve(500);
		for (int32 Row = 0; Row < 20; ++Row)
		{
			for (int32 Column = 0; Column < 25; ++Column)
			{
				Agents.Add({
					static_cast<uint64>(Agents.Num() + 1),
					FVector(Column * 1800.0, Row * 1800.0, 0.0),
					FVector::ZeroVector,
					750.0f,
					true,
					true});
			}
		}
		FAvoidanceSpatialGrid Grid;
		TestEqual(TEXT("A standard 1800 cm formation keeps 1500 cm buckets sparse"),
			BuildSpatialGrid(Agents, Grid), 1);
		uint64 TotalBucketVisits = 0u;
		for (int32 AgentIndex = 0; AgentIndex < Agents.Num(); ++AgentIndex)
		{
			FNearestCandidateList Candidates;
			TotalBucketVisits += SelectNearestCandidates(
				AgentIndex, Agents, Grid, Candidates).BucketEntriesVisited;
		}
		TestTrue(TEXT("Even querying all 500 Soldiers stays far below a full 249500 directed scan"),
			TotalBucketVisits < 50000u);
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
