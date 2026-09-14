// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/GuLiShipCameraCollision.h"

float GuLiShipCameraCollision::ConstrainArmToBlockingDistance(
	const float RequestedArmLength,
	const float BlockingDistance,
	const float MinimumArmLength)
{
	if (!FMath::IsFinite(RequestedArmLength) || RequestedArmLength <= 0.0f)
	{
		return 0.0f;
	}
	if (!FMath::IsFinite(BlockingDistance) || BlockingDistance < 0.0f
		|| BlockingDistance >= RequestedArmLength)
	{
		return RequestedArmLength;
	}
	const float SafeBlockingDistance = FMath::Max(0.0f, BlockingDistance);
	const float SoftMinimum = FMath::IsFinite(MinimumArmLength)
		? FMath::Max(0.0f, MinimumArmLength)
		: 0.0f;
	// The configured minimum is a comfort preference, not permission to place the
	// camera beyond a closer Landscape or Ship surface.
	return SafeBlockingDistance < SoftMinimum
		? SafeBlockingDistance
		: FMath::Min(RequestedArmLength, FMath::Max(SoftMinimum, SafeBlockingDistance));
}
