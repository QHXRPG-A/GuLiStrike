#pragma once

#include "CoreTypes.h"

namespace GuLiEngineeringCollision
{
    // Movement queries ignore engineering bodies without changing Pawn selection/damage queries.
    inline constexpr uint8 VehicleMask = 1 << 0;

}
