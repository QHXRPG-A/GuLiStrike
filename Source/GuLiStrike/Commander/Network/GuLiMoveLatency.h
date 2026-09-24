#pragma once

#include "CoreMinimal.h"

/** Diagnostic-only correlation. No wire fields, RPCs, gameplay decisions, or cross-host subtraction. */
namespace GuLiMoveLatency
{
	struct FContext
	{
		FGuid Player;
		uint32 Epoch = 0, Command = 0, Execution = 0, Batch = 0;
		uint64 Plan = 0;
	};
	GULISTRIKE_API bool IsEnabled();
	GULISTRIKE_API FContext Context(const UObject* Object, uint32 Command = 0, uint32 Batch = 0);
	GULISTRIKE_API void Record(const TCHAR* Stage, const FContext& Context, int32 Members = 0, const TCHAR* Detail = TEXT(""));
}
