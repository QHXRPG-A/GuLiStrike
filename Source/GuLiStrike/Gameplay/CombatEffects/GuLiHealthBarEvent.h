#pragma once

#include "CoreTypes.h"
#include "Math/UnrealMathUtility.h"

/** Display values for an active bar. Generation identifies this entry until it is removed. */
struct FGuLiHealthBarEvent
{
	double LastChange = -1;
	uint64 Generation = 0;
	float HealthFraction = 0;
	float ShieldFraction = 0;
	float MaxHealth = 0;
	float MaxShield = 0;
	double LastDamage = -1;

	static bool LostVitals(float OldHealth, float OldShield, float Health, float MaxHP, float Shield, float MaxSP)
	{
		return Health < FMath::Min(OldHealth, MaxHP) || Shield < FMath::Min(OldShield, MaxSP);
	}
};
