// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/GuLiCommanderSimulationTiming.h"
#include "AI/Navigation/NavigationTypes.h"

class ANavigationData;
class UNavigationSystemV1;

/**
 * Testable navigation decisions shared by the Commander authority runtime.
 *
 * World/NavMesh sampling stays on the game thread in the authority subsystem;
 * this policy owns only the deterministic contract applied to those samples.
 */
namespace GuLiCommanderNavigationPolicy
{
	inline constexpr int32 FormationMemberCapacity = 25;
	inline constexpr int32 MaximumFormationColumns = 5;
	inline constexpr float RequiredAgentRadiusCentimeters = 150.0f;
	inline constexpr float MaximumSurfaceMoveZDeltaCentimeters = 50.0f;
	inline constexpr float MinimumNavigationProgressCentimeters = 6.0f;
	inline constexpr int32 RequiredTransitExpansionSuccessSteps = GuLiCommanderSimulationTiming::RateHz / 2u;
	inline constexpr double MinimumTransitRearrangementIntervalSeconds = 0.5;
	inline constexpr uint32 MovementUpdateIntervalTicks = 1u;
	inline constexpr float MaximumMovementUpdateDeltaSeconds = 0.1f;

	/** Frozen terminal coordinate frame derived from the last usable NavMesh path segment. */
	struct GULISTRIKE_API FFinalPathFrame
	{
		FVector Forward = FVector::ForwardVector;
		FVector TailClearOrigin = FVector::ZeroVector;
		double TailClearPathDistanceCentimeters = 0.0;
		int32 TailClearPathPointIndex = INDEX_NONE;
		float YawDegrees = 0.0f;
		bool bHasUsableDirection = false;
		bool bRequiresTailClear = false;
	};

	/** Runtime selection state supplied to the pure order-completion predicate. */
	struct GULISTRIKE_API FFormationMemberProgressSample
	{
		FVector Location = FVector::ZeroVector;
		bool bAlive = false;
		bool bFollowsOrder = false;
		bool bTailCleared = false;
		bool bHasReachedArrival = false;
	};

	/** Monotonic per-member arrival state plus a hysteretic recovery sub-state. */
	struct GULISTRIKE_API FLooseArrivalMemberState
	{
		bool bTailCleared = false;
		bool bHasReachedArrival = false;
		bool bRecovering = false;
	};

	/** One accepted formation's contribution to its shared batch completion gate. */
	struct GULISTRIKE_API FBatchOrderFormationCompletionSample
	{
		bool bHasActiveMembers = false;
		bool bPathValid = false;
		bool bGuideAndMembersReady = false;
	};

	/** Persistent inputs for deterministic transit-column expansion hysteresis. */
	struct GULISTRIKE_API FTransitColumnHysteresisState
	{
		int32 ColumnCount = MaximumFormationColumns;
		int32 ConsecutiveExpansionSuccessSteps = 0;
		double LastRearrangementTimeSeconds = -1.0;
	};

	/** Immutable input for one deterministic manual-separation solve. */
	struct GULISTRIKE_API FManualAvoidanceAgent
	{
		uint32 StableSoldierId = 0u;
		FVector Location = FVector::ZeroVector;
		bool bParticipates = false;
		bool bReceivesAvoidance = false;
		/** Zero keeps the legacy global minimum; static proxies use their actual footprint radius. */
		float RadiusCentimeters = 0.0f;
	};

	/** Work counters produced by one manual-separation refresh. */
	struct GULISTRIKE_API FManualAvoidanceMetrics
	{
		uint64 CandidatePairs = 0u;
		uint64 OverlapPairs = 0u;
		int32 MaximumBucketOccupancy = 0;
	};

	using FManualAvoidanceBucket = TArray<int32, TInlineAllocator<4>>;
	using FManualAvoidanceSpatialGrid = TMap<FIntPoint, FManualAvoidanceBucket>;

	/** The only SupportedAgent name legal for Commander movement. */
	GULISTRIKE_API FName GetRequiredAgentName();

	/** Optional phase scheduling; the 10 Hz authority runtime updates every Soldier on every step. */
	GULISTRIKE_API uint32 ResolveMovementUpdatePhase(
		uint32 StableSoldierId,
		uint32 UpdateIntervalTicks = MovementUpdateIntervalTicks);

	/** A forced first update bypasses the phase gate; invalid IDs and intervals are rejected. */
	GULISTRIKE_API bool ShouldRunMovementUpdate(
		uint32 ServerSimTick,
		uint32 StableSoldierId,
		bool bForceUpdate,
		uint32 UpdateIntervalTicks = MovementUpdateIntervalTicks);

	/** Clamps elapsed simulation time so a resumed Soldier cannot perform a large catch-up jump. */
	GULISTRIKE_API float ResolveMovementUpdateDeltaSeconds(
		double CurrentSimulationSeconds,
		double LastMovementUpdateSimulationSeconds,
		float MinimumDeltaSeconds,
		float MaximumDeltaSeconds = MaximumMovementUpdateDeltaSeconds);

	/** Cell coordinate for the dedicated short-range manual-avoidance grid. */
	GULISTRIKE_API FIntPoint MakeAvoidanceSpatialCell(
		const FVector& Location,
		float CellSizeCentimeters);

	/**
	 * Rebuilds the caller-owned grid and evaluates every local unordered pair once.
	 * CellSize must be at least MinimumDistance so the fixed 3x3 search cannot miss a pair.
	 */
	GULISTRIKE_API FManualAvoidanceMetrics BuildManualAvoidanceVelocities(
		TConstArrayView<FManualAvoidanceAgent> Agents,
		float CellSizeCentimeters,
		float MinimumDistanceCentimeters,
		float MaximumHeightDifferenceCentimeters,
		float MovementSpeedCentimetersPerSecond,
		float AvoidanceStrength,
		FManualAvoidanceSpatialGrid& InOutSpatialGrid,
		TArray<FVector>& OutAvoidanceVelocities);

	/**
	 * Finds the exact Commander SupportedAgent config. A Default-only list returns
	 * null; radius mismatches are rejected instead of silently changing clearance.
	 */
	GULISTRIKE_API const FNavDataConfig* FindRequiredAgentConfig(
		TConstArrayView<FNavDataConfig> SupportedAgents);

	/**
	 * Resolves the named Commander NavData from the live navigation system.
	 * There is deliberately no GetDefaultNavDataInstance fallback.
	 */
	GULISTRIKE_API ANavigationData* ResolveRequiredNavigationData(
		UNavigationSystemV1& NavigationSystem);

	/** Stable, centered row-major slot geometry for a 25-Soldier transit formation. */
	GULISTRIKE_API FVector MakeFormationSlotOffset(
		int32 SlotIndex,
		float SpacingCentimeters,
		int32 RequestedColumnCount = MaximumFormationColumns);

	/** A target projection may move vertically, but never farther than one agent radius in XY. */
	GULISTRIKE_API bool IsProjectedTargetAcceptable(
		const FVector& RequestedTarget,
		const FVector& ProjectedTarget,
		float AgentRadiusCentimeters);

	/**
	 * Accepts a FindMoveAlongSurface result only when the query succeeded and the
	 * candidate remains finite and within the permitted vertical step.
	 */
	GULISTRIKE_API bool IsSurfaceMoveResultAcceptable(
		bool bSurfaceMoveSucceeded,
		const FVector& PreviousLocation,
		const FVector& CandidateLocation,
		float MaximumZDeltaCentimeters = MaximumSurfaceMoveZDeltaCentimeters);

	/**
	 * A navigation step makes progress only by advancing its monotonic path cursor
	 * or reducing the current-waypoint distance by at least the configured amount.
	 */
	GULISTRIKE_API bool HasMeaningfulNavigationProgress(
		int32 PreviousPathPointIndex,
		int32 CurrentPathPointIndex,
		float PreviousWaypointDistanceCentimeters,
		float CurrentWaypointDistanceCentimeters,
		float RequiredImprovementCentimeters = MinimumNavigationProgressCentimeters);

	/** Ignores zero/terminal orders and returns the one common executing order, or zero for none/mixed. */
	GULISTRIKE_API uint32 ResolveCommonActiveOrderId(TConstArrayView<uint32> ActiveOrderIds);

	/** Pure threshold gates used by the authority recovery state machine. */
	GULISTRIKE_API bool ShouldEnterCenterlineRecovery(
		int32 ConsecutiveSurfaceFailures,
		float NoProgressSeconds,
		int32 FailureThreshold = 2,
		float NoProgressThresholdSeconds = 1.0f);
	GULISTRIKE_API bool ShouldEnterPersonalPathRecovery(
		int32 TotalSurfaceFailures,
		float NoProgressSeconds,
		int32 FailureThreshold = 6,
		float NoProgressThresholdSeconds = 1.0f);
	GULISTRIKE_API bool ShouldBlockPersonalPathRecovery(
		int32 CompletedPathQueries,
		float NoProgressSeconds,
		int32 RequiredPathQueries = 2,
		float NoProgressThresholdSeconds = 2.0f);

	/**
	 * Returns the cached loose-arrival radius for a whole accepted batch. The ring
	 * count is the smallest k for which 1 + 3k(k + 1) can contain every member.
	 */
	GULISTRIKE_API float CalculateArrivalDomainRadiusCentimeters(
		int32 InitialAcceptedMemberCount,
		float AgentRadiusCentimeters,
		float PaddingCentimeters = 100.0f);

	/** Inner radius at which a member is released; the outer domain is the recovery threshold. */
	GULISTRIKE_API float CalculateLooseArrivalHoldRadiusCentimeters(
		float ArrivalDomainRadiusCentimeters,
		float HysteresisCentimeters = 100.0f);

	/** Largest safe frozen lane offset that still guarantees a discrete inner-domain crossing. */
	GULISTRIKE_API float CalculateLooseArrivalMaximumLaneOffsetCentimeters(
		float ArrivalDomainRadiusCentimeters,
		float HysteresisCentimeters,
		float AgentRadiusCentimeters,
		float MovementSpeedCentimetersPerSecond,
		float FixedDeltaSeconds);

	/**
	 * The final path point alone is not a terminal phase: a two-point open-ground
	 * path keeps its transit columns until the guide reaches the loose-arrival approach.
	 */
	GULISTRIKE_API bool HasEnteredLooseArrivalTerminalPhase(
		int32 PathPointIndex,
		int32 PathPointCount,
		const FVector& GuideAnchor,
		const FVector& TargetAnchor,
		float ArrivalDomainRadiusCentimeters,
		float ApproachPaddingCentimeters);

	/** Advances a per-member path cursor sequentially; total work is amortized per PathRevision. */
	GULISTRIKE_API int32 AdvanceMemberPathPointIndex(
		TConstArrayView<FVector> PathPoints,
		int32 CurrentPathPointIndex,
		const FVector& MemberLocation,
		float WaypointToleranceCentimeters,
		float MaximumCrossTrackCentimeters);

	/** O(1) final-turn test backed by the member's monotonic shared-path cursor. */
	GULISTRIKE_API bool HasClearedFinalTurn(
		const FFinalPathFrame& FinalPathFrame,
		int32 MemberPathPointIndex);

	/** Shared-path waypoint shifted by the member's current transit lane. */
	GULISTRIKE_API FVector CalculatePathLaneWaypoint(
		TConstArrayView<FVector> PathPoints,
		int32 PathPointIndex,
		float LateralOffsetCentimeters);

	/** Applies monotonic tail/release state and inner/outer arrival hysteresis. */
	GULISTRIKE_API FLooseArrivalMemberState UpdateLooseArrivalMemberState(
		const FLooseArrivalMemberState& PreviousState,
		bool bTailClearObserved,
		bool bFinalCorridorActive,
		float DistanceToTargetCentimeters,
		float ArrivalDomainRadiusCentimeters,
		float HysteresisCentimeters = 100.0f);

	/** Moving local waypoint that preserves a frozen lateral lane without a longitudinal slot. */
	GULISTRIKE_API FVector CalculateFinalCorridorLaneTarget(
		const FFinalPathFrame& FinalPathFrame,
		const FVector& TargetAnchor,
		const FVector& MemberLocation,
		float FrozenLateralOffsetCentimeters,
		float LookAheadCentimeters);

	/**
	 * Chooses the widest successful transit layout from columns 1..5. The array is
	 * indexed by ColumnCount - 1. If every probe fails, one column is the safe
	 * movement fallback; common-target projection acceptance is governed separately.
	 */
	GULISTRIKE_API int32 SelectTransitColumnCount(
		TConstArrayView<uint8> FitsByColumnCount);

	/**
	 * Narrows immediately. Widening requires uninterrupted successful movement
	 * steps and cannot occur until the minimum interval after the last rearrange.
	 */
	GULISTRIKE_API FTransitColumnHysteresisState UpdateTransitColumnHysteresis(
		const FTransitColumnHysteresisState& PreviousState,
		int32 WidestFittingColumnCount,
		bool bMovementStepSucceeded,
		double CurrentTimeSeconds,
		int32 RequiredExpansionSuccessSteps = RequiredTransitExpansionSuccessSteps,
		double MinimumRearrangementIntervalSeconds =
			MinimumTransitRearrangementIntervalSeconds);

	/**
	 * Resolves the last non-degenerate path direction. Repeated terminal points are
	 * skipped; an entirely degenerate path falls back to FallbackStart->FallbackEnd,
	 * then deterministic +X/0 degrees.
	 */
	GULISTRIKE_API FFinalPathFrame ResolveFinalPathFrame(
		TConstArrayView<FVector> PathPoints,
		const FVector& FallbackStart,
		const FVector& FallbackEnd);

	/** Ignores dead/superseded members and requires every active member's monotonic tail/arrival latches. */
	GULISTRIKE_API bool AreActiveMembersInsideArrivalDomainAndPastTail(
		const FFinalPathFrame& FinalPathFrame,
		TConstArrayView<FVector> PathPoints,
		const FVector& TargetAnchor,
		TConstArrayView<FFormationMemberProgressSample> Members,
		float ArrivalDomainRadiusCentimeters,
		float TailClearToleranceCentimeters);

	/** A loose move order completes only after the guide and every relevant member arrive. */
	GULISTRIKE_API bool ShouldCompleteOrder(
		bool bGuideAtFinal,
		const FFinalPathFrame& FinalPathFrame,
		TConstArrayView<FVector> PathPoints,
		const FVector& TargetAnchor,
		TConstArrayView<FFormationMemberProgressSample> Members,
		float ArrivalDomainRadiusCentimeters,
		float TailClearToleranceCentimeters);

	/** Every formation with active followers in a shared BatchOrderId must be ready. */
	GULISTRIKE_API bool ShouldCompleteBatchOrder(
		TConstArrayView<FBatchOrderFormationCompletionSample> Formations);
}
