#pragma once
#include <memory>
#include "GuLiNetTypes.h"

namespace GuLiNet
{
struct Health
{
    float Value = 0;
    float Maximum = 0;
    float Shield = 0;
    float MaximumShield = 0;
    std::uint32_t Version = 0;
    bool Dead = false;
    friend bool operator==(const Health&,const Health&)=default;
};
struct DamageDelta
{
    float Applied = 0;
    float Absorbed = 0;
    bool Killed = false;
};
GULINETCORE_API bool ValidHealth(const Health& State);
GULINETCORE_API bool ApplyDamage(Health& State, float Amount, DamageDelta& Out);
GULINETCORE_API float AbsorbShield(Health& State, float Amount);
GULINETCORE_API bool RechargeShield(Health& State, float Amount);
GULINETCORE_API bool InShieldRadius(double TargetX, double TargetY, double CenterX, double CenterY, double Radius);

struct Resources
{
    std::int32_t Blue = 0;
    std::int32_t Red = 0;
    std::uint32_t Version = 0;
    bool Nonnegative() const { return Blue >= 0 && Red >= 0; }
    bool Positive() const { return Nonnegative() && (Blue > 0 || Red > 0); }
    friend bool operator==(const Resources&, const Resources&) = default;
};
GULINETCORE_API bool CanAfford(const Resources& Balance, const Resources& Cost);
GULINETCORE_API bool Credit(Resources& Balance, const Resources& Amount);
GULINETCORE_API bool Debit(Resources& Balance, const Resources& Cost);

struct EconomyKey
{
    Id Account;
    std::uint64_t Request = 0;
    friend bool operator==(const EconomyKey&, const EconomyKey&) = default;
    friend bool operator<(const EconomyKey& A, const EconomyKey& B)
    { return std::tie(A.Account, A.Request) < std::tie(B.Account, B.Request); }
};
struct Reservation
{
    Id Token;
    EconomyKey Key;
    Team Faction = Team::None;
    Resources Cost;
};
struct EconomyState
{
    Resources Red;
    Resources Blue;
    bool Active = false;
    bool Open = false;
    std::map<EconomyKey, Reservation> Pending;
    std::set<EconomyKey> Finalized;
};
class GULINETCORE_API Economy
{
public:
    bool Begin(const Resources& Red, const Resources& Blue);
    bool Open();
    void End();
    Resources Balance(Team Faction) const;
    bool Add(Team Faction, const Resources& Amount);
    bool Reserve(Team Faction, EconomyKey Key, const Resources& Cost, Id Token, Reservation& Out);
    bool Finalize(const Reservation& Value, bool Refund);
    const EconomyState& Snapshot() const { return *Storage; }
    bool Restore(EconomyState Value);
private:
    Resources& MutableBalance(Team Faction);
    std::shared_ptr<EconomyState> Storage=std::make_shared<EconomyState>();
    EconomyState& Write() {if (Storage.use_count()!=1) Storage=std::make_shared<EconomyState>(*Storage);return *Storage;}
};
}
