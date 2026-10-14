#pragma once
#include "GuLiNetCapability.h"
namespace GuLiNet::Population
{
inline constexpr const char* Capability="population";
inline constexpr std::uint32_t Version=1;
// An unfinished carrier reserves the same units as its eventual replacement.
// The team owner commits this claim and its population account atomically.
struct Claim {std::string Group;std::uint32_t Units=0,Limit=0;};
GULINETCORE_API bool Read(const ObjectRecord&,Claim&);
GULINETCORE_API std::uint64_t Used(const std::map<ObjectKey,ObjectRecord>&,Team,const std::string&);
}
