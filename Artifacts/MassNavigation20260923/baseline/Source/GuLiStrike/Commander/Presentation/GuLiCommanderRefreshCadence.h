#pragma once

#include "CoreMinimal.h"

/** Client maintenance cadence. A hitch consumes one latest update, never a catch-up loop. */
struct FGuLiCommanderRefreshCadence
{
	static constexpr double IntervalSeconds = 0.1;
	double NextSeconds = 0.0;
	bool Consume(double Now)
	{
		if (!FMath::IsFinite(Now) || Now + 1.e-6 < NextSeconds) return false;
		const double Steps = FMath::FloorToDouble(FMath::Max(0.0, Now - NextSeconds + 1.e-6) / IntervalSeconds) + 1.0;
		NextSeconds += Steps * IntervalSeconds;
		return true;
	}
	void Reset() { NextSeconds = 0.0; }
};
