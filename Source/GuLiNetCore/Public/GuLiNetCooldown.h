#pragma once
#include "GuLiNetProtocol.h"
#include <string_view>

namespace GuLiNet
{
struct CooldownKey
{
    std::string Domain,Name;
    friend bool operator==(const CooldownKey&,const CooldownKey&)=default;
    friend bool operator<(const CooldownKey& A,const CooldownKey& B)
    {return std::tie(A.Domain,A.Name)<std::tie(B.Domain,B.Name);}
};
struct CooldownReservation
{
    Id Token;
    Tick StartedAt=0,ExpiresAt=0;
};
struct CooldownState
{
    Tick At=0;
    Revision Version=1;
    std::map<CooldownKey,CooldownReservation> Active;
};
enum class CooldownDecision : std::uint8_t { Reserved,Duplicate,Busy,Invalid,Exhausted };

/** Logical-time reservation book shared by gameplay capabilities and recovery. */
class GULINETCORE_API CooldownBook
{
public:
    // Even maximum-length names must fit in a bounded actor capability shard.
    static constexpr std::size_t MaximumEntries=128;
    CooldownDecision Reserve(CooldownKey Key,Id Token,Tick Duration,Tick Now);
    bool Rollback(const CooldownKey& Key,Id Token);
    bool ClearDomain(std::string_view Domain);
    bool Advance(Tick Now);
    const CooldownReservation* Find(const CooldownKey& Key) const;
    const CooldownState& Snapshot() const {return State;}
    bool Restore(CooldownState Value);
private:
    CooldownState State;
};
GULINETCORE_API bool ValidCooldownState(const CooldownState& Value);
GULINETCORE_API Bytes EncodeCooldownState(const CooldownState& Value);
GULINETCORE_API bool DecodeCooldownState(std::span<const std::uint8_t> BytesValue,CooldownState& Out);
}
