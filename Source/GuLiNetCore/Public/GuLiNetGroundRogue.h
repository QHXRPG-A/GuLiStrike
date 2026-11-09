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
inline constexpr const char* BlastName="ground_explosion";
inline constexpr const char* CounterName="ground_shots";
// v7 adds the per-level explosive radius/damage modifier to the shared catalogue.
inline constexpr std::uint32_t Version=7;
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
struct ShotProgress {std::uint32_t ExplosiveLevel=0,Count=0;};
struct ShotCounters {std::map<Id,ShotProgress> Players;};
GULINETCORE_API Bytes EncodeCounters(const ShotCounters&);
GULINETCORE_API bool DecodeCounters(std::span<const std::uint8_t>,ShotCounters&);
GULINETCORE_API std::uint32_t ReadProgress(const ShotCounters&,const Id&,const Effects::Set&);
struct Attack
{
    ObjectKey Source,Root;
    Id Player;
    Revision RevisionValue=0;
    Effects::Set Effects;
    float Damage=0;
    float FireRate=0;
    std::uint32_t BulletVfx=0,ImpactVfx=0,MuzzleVfx=0;
    float ExplosiveDamage=0,ExplosiveRadius=0;
    std::uint32_t ExplosiveBulletVfx=0,ExplosiveImpactVfx=0;
};
// Fit the frozen area target keys beside the full ricochet route in the shared
// 64 KiB capability envelope. UE's private receipt payload has a separate limit.
inline constexpr std::uint32_t MaximumBlastTargets=2048;
static_assert(8192+MaximumBlastTargets*21+MaximumHops*45<=MaximumCapabilityBytes);
struct Blast {Id Shot;Tick At=0;double X=0,Y=0,Z=0;std::vector<ObjectKey> Targets;};
GULINETCORE_API Bytes EncodeBlast(const Blast&);
GULINETCORE_API bool DecodeBlast(std::span<const std::uint8_t>,Blast&);
GULINETCORE_API Id BlastEvent(const Id& Shot);
GULINETCORE_API Id BlastHitEvent(const Id& Shot,const ObjectKey& Target);
struct Hop {ObjectKey Target;double X=0,Y=0,Z=0;};
struct Chain {Attack Frozen;ObjectKey First;Id Shot;Tick At=0;double X=0,Y=0,Z=0;std::uint32_t Budget=0;std::vector<Hop> Hops;};
GULINETCORE_API bool Compile(const Catalog&,CompiledUpgradeCatalog&,std::string&);
GULINETCORE_API bool Resolve(const Catalog&,const Build&,Effects::Set&);
GULINETCORE_API bool Eligible(const Catalog&,const Build&,const std::string&,float BaseFireRate=0);
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
