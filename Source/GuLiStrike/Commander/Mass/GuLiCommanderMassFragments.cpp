// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/GuLiCommanderMassFragments.h"

bool FGuLiMassHealthFragment::IsAlive() const
{
	return !bDead && Health > 0u;
}
