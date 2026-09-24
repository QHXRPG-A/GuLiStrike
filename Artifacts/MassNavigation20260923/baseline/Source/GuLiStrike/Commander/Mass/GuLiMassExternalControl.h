#pragma once
#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"

/** Stable identity, independent of a world's transient FMassEntityHandle. */
struct FGuLiMassExternalUnit
{
	FGuLiSoldierId Id;
	FTransform Transform;
	float Radius = 0;
};
