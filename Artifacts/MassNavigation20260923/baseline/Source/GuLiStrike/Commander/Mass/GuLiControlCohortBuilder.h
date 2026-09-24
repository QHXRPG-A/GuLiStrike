// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"

namespace GuLiControlCohortBuilder
{
	struct FCandidate
	{
		FGuLiSoldierId SoldierId;
		FVector Location = FVector::ZeroVector;
	};

	/**
	 * Partitions every in-circle seed into deterministic proximity cohorts. Only
	 * the final under-strength cohort is filled with nearby, unused allies.
	 */
	GULISTRIKE_API void Build(
		TConstArrayView<FCandidate> InCircleSeeds,
		TConstArrayView<FCandidate> FillCandidates,
		float MaximumFillDistanceCentimeters,
		TArray<TArray<FGuLiSoldierId>>& OutCohorts);
}
