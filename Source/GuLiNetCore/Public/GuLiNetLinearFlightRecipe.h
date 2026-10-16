#pragma once
#include "GuLiNetCapability.h"

namespace GuLiNet::LinearFlightRecipe
{
inline constexpr const char* Name="linear_flight_recipe";
inline constexpr std::uint32_t Version=1;
struct Recipe {ObjectKey Flight,Source;Id Admission;Bytes State;};
GULINETCORE_API Bytes Encode(const Recipe& Value);
GULINETCORE_API bool Decode(std::span<const std::uint8_t> Bytes,Recipe& Out);
}
