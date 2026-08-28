// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
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
	inline constexpr float RequiredAgentRadiusCentimeters = 750.0f;

	/** Frozen terminal coordinate frame derived from the last usable NavMesh path segment. */
	struct GULISTRIKE_API FFinalPathFrame
	{
		FVector Forward = FVector::ForwardVector;
		FVector TailClearOrigin = FVector::ZeroVector;
		double TailClearPathDistanceCentimeters = 0.0;
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
	};

	/** One accepted formation's contribution to its shared batch completion gate. */
	struct GULISTRIKE_API FBatchOrderFormationCompletionSample
	{
		bool bHasActiveMembers = false;
		bool bPathValid = false;
		bool bGuideAndMembersReady = false;
	};

	/** The only SupportedAgent name legal for Commander movement. */
	GULISTRIKE_API FName GetRequiredAgentName();

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
	 * Returns the cached loose-arrival radius for a whole accepted batch. The ring
	 * count is the smallest k for which 1 + 3k(k + 1) can contain every member.
	 */
	GULISTRIKE_API float CalculateArrivalDomainRadiusCentimeters(
		int32 InitialAcceptedMemberCount,
		float AgentRadiusCentimeters,
		float PaddingCentimeters = 500.0f);

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

	/**
	 * Chooses the widest successful transit layout from columns 1..5. The array is
	 * indexed by ColumnCount - 1. If every probe fails, one column is the safe
	 * movement fallback; common-target projection acceptance is governed separately.
	 */
	GULISTRIKE_API int32 SelectTransitColumnCount(
		TConstArrayView<uint8> FitsByColumnCount);

	/**
	 * Resolves the last non-degenerate path direction. Repeated terminal points are
	 * skipped; an entirely degenerate path falls back to FallbackStart->FallbackEnd,
	 * then deterministic +X/0 degrees.
	 */
	GULISTRIKE_API FFinalPathFrame ResolveFinalPathFrame(
		TConstArrayView<FVector> PathPoints,
		const FVector& FallbackStart,
		const FVector& FallbackEnd);

	/** Ignores dead/superseded members and requires every active member inside the domain and past the last turn. */
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

	/** Per-member direction along the one shared path, used while the tail clears the final turn. */
	GULISTRIKE_API FVector CalculateSharedPathFollowDirection(
		TConstArrayView<FVector> PathPoints,
		const FVector& MemberLocation,
		float LookAheadCentimeters);
}
