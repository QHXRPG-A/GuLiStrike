#pragma once

#include "CoreTypes.h"

namespace GuLiCircleCollisionFiltering
{
// Only movement queries opt out of the original physics geometry. Weapon,
// selection and flight queries retain their existing channel responses.
inline constexpr uint8 CircularBoundaryMask = 1 << 1;
}
