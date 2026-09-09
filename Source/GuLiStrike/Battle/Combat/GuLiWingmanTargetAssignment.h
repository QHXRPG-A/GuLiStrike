// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"

namespace GuLiWingmanTargetAssignment
{
	inline constexpr int64 DefaultSwitchPenaltyCentimeters = 10000;
}

/** One legal member/target edge supplied to the pure assignment solver. */
struct GULISTRIKE_API FGuLiWingmanTargetAssignmentEdge
{
	FGuLiWingmanHandle Emitter;
	FGuLiTargetHandle Target;
	int64 DistanceCentimeters = 0;
	bool bSwitchesFromRetainableTarget = false;
};

/** Complete immutable input for one Ship assignment scan. */
struct GULISTRIKE_API FGuLiWingmanTargetAssignmentProblem
{
	TArray<FGuLiWingmanHandle> Members;
	TArray<FGuLiTargetHandle> Targets;
	TArray<FGuLiWingmanTargetAssignmentEdge> LegalEdges;
	int64 SwitchPenaltyCentimeters =
		GuLiWingmanTargetAssignment::DefaultSwitchPenaltyCentimeters;
};

/** One final member assignment. RoundIndex is zero-based. */
struct GULISTRIKE_API FGuLiWingmanTargetAssignmentPair
{
	FGuLiWingmanHandle Emitter;
	FGuLiTargetHandle Target;
	int64 CostCentimeters = 0;
	int32 RoundIndex = INDEX_NONE;
};

namespace GuLiWingmanTargetAssignment
{
	GULISTRIKE_API bool IsStableMemberLess(
		const FGuLiWingmanHandle& Lhs,
		const FGuLiWingmanHandle& Rhs);
	GULISTRIKE_API bool IsStableTargetLess(
		const FGuLiTargetHandle& Lhs,
		const FGuLiTargetHandle& Rhs);

	/**
	 * Runs deterministic multi-round rectangular Hungarian assignment.
	 * A valid problem with no legal matches succeeds with an empty result.
	 */
	GULISTRIKE_API bool Solve(
		const FGuLiWingmanTargetAssignmentProblem& Problem,
		TArray<FGuLiWingmanTargetAssignmentPair>& OutAssignments);
}
