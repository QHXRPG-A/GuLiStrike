#pragma once
#include "CoreMinimal.h"
class UWorld;

namespace GuLiCommanderCameraGeometry
{
	GULISTRIKE_API bool GetBattleBounds(const UWorld* World, FBox& OutBounds);
	/** Finite distance enclosing the battlefield from this ray origin; shared by client and authority. */
	GULISTRIKE_API double SelectionRayLength(const FVector& Origin, const UWorld* World);
}
