#include "Commander/Mass/GuLiMassMovementTuning.h"

float GuLiMassMovementTuning::GetLookaheadSeconds() { return .5f; }
int32 GuLiMassMovementTuning::GetCandidateLimit() { return 6; }
float GuLiMassMovementTuning::GetCacheLifetimeSeconds() { return .3f; }
float GuLiMassMovementTuning::GetTurnRateScale() { return 3.f; }
