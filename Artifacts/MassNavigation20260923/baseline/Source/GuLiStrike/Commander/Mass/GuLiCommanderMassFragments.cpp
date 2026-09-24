// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/GuLiCommanderMassFragments.h"

bool FGuLiMassHealthFragment::IsAlive() const
{
	return !bDead && FMath::IsFinite(Health) && Health > 0.0f;
}
