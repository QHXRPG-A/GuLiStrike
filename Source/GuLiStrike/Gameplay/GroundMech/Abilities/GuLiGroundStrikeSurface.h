#pragma once

#include "CoreMinimal.h"

/** One local height sample. Reconstructed at the previous verified position after an ownership change. */
struct FGuLiGroundStrikeHeightCache
{
	TOptional<FPlane> Support;
	bool bInitialized = false;
};

namespace GuLiGroundStrikeSurface
{
	/** Highest upward-facing support at XY below the supplied ceiling. */
	bool HeightBelow(UWorld& World, const FVector& Ceiling, double& OutHeight);
	/** Returns the first contact from above, using only vertical surface heights. */
	bool FindContact(UWorld& World, TConstArrayView<FVector> Path, FGuLiGroundStrikeHeightCache& Cache, FVector& OutContact);
}
