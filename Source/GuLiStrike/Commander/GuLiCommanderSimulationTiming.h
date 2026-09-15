// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

/** Shared time contract for authority steps and consumers of ServerSimTick. */
namespace GuLiCommanderSimulationTiming
{
	inline constexpr uint32 RateHz = 10u;
	inline constexpr float StepSeconds = 1.0f / RateHz;
}
