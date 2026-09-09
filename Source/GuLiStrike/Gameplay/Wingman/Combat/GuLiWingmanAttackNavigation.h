#pragma once
#include "CoreMinimal.h"
class UWorld;
struct FGuLiWingmanGroundRunPath;
enum class EGuLiFlightNavSegmentStatus : uint8;

namespace GuLiWingmanAttack
{
	enum class EGroundPathRejectReason : uint8
	{
		None = 0,
		InvalidInput,
		Navigation,
		StaticObstacle,
		MissingGround,
		GroundClearance
	};

	/** Shared client planning/server admission gate for a complete dive, turn and climb. */
	GULISTRIKE_API bool GroundRunClearsTerrain(UWorld* World, const FGuLiWingmanGroundRunPath& Path,
		float Radius, EGroundPathRejectReason* OutRejectReason = nullptr,
		EGuLiFlightNavSegmentStatus* OutNavigationStatus = nullptr);
	/** FlightNav plus physical WorldStatic sweep for one planned dogfight leg. */
	GULISTRIKE_API bool AirSegmentClearsWorld(UWorld* World, const FVector& Start, const FVector& End, float Radius);
}
