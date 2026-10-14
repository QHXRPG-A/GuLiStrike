#pragma once
#include "GuLiNetProtocol.h"

namespace GuLiNet
{
// The transport and handoff codecs know only this envelope. Gameplay owns
// each payload and its version; unknown required capabilities are never skipped.
struct CapabilityState
{
    std::uint32_t Version=0;
    Bytes Data;
    bool operator==(const CapabilityState&) const=default;
};
using CapabilityStates=std::map<std::string,CapabilityState>;
inline constexpr std::uint32_t MaximumObjectCapabilities=32;
inline constexpr std::uint32_t MaximumCapabilityBytes=65536;
inline constexpr std::uint32_t MaximumCapabilityObjects=4096;
GULINETCORE_API void WriteCapabilityStates(Writer& W,const CapabilityStates& Values);
GULINETCORE_API bool ReadCapabilityStates(Reader& R,CapabilityStates& Values);
}
