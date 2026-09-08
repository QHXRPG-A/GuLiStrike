#pragma once
#include "CoreMinimal.h"
class UWorld;
struct FGuLiWingmanGroundRunPath;

namespace GuLiWingmanAttack
{
	/** Shared client planning/server admission gate for a complete dive, turn and climb. */
	GULISTRIKE_API bool GroundRunClearsTerrain(UWorld* World, const FGuLiWingmanGroundRunPath& Path, float Radius);
	/** FlightNav plus physical WorldStatic sweep for one planned dogfight leg. */
	GULISTRIKE_API bool AirSegmentClearsWorld(UWorld* World, const FVector& Start, const FVector& End, float Radius);
}
