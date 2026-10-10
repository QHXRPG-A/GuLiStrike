// Copyright Epic Games, Inc. All Rights Reserved.

#include "GuLiStrikeWeaponPart.h"

bool UGuLiStrikeWeaponPart::GetMuzzleTransformRelativeToPart(FTransform& OutTransform) const
{
	if (!MuzzleSocketName.IsNone()) { return GetVisualSocketTransform(MuzzleSocketName, OutTransform); }
	OutTransform = FTransform(MuzzleOffset);
	return true;
}
