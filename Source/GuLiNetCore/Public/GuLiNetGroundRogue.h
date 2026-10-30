#pragma once
#include "GuLiNetArmy.h"
#include "GuLiNetUpgrade.h"
#include "GuLiNetEffects.h"
#include <memory>

namespace GuLiNet::GroundRogue
{
inline constexpr const char* Name="ground_rogue";
inline constexpr const char* AttackName="ground_attack";
inline constexpr const char* ChainName="ground_ricochet";
inline constexpr std::uint32_t Version=2;
// Both the portable capability and the UE flight recovery record must fit 64 KiB.
// Reserve the fixed recipe and budget the larger UE hop representation, not just its compact wire form.
inline constexpr std::uint32_t MaximumHops=Effects::MaximumRicochetHops;
using Ricochet=Effects::Ricochet;
struct Card
{
    std::string Id;
    std::uint32_t MaximumAcquisitions=0;
    std::vector<std::string> Required,Excluded;
    std::vector<std::uint32_t> EffectIds;
};
struct Catalog
{
    std::map<std::string,Card> Cards;
    std::map<std::uint32_t,Effects::Definition> Effects;
    // Immutable decoded catalogs share a compiled graph; never serialized.
    mutable std::shared_ptr<const CompiledUpgradeCatalog> Compiled;
    mutable std::string CompiledHash;
};
struct Build
{
    Revision RevisionValue=1;
    Id LastRequest;
    std::uint64_t Random=1;
    Army::Offer Current;
    std::map<std::string,std::uint32_t> Acquired;
};
// The state carrier follows the Ground seat; builds inside it always belong to the player ID.
struct State {Catalog Definitions;std::map<Id,Build> Players;};
struct Attack
{
    ObjectKey Source,Root;
    Id Player;
    Revision RevisionValue=0;
    Effects::Set Effects;
    float Damage=0;
    std::uint32_t BulletVfx=0,ImpactVfx=0,MuzzleVfx=0;
};
struct Hop {ObjectKey Target;double X=0,Y=0,Z=0;};
struct Chain {Attack Frozen;ObjectKey First;Id Shot;Tick At=0;double X=0,Y=0,Z=0;std::uint32_t Budget=0;std::vector<Hop> Hops;};
GULINETCORE_API bool Compile(const Catalog&,CompiledUpgradeCatalog&,std::string&);
GULINETCORE_API bool Resolve(const Catalog&,const Build&,Effects::Set&);
GULINETCORE_API bool Eligible(const Catalog&,const Build&,const std::string&);
GULINETCORE_API Bytes EncodeCatalog(const Catalog&);
GULINETCORE_API bool DecodeCatalog(std::span<const std::uint8_t>,Catalog&);
GULINETCORE_API Bytes EncodeState(const State&);
GULINETCORE_API bool DecodeState(std::span<const std::uint8_t>,State&);
GULINETCORE_API bool GetState(const ObjectRecord&,State&);
GULINETCORE_API Bytes EncodeAttack(const Attack&);
GULINETCORE_API bool DecodeAttack(std::span<const std::uint8_t>,Attack&);
GULINETCORE_API Bytes EncodeChain(const Chain&);
GULINETCORE_API bool DecodeChain(std::span<const std::uint8_t>,Chain&);
GULINETCORE_API Id HopEvent(const Id& Shot,std::uint32_t Index,const ObjectKey& Target);
GULINETCORE_API std::uint32_t HopBudget(const Id& Shot,const Ricochet&);
}
