#pragma once

#include "CoreTypes.h"

namespace GuLiEngineeringCollision
{
    // Movement queries ignore engineering bodies without changing Pawn selection/damage queries.
    inline constexpr uint8 VehicleMask = 1 << 0;

    // Classification comes from the shared engineering travel interface, never a vehicle type ID.
    inline constexpr bool IgnoresPeer(bool bEngineering, bool bOtherEngineering)
    {
        return bEngineering && bOtherEngineering;
    }
}
