#pragma once
#include "GuLiNetCapability.h"
namespace GuLiNet::Replacement
{
inline constexpr const char* Capability="replacement";
inline constexpr std::uint32_t Version=1;
GULINETCORE_API ObjectKey Child(ObjectKey Source,Id Event,ObjectKind Kind);
GULINETCORE_API bool Result(const ObjectRecord&,ObjectKey&);
}
