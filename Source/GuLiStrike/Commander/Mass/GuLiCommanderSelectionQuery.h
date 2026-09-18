// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"

/** Pure authoritative selection rules, shared by the runtime resolver and native tests. */
namespace GuLiCommanderSelectionQuery
{
	struct FCandidate
	{
		FGuLiSoldierId SoldierId;
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		EGuLiTeam Team = EGuLiTeam::Unassigned;
		uint16 UnitTypeId = GULI_DEFAULT_SOLDIER_UNIT_TYPE_ID;
		bool bAlive = true;
		/** Effective world bounds from the same model definition used by presentation. */
		FBox WorldBounds = FBox(ForceInit);
	};

	/** A rejected point hint never falls back to a different soldier. Empty areas are accepted. */
	GULISTRIKE_API bool ResolveCandidates(
		const FGuLiSelectionRequest& Request,
		EGuLiTeam Team,
		TConstArrayView<FCandidate> Population,
		TArray<FGuLiSoldierId>& OutIds);

	/** Deduplicated, ID-sorted membership. Add preserves old members when the protocol cap is reached. */
	GULISTRIKE_API void CombineMembership(
		TConstArrayView<FGuLiSoldierId> ExistingIds,
		TConstArrayView<FGuLiSoldierId> HitIds,
		EGuLiSelectionModifier Modifier,
		TArray<FGuLiSoldierId>& OutIds);
}
