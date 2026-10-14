#pragma once
#include "GuLiNetTypes.h"
#include <span>
#include <string_view>

namespace GuLiNet
{
// SHA-256 over explicit wire bytes, shared by the editor exporter, peers and service.
GULINETCORE_API std::string Sha256(std::span<const std::uint8_t> Data);
GULINETCORE_API bool IsSha256(std::string_view Text);
GULINETCORE_API Id StableId(std::string_view Namespace, std::string_view Name);
}
