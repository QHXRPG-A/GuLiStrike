#pragma once
#include "CoreMinimal.h"

class UWorld;

/** Shared terrain/navigation validation for point skills and tactical previews. */
namespace GuLiSkillTargeting
{
	bool ResolveGround(UWorld& World, const FVector& Point, FVector& Out, double* OutSurfaceHeight = nullptr);
}
