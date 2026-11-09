#pragma once
#include "GuLiNetTypes.h"
#include <span>
#include <string_view>

namespace GuLiNet
{
// Incremental canonical digest for retained local directories. Avoid a second
// directory-sized allocation or applying a network message limit to history.
class GULINETCORE_API Sha256Stream
{
public:
    void Append(std::span<const std::uint8_t> Data);
    std::string Finish() const;
private:
    std::array<std::uint32_t,8> State{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::array<std::uint8_t,64> Pending{};
    std::size_t PendingSize=0;
    std::uint64_t Length=0;
};
// SHA-256 over explicit wire bytes, shared by the editor exporter, peers and service.
GULINETCORE_API std::string Sha256(std::span<const std::uint8_t> Data);
GULINETCORE_API bool IsSha256(std::string_view Text);
GULINETCORE_API Id StableId(std::string_view Namespace, std::string_view Name);
}
