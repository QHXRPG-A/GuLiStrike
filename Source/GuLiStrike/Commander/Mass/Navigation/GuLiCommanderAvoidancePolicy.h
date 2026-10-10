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
	inline constexpr int32 MaximumNearestCandidates = 12;
	inline constexpr int32 MaximumColliders = 12;

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
		FVector DesiredVelocity = FVector::ZeroVector;
		float MaximumSpeed = 0.0f;
	};

	/** One exact-range threat, ordered by overlap, collision time, distance and stable key. */
	struct GULISTRIKE_API FNearestCandidate
	{
		int32 AgentIndex = INDEX_NONE;
		double DistanceSquared = 0.0;
		uint64 StableKey = 0u;
		bool bOverlapping = false;
		double TimeToCollision = TNumericLimits<double>::Max();
	};

	/** Query work counters. Bucket visits measure actual local lookup work before exact filtering. */
	struct GULISTRIKE_API FCandidateQueryMetrics
	{
		uint64 BucketEntriesVisited = 0u;
		uint64 ExactCandidates = 0u;
		uint64 CellLookups = 0u, CellHits = 0u, SparseComparisons = 0u, EnvironmentDuplicates = 0u, UniqueVisits = 0u;
		uint64 RetainedCandidates = 0u, ConsumedCandidates = 0u, VerifiedPrefixes = 0u, PrefixMismatches = 0u;
	};

	/** Predictive-only subset of Epic's moving avoidance parameters. */
	struct GULISTRIKE_API FPredictiveParameters
	{
		float PredictiveAvoidanceTime = 2.5f;
		float PredictiveAvoidanceRadiusScale = 1.0f;
		float PredictiveAvoidanceDistance = 15.0f;
		float PredictiveAvoidanceStiffness = 700.0f;
		float StandingObstacleAvoidanceScale = 0.65f;
		float MaximumSpeed = 0.0f;
		float MaximumAcceleration = 0.0f;
	};

	using FAvoidanceBucket = TArray<int32, TInlineAllocator<8>>;
	/** Ordered nonempty cells retain the original X/Y and bucket traversal order.
	 * Bucket pointers are rebuilt after all map insertions and live until the next rebuild. */
	struct GULISTRIKE_API FAvoidanceSpatialGrid : TMap<FIntPoint, FAvoidanceBucket>
	{
		FAvoidanceSpatialGrid() = default;
		FAvoidanceSpatialGrid(const FAvoidanceSpatialGrid&) = delete;
		FAvoidanceSpatialGrid& operator=(const FAvoidanceSpatialGrid&) = delete;
		struct FCell { FIntPoint Key; const FAvoidanceBucket* Bucket; };
		struct FRow { int32 X, Begin, End; };
		TArray<FCell> OrderedCells;
		TArray<FRow> OrderedRows;
		void Reset();
		void RebuildOrderedCells();
		void GatherOrderedBuckets(FIntPoint Minimum, FIntPoint Maximum,
			TArray<const FAvoidanceBucket*, TInlineAllocator<64>>& Out, uint64& Comparisons) const;
	};
	using FNearestCandidateList = TArray<FNearestCandidate, TInlineAllocator<24>>;

	/** Environment colliders span cells; ordinary agents occur in exactly one bucket. */
	struct GULISTRIKE_API FCandidateQueryScratch
	{
		TArray<uint32> EnvironmentMarks;
		TArray<const FAvoidanceBucket*, TInlineAllocator<64>> Buckets;
		uint32 Generation = 0;
		void BeginQuery(int32 Count);
		bool VisitEnvironment(int32 Index);
	};

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

	GULISTRIKE_API FIntPoint MakeSpatialCell(const FVector& Location,
		float CellSize = SpatialCellSizeCentimeters);

	/** Rebuilds the radius-covered environment / center-point soldier grid and returns its largest bucket occupancy. */
	GULISTRIKE_API int32 BuildSpatialGrid(
		TConstArrayView<FAgentSnapshot> Agents,
		FAvoidanceSpatialGrid& InOutGrid,
		float CellSize = SpatialCellSizeCentimeters);

	/** Retains twelve consumed threats, including up to two environment priority slots. */
	GULISTRIKE_API FCandidateQueryMetrics SelectNearestCandidates(
		int32 AgentIndex,
		TConstArrayView<FAgentSnapshot> Agents,
		const FAvoidanceSpatialGrid& Grid,
		FNearestCandidateList& OutCandidates,
		float DetectionDistance = DetectionDistanceCentimeters,
		float MaximumHeightDifference = MaximumHeightDifferenceCentimeters,
		float CellSize = SpatialCellSizeCentimeters,
		float TimeHorizon = 2.5f,
		FCandidateQueryScratch* Scratch = nullptr);

	/** Copies only CPA-related values; separation stiffness and distance are intentionally ignored. */
	GULISTRIKE_API FPredictiveParameters MakePredictiveParameters(
		const FMassMovingAvoidanceParameters& AvoidanceParameters,
		const FMassMovementParameters& MovementParameters);

	/** Start/end action fade with a minimum final predictive scale of one half. */
	GULISTRIKE_API float CalculatePathFade(
		double CurrentWorldSeconds,
		double CurrentActionStartSeconds,
		bool bPreviousActionWasMove,
		bool bWillStandAtGoal,
		float DistanceToGoal,
		float DesiredSpeed,
		const FMassMovingAvoidanceParameters& AvoidanceParameters);

	/** Computes predictive CPA acceleration; positional separation belongs to neither this nor the soft solver. */
	GULISTRIKE_API FVector CalculatePredictiveAvoidance(
		const FAgentSnapshot& Agent,
		TConstArrayView<FAgentSnapshot> Agents,
		TConstArrayView<FNearestCandidate> Candidates,
		const FPredictiveParameters& Parameters,
		float PathFade,
		int32& OutColliderEvaluations);
}
