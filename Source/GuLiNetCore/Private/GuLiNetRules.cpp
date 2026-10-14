#include "GuLiNetRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace GuLiNet
{
bool ValidHealth(const Health& S)
{
    return S.Version && std::isfinite(S.Value) && std::isfinite(S.Maximum)
        && std::isfinite(S.Shield) && std::isfinite(S.MaximumShield)
        && S.Maximum > 0 && S.Value >= 0 && S.Value <= S.Maximum
        && S.Shield >= 0 && S.MaximumShield >= S.Shield && S.Dead == (S.Value <= 0);
}
float AbsorbShield(Health& State, float Amount)
{
    if (!std::isfinite(Amount) || Amount < 0 || !std::isfinite(State.Shield) || State.Shield < 0) return 0;
    const float Absorbed = std::min(Amount, State.Shield);
    if (Absorbed > 0) { State.Shield -= Absorbed; State.Version = NextRevision(State.Version); }
    return Absorbed;
}
bool RechargeShield(Health& State, float Amount)
{
    if (!ValidHealth(State) || State.Dead || !std::isfinite(Amount) || Amount < 0
        || State.Shield >= State.MaximumShield) return false;
    State.Shield = std::min(State.MaximumShield, State.Shield + Amount);
    State.Version = NextRevision(State.Version); return true;
}
bool ApplyDamage(Health& State, float Amount, DamageDelta& Out)
{
    Out = {};
    if (!ValidHealth(State) || State.Dead || !std::isfinite(Amount) || Amount <= 0) return false;
    const float Previous = State.Value;
    Out.Absorbed = AbsorbShield(State, Amount);
    State.Value = std::max(0.0f, State.Value - (Amount - Out.Absorbed));
    State.Dead = State.Value <= 0;
    State.Version = NextRevision(State.Version);
    Out.Applied = Previous - State.Value; Out.Killed = State.Dead;
    return true;
}
bool InShieldRadius(double X, double Y, double CX, double CY, double Radius)
{
    if (!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(CX) || !std::isfinite(CY)
        || !std::isfinite(Radius) || Radius < 0) return false;
    const double DX = X - CX, DY = Y - CY;
    return DX * DX + DY * DY <= Radius * Radius;
}
bool CanAfford(const Resources& Balance, const Resources& Cost)
{ return Balance.Nonnegative() && Cost.Nonnegative() && Balance.Blue >= Cost.Blue && Balance.Red >= Cost.Red; }
bool Credit(Resources& Balance, const Resources& Amount)
{
    if (!Balance.Nonnegative() || !Amount.Positive()
        || static_cast<std::int64_t>(Balance.Blue) + Amount.Blue > INT32_MAX
        || static_cast<std::int64_t>(Balance.Red) + Amount.Red > INT32_MAX) return false;
    Balance.Blue += Amount.Blue; Balance.Red += Amount.Red; Balance.Version = NextRevision(Balance.Version); return true;
}
bool Debit(Resources& Balance, const Resources& Cost)
{
    if (!Cost.Positive() || !CanAfford(Balance, Cost)) return false;
    Balance.Blue -= Cost.Blue; Balance.Red -= Cost.Red; Balance.Version = NextRevision(Balance.Version); return true;
}
Resources& Economy::MutableBalance(Team Faction) { return Faction == Team::Red ? State.Red : State.Blue; }
bool Economy::Begin(const Resources& Red, const Resources& Blue)
{
    if (State.Active || !Red.Nonnegative() || !Blue.Nonnegative()) return false;
    State = {}; State.Active = true; State.Red = Red; State.Blue = Blue;
    State.Red.Version = State.Blue.Version = 1; return true;
}
bool Economy::Open() { if (!State.Active) return false; State.Open = true; return true; }
void Economy::End() { State = {}; }
Resources Economy::Balance(Team Faction) const
{ return !State.Active || !Playable(Faction) ? Resources{} : Faction == Team::Red ? State.Red : State.Blue; }
bool Economy::Add(Team Faction, const Resources& Amount)
{ return State.Active && Playable(Faction) && Credit(MutableBalance(Faction), Amount); }
bool Economy::Reserve(Team Faction, EconomyKey Key, const Resources& Cost, Id Token, Reservation& Out)
{
    Out = {};
    if (!State.Active || !State.Open || !Playable(Faction) || !Key.Account.Valid() || !Key.Request
        || !Cost.Positive() || State.Finalized.contains(Key)) return false;
    if (auto It = State.Pending.find(Key); It != State.Pending.end())
    {
        const auto& Existing = It->second;
        if (Existing.Faction != Faction || Existing.Cost.Blue != Cost.Blue || Existing.Cost.Red != Cost.Red) return false;
        Out = Existing; return true;
    }
    if (!Token.Valid() || !Debit(MutableBalance(Faction), Cost)) return false;
    Out = {Token, Key, Faction, Cost}; State.Pending.emplace(Key, Out); return true;
}
bool Economy::Finalize(const Reservation& Value, bool Refund)
{
    if (!State.Active || !Value.Token.Valid()) return false;
    auto It = State.Pending.find(Value.Key);
    if (It == State.Pending.end() || It->second.Token != Value.Token || It->second.Faction != Value.Faction
        || It->second.Cost.Blue != Value.Cost.Blue || It->second.Cost.Red != Value.Cost.Red) return false;
    if (Refund && !Credit(MutableBalance(It->second.Faction), It->second.Cost)) return false;
    if (!Refund)
    {
        auto& Account=MutableBalance(It->second.Faction);
        if (Account.Version==UINT64_MAX) return false;
        ++Account.Version;
    }
    State.Finalized.insert(Value.Key); State.Pending.erase(It); return true;
}
bool Economy::Restore(EconomyState Value)
{
    if (!Value.Red.Nonnegative() || !Value.Blue.Nonnegative() || (Value.Open && !Value.Active)) return false;
    for (const auto& [Key, Pending] : Value.Pending)
        if (Key != Pending.Key || !Key.Account.Valid() || !Key.Request || !Pending.Token.Valid()
            || !Playable(Pending.Faction) || !Pending.Cost.Positive() || Value.Finalized.contains(Key)) return false;
    State = std::move(Value); return true;
}
}
