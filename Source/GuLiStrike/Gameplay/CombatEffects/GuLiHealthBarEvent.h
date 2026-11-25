#pragma once

#include "CoreTypes.h"

/** Display values for an active bar. Generation identifies this entry until it is removed. */
struct FGuLiHealthBarEvent
{
	double LastChange = -1;
	uint64 Generation = 0;
	float HealthFraction = 0;
	float ShieldFraction = 0;
	bool bHasShield = false;
};
