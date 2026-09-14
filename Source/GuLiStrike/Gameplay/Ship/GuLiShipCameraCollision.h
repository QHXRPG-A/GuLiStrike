// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace GuLiShipCameraCollision
{
	/** Applies the nearest swept blocking distance without extending the requested arm or crossing it to honor a soft minimum. */
	GULISTRIKE_API float ConstrainArmToBlockingDistance(
		float RequestedArmLength,
		float BlockingDistance,
		float MinimumArmLength);
}
