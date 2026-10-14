#pragma once
#include "CoreMinimal.h"

/** One version for curved flight, field, burst, pending damage and cooldown recovery. */
namespace GuLiEffectRuntimeContract
{
    inline constexpr uint32 Version=1;
    inline constexpr uint32 MaximumRecords=16384;
    inline constexpr uint32 MaximumPendingHits=16384;
    inline constexpr uint32 MaximumBytes=32*1024*1024;
}
