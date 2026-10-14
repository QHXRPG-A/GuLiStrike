#include "GuLiNetCooldown.h"
#include <algorithm>
#include <limits>

namespace GuLiNet
{
namespace
{
bool ValidKey(const CooldownKey& Key)
{return !Key.Domain.empty() && Key.Domain.size()<=256 && !Key.Name.empty() && Key.Name.size()<=256;}
constexpr Tick MaximumDuration=60ull*86400*30;
}
bool ValidCooldownState(const CooldownState& V)
{
    if (!V.Version || V.Active.size()>CooldownBook::MaximumEntries) return false;
    for (const auto& [K,R]:V.Active)
        if (!ValidKey(K) || !R.Token.Valid() || R.StartedAt>V.At || R.ExpiresAt<=V.At
            || R.ExpiresAt<=R.StartedAt || R.ExpiresAt-R.StartedAt>MaximumDuration) return false;
    return true;
}
bool CooldownBook::Advance(Tick Now)
{
    if (Now<State.At) return false;
    const bool Expiring=std::any_of(State.Active.begin(),State.Active.end(),[&](const auto& P){return P.second.ExpiresAt<=Now;});
    if (Expiring && State.Version==UINT64_MAX) return false;
    State.At=Now;
    if (Expiring) {std::erase_if(State.Active,[&](const auto& P){return P.second.ExpiresAt<=Now;});++State.Version;}
    return true;
}
CooldownDecision CooldownBook::Reserve(CooldownKey Key,Id Token,Tick Duration,Tick Now)
{
    if (!ValidKey(Key) || !Token.Valid() || !Duration || Duration>MaximumDuration || Now>UINT64_MAX-Duration || Now<State.At)
        return CooldownDecision::Invalid;
    if (!Advance(Now)) return CooldownDecision::Exhausted;
    if (const auto* Old=Find(Key))
        return Old->Token==Token && Old->ExpiresAt-Old->StartedAt==Duration?CooldownDecision::Duplicate:CooldownDecision::Busy;
    if (State.Active.size()>=MaximumEntries || State.Version==UINT64_MAX) return CooldownDecision::Exhausted;
    State.Active.emplace(std::move(Key),CooldownReservation{Token,Now,Now+Duration});++State.Version;return CooldownDecision::Reserved;
}
bool CooldownBook::Rollback(const CooldownKey& Key,Id Token)
{
    const auto* Old=Find(Key);if (!Old || Old->Token!=Token || State.Version==UINT64_MAX) return false;
    State.Active.erase(Key);++State.Version;return true;
}
bool CooldownBook::ClearDomain(std::string_view Domain)
{
    const bool Found=std::any_of(State.Active.begin(),State.Active.end(),[&](const auto& P){return P.first.Domain==Domain;});
    if (!Found) return true;if (State.Version==UINT64_MAX) return false;
    std::erase_if(State.Active,[&](const auto& P){return P.first.Domain==Domain;});++State.Version;return true;
}
const CooldownReservation* CooldownBook::Find(const CooldownKey& Key) const
{const auto It=State.Active.find(Key);return It==State.Active.end()?nullptr:&It->second;}
bool CooldownBook::Restore(CooldownState Value)
{if (!ValidCooldownState(Value)) return false;State=std::move(Value);return true;}
Bytes EncodeCooldownState(const CooldownState& Value)
{
    if (!ValidCooldownState(Value)) return {};Writer W(1024*1024);W.U32(1);W.U64(Value.At);W.U64(Value.Version);W.U32(std::uint32_t(Value.Active.size()));
    for (const auto& [K,R]:Value.Active) {W.Text(K.Domain);W.Text(K.Name);W.Identity(R.Token);W.U64(R.StartedAt);W.U64(R.ExpiresAt);}
    return W.Take();
}
bool DecodeCooldownState(std::span<const std::uint8_t> B,CooldownState& Out)
{
    if (B.empty() || B.size()>1024*1024) return false;Reader R(B);CooldownState V;std::uint32_t Schema=0,Count=0;
    if (!R.U32(Schema) || Schema!=1 || !R.U64(V.At) || !R.U64(V.Version) || !R.U32(Count) || Count>CooldownBook::MaximumEntries) return false;
    for (std::uint32_t I=0;I<Count;++I)
    {
        CooldownKey K;CooldownReservation C;
        if (!R.Text(K.Domain,256) || !R.Text(K.Name,256) || !R.Identity(C.Token) || !R.U64(C.StartedAt) || !R.U64(C.ExpiresAt)
            || !V.Active.emplace(std::move(K),C).second) return false;
    }
    if (!R.Done() || !ValidCooldownState(V)) return false;Out=std::move(V);return true;
}
}
