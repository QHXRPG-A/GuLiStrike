// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"

/**
 * Pure, deterministic final-destination planning for Commander move orders.
 *
 * This layer deliberately has no UObject or navigation-system dependency. The
 * authority builds a candidate plan here, then validates/projects every world
 * candidate against the CommanderSoldier NavMesh on the game thread.
 */
namespace GuLiCommanderDestinationPlanner
{
	inline constexpr int32 MembersPerBlock = 25;
	inline constexpr int32 ColumnsPerBlock = 5;
	inline constexpr float DefaultFreeCandidatePitchCentimeters = 1800.0f;
	inline constexpr float DefaultFreeCandidateRadiusCentimeters = 45000.0f;
	inline constexpr float DefaultSoftAnchorPitchCentimeters = 9000.0f;
	inline constexpr int32 DefaultFreeCandidateCount = 2263;

	struct GULISTRIKE_API FMemberInput
	{
		FGuLiSoldierId SoldierId;
		FVector Location = FVector::ZeroVector;
		FVector Facing = FVector::ZeroVector;
	};

	struct GULISTRIKE_API FCohortInput
	{
		FGuLiControlCohortId CohortId;
		TArray<FMemberInput> Members;
	};

	struct GULISTRIKE_API FRequest
	{
		TArray<FCohortInput> Cohorts;
		FVector TargetAnchor = FVector::ZeroVector;
		float MemberSpacingCentimeters = 1800.0f;
	};

	struct GULISTRIKE_API FMemberCandidate
	{
		FGuLiSoldierId SoldierId;

		/** Stable row-major index in the final five-by-five block (0..24). */
		int32 SlotIndex = INDEX_NONE;

		/** Offset from this member's cohort block center (X forward, Y right). */
		FVector BlockLocalOffset = FVector::ZeroVector;

		/** Offset from the requested target anchor (X forward, Y right). */
		FVector BatchLocalOffset = FVector::ZeroVector;

		/** Unprojected candidate supplied to the authority's NavMesh validation. */
		FVector WorldCandidate = FVector::ZeroVector;
	};

	struct GULISTRIKE_API FCohortCandidate
	{
		FGuLiControlCohortId CohortId;
		int32 BlockIndex = INDEX_NONE;
		FVector SourceCentroid = FVector::ZeroVector;

		/** Offset from the target anchor (X forward, Y right). */
		FVector BatchLocalOffset = FVector::ZeroVector;
		FVector WorldBlockCenter = FVector::ZeroVector;
		TArray<FMemberCandidate> Members;
	};

	struct GULISTRIKE_API FPlan
	{
		FVector TargetAnchor = FVector::ZeroVector;
		FVector SelectionCentroid = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		FVector Right = FVector::RightVector;
		float YawDegrees = 0.0f;
		float MemberSpacingCentimeters = 0.0f;
		float BlockPitchCentimeters = 0.0f;
		TArray<FCohortCandidate> Cohorts;

		void Reset();
	};

	/**
	 * One world-axis hex-lattice point before navigation projection.
	 * CandidateIndex is assigned after the deterministic center-first sort.
	 */
	struct GULISTRIKE_API FFreeDestinationCandidate
	{
		int32 CandidateIndex = INDEX_NONE;
		int32 AxialQ = 0;
		int32 AxialR = 0;
		FVector AnchorLocalOffset = FVector::ZeroVector;
		FVector WorldCandidate = FVector::ZeroVector;
		double DistanceSquaredFromAnchor = 0.0;
	};

	struct GULISTRIKE_API FHexCandidateRequest
	{
		FVector TargetAnchor = FVector::ZeroVector;
		float CandidatePitchCentimeters = DefaultFreeCandidatePitchCentimeters;
		float MaximumRadiusCentimeters = DefaultFreeCandidateRadiusCentimeters;
	};

	/**
	 * Builds the complete unprojected world-axis hex lattice inside the radius.
	 * Results are sorted by original 2D distance, AxialQ, then AxialR.
	 */
	GULISTRIKE_API bool BuildHexCandidates(
		const FHexCandidateRequest& Request,
		TArray<FFreeDestinationCandidate>& OutCandidates);

	/**
	 * Returns an exclusive center-first prefix end containing at least
	 * MinimumCandidateCount candidates without splitting an equal-distance shell.
	 * Candidates must use the deterministic ordering produced by BuildHexCandidates.
	 */
	GULISTRIKE_API int32 FindProjectionPrefixEnd(
		TConstArrayView<FFreeDestinationCandidate> Candidates,
		int32 MinimumCandidateCount);

	/** A routing-locality hint. It is never a required final destination. */
	struct GULISTRIKE_API FSoftCohortAnchor
	{
		FGuLiControlCohortId CohortId;
		int32 AnchorIndex = INDEX_NONE;
		FVector SourceCentroid = FVector::ZeroVector;
		FVector BatchLocalOffset = FVector::ZeroVector;
		FVector WorldAnchor = FVector::ZeroVector;
	};

	struct GULISTRIKE_API FSoftAnchorPlan
	{
		FVector TargetAnchor = FVector::ZeroVector;
		FVector SelectionCentroid = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		FVector Right = FVector::RightVector;
		float YawDegrees = 0.0f;
		float AnchorPitchCentimeters = 0.0f;
		TArray<FSoftCohortAnchor> Cohorts;

		void Reset();
	};

	/**
	 * Builds deterministic cohort soft anchors and matches source cohorts to
	 * them by minimum centroid travel. No anchor is a navigation requirement.
	 */
	GULISTRIKE_API bool BuildSoftCohortAnchors(
		const FRequest& Request,
		float AnchorPitchCentimeters,
		FSoftAnchorPlan& OutPlan);

	/**
	 * A candidate that survived authority-side projection, de-duplication,
	 * spacing, reservation, and any other world validation.
	 */
	struct GULISTRIKE_API FFreeDestinationSlot
	{
		int32 CandidateIndex = INDEX_NONE;
		int32 AxialQ = 0;
		int32 AxialR = 0;
		FVector OriginalWorldCandidate = FVector::ZeroVector;
		FVector WorldDestination = FVector::ZeroVector;
	};

	struct GULISTRIKE_API FFreeMemberAssignment
	{
		FGuLiSoldierId SoldierId;
		int32 CandidateIndex = INDEX_NONE;
		FVector WorldDestination = FVector::ZeroVector;
	};

	struct GULISTRIKE_API FFreeCohortAssignment
	{
		FGuLiControlCohortId CohortId;
		int32 RequestedMemberCount = 0;
		int32 SoftAnchorIndex = INDEX_NONE;
		FVector SourceCentroid = FVector::ZeroVector;
		FVector WorldSoftAnchor = FVector::ZeroVector;
		TArray<FFreeMemberAssignment> Members;
	};

	struct GULISTRIKE_API FFreeAssignmentPlan
	{
		FVector TargetAnchor = FVector::ZeroVector;
		int32 RequestedMemberCount = 0;
		int32 AssignedMemberCount = 0;
		TArray<FFreeCohortAssignment> Cohorts;

		void Reset();
		bool IsComplete() const
		{
			return RequestedMemberCount > 0
				&& AssignedMemberCount == RequestedMemberCount;
		}
	};

	/**
	 * Assigns authority-filtered free slots to cohorts and members. Slots are
	 * consumed center-first; each goes to the nearest non-full soft anchor,
	 * then members are matched by minimum 2D travel. A valid request may return
	 * a partial or empty assignment when fewer usable slots were supplied.
	 */
	GULISTRIKE_API bool AssignFreeDestinations(
		const FRequest& Request,
		TConstArrayView<FFreeDestinationSlot> AvailableSlots,
		float SoftAnchorPitchCentimeters,
		FFreeAssignmentPlan& OutPlan);

	/**
	 * Legacy fixed-block compatibility entry point. New move-order code should
	 * use BuildHexCandidates, authority-side filtering, then
	 * AssignFreeDestinations. Inputs are normalized by CohortId and SoldierId,
	 * so equivalent input permutations return the same assignment.
	 */
	GULISTRIKE_API bool Build(const FRequest& Request, FPlan& OutPlan);
}
