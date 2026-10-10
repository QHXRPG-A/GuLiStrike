#pragma once

#include "CoreMinimal.h"

/** Fixed production settings. No runtime switch, replicated field or RPC. */
namespace GuLiMassMovementTuning
{
	GULISTRIKE_API float GetLookaheadSeconds();
	GULISTRIKE_API int32 GetCandidateLimit();
	GULISTRIKE_API float GetCacheLifetimeSeconds();
	GULISTRIKE_API float GetTurnRateScale();
}
