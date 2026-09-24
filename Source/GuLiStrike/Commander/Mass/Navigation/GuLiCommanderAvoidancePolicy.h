// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FMassMovingAvoidanceParameters;
struct FMassMovementParameters;

/**
 * Pure, deterministic policy used by the Commander Mass predictive-avoidance processor.
 * Mass iteration and world access stay in the processor; scheduling, spatial lookup and
 * closest-point-of-approach force calculations live here so they can be regression tested.
 */
namespace GuLiCommanderAvoidancePolicy
{
	inline constexpr uint32 SolvePhaseCount = 3u;
	inline constexpr double FixedStepSeconds = 1.0 / 30.0;
	inline constexpr double MaximumAccumulatedSeconds = FixedStepSeconds * SolvePhaseCount;
	inline constexpr float SpatialCellSizeCentimeters = 300.0f;
	inline constexpr float DetectionDistanceCentimeters = 1200.0f;
	inline constexpr float MaximumHeightDifferenceCentimeters = 300.0f;
	inline constexpr int32 MaximumNearestCandidates = 24;
	inline constexpr int32 MaximumColliders = 6;

	/** The unique phases crossed by one render-frame update, plus their solve sequence numbers. */
	struct GULISTRIKE_API FPhaseAdvanceResult
	{
		uint8 PhaseMask = 0u;
		int32 FixedSteps = 0;
		uint64 LastSequence = 0u;
		uint64 SequenceByPhase[SolvePhaseCount] = {};
	};

	/** Immutable center-point collider snapshot. */
	struct GULISTRIKE_API FAgentSnapshot
	{
		uint64 StableKey = 0u;
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		float Radius = 0.0f;
		bool bParticipates = false;
		bool bMoving = false;
		bool bEnvironment = false;
	};

	/** One exact-range candidate, ordered by distance then stable entity key. */
	struct GULISTRIKE_API FNearestCandidate
	{
		int32 AgentIndex = INDEX_NONE;
		double DistanceSquared = 0.0;
		uint64 StableKey = 0u;
	};

	/** Query work counters. Bucket visits measure actual local lookup work before exact filtering. */
	struct GULISTRIKE_API FCandidateQueryMetrics
	{
		uint64 BucketEntriesVisited = 0u;
		uint64 ExactCandidates = 0u;
	};

	/** Predictive-only subset of Epic's moving avoidance parameters. */
	struct GULISTRIKE_API FPredictiveParameters
	{
		float PredictiveAvoidanceTime = 2.5f;
		float PredictiveAvoidanceRadiusScale = 0.65f;
		float PredictiveAvoidanceDistance = 15.0f;
		float PredictiveAvoidanceStiffness = 140.0f;
		float StandingObstacleAvoidanceScale = 0.65f;
		float MaximumSpeed = 0.0f;
		float MaximumAcceleration = 0.0f;
	};

	using FAvoidanceBucket = TArray<int32, TInlineAllocator<8>>;
	using FAvoidanceSpatialGrid = TMap<FIntPoint, FAvoidanceBucket>;
	using FNearestCandidateList = TArray<FNearestCandidate, TInlineAllocator<MaximumNearestCandidates>>;

	/** Advances at 30 Hz, caps hitch catch-up at three steps, and merges repeated phases in one mask. */
	GULISTRIKE_API FPhaseAdvanceResult AdvancePhases(
		double DeltaSeconds,
		double& InOutAccumulatorSeconds,
		uint64& InOutNextSequence);

	GULISTRIKE_API uint32 ResolveSolvePhase(uint32 StableSoldierId);

	/** A changed order revision bypasses the current phase exactly once. */
	GULISTRIKE_API bool ShouldSolve(
		uint8 DuePhaseMask,
		uint32 StableSoldierId,
		uint32 OrderRevision,
		uint32 LastProcessedOrderRevision,
		bool bMoving);

	GULISTRIKE_API FIntPoint MakeSpatialCell(const FVector& Location);

	/** Rebuilds the radius-covered environment / center-point soldier grid and returns its largest bucket occupancy. */
	GULISTRIKE_API int32 BuildSpatialGrid(
		TConstArrayView<FAgentSnapshot> Agents,
		FAvoidanceSpatialGrid& InOutGrid);

	/** Queries 9x9 cells and deterministically retains the nearest 24 exact-range neighbors. */
	GULISTRIKE_API FCandidateQueryMetrics SelectNearestCandidates(
		int32 AgentIndex,
		TConstArrayView<FAgentSnapshot> Agents,
		const FAvoidanceSpatialGrid& Grid,
		FNearestCandidateList& OutCandidates,
		float DetectionDistance = DetectionDistanceCentimeters,
		float MaximumHeightDifference = MaximumHeightDifferenceCentimeters);

	/** Copies only CPA-related values; separation stiffness and distance are intentionally ignored. */
	GULISTRIKE_API FPredictiveParameters MakePredictiveParameters(
		const FMassMovingAvoidanceParameters& AvoidanceParameters,
		const FMassMovementParameters& MovementParameters);

	/** Epic-compatible start/end action fade expressed as one final avoidance scale. */
	GULISTRIKE_API float CalculatePathFade(
		double CurrentWorldSeconds,
		double CurrentActionStartSeconds,
		bool bPreviousActionWasMove,
		bool bWillStandAtGoal,
		float DistanceToGoal,
		float DesiredSpeed,
		const FMassMovingAvoidanceParameters& AvoidanceParameters);

	/** Computes predictive CPA force for at most six colliders; no instantaneous separation is applied. */
	GULISTRIKE_API FVector CalculatePredictiveAvoidance(
		const FAgentSnapshot& Agent,
		TConstArrayView<FAgentSnapshot> Agents,
		TConstArrayView<FNearestCandidate> Candidates,
		const FPredictiveParameters& Parameters,
		float PathFade,
		int32& OutColliderEvaluations);
}
