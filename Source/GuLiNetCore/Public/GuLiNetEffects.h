#pragma once
#include "GuLiNetCapability.h"

namespace GuLiNet::Effects
{
inline constexpr std::uint32_t Version=1;
struct Definition
{
    std::uint32_t Id=0,VersionValue=1;
    std::string Handler;
    Bytes Parameters;
    friend bool operator==(const Definition&,const Definition&)=default;
};
using Set=std::vector<Definition>;
enum class DamageScope { SingleTarget, Area };
enum class DamageTiming { Once, Periodic };
struct DamageBonuses
{
    float Area=0, Periodic=0;

    float Apply(float base,DamageScope scope,DamageTiming timing) const
    {
        return base*(scope==DamageScope::Area ? 1.f+Area : 1.f)
                   *(timing==DamageTiming::Periodic ? 1.f+Periodic : 1.f);
    }
};
// Read the source's resolved effects once when an attack, cast or status is frozen.
GULINETCORE_API bool ResolveDamageBonuses(const Set&,DamageBonuses&);
// Numeric columns come from the authoring adapter. Gameplay codecs remain independent of UE and Excel.
using ReadWeaponColumn=std::function<bool(const char*,std::vector<double>&)>;
struct WeaponAuthoring
{
    Bytes (*Build)(const ReadWeaponColumn&)=nullptr;
    bool (*Describe)(const Definition&,std::uint32_t,std::vector<double>&)=nullptr;
};
struct GroundHitProperties
{
    bool Instant=false;
    std::uint32_t ArcVfx=0;
    float ShieldMultiplier=1.f;
};
struct GroundWeaponBase
{
    float Damage=0, FireRate=0, InitialDamageRatio=1;
    std::uint32_t BulletVfx=0, InitialBulletVfx=0, ImpactVfx=0, MuzzleVfx=0;
};
struct GroundShotStats
{
    float Damage=0, FireRate=0;
    std::uint32_t BulletVfx=0, ImpactVfx=0, MuzzleVfx=0;
};
inline constexpr const char* BarrageHandler="ground/no_survivors";
struct Handler
{
    std::string Name;
    std::uint32_t VersionValue=1;
    std::function<bool(const Definition&)> Validate;
    std::function<bool(const Definition&,std::uint32_t,Set&)> Accumulate;
    std::function<bool(const Set&,const TransactionRequest&,const CommitResult&,const std::map<ObjectKey,ObjectRecord>&,std::map<ObjectKey,ObjectRecord>&)> AcceptedHit;
    std::function<bool(const Definition&,std::uint32_t&,std::uint32_t&,std::uint32_t&)> Presentation;
    WeaponAuthoring Authoring{};
    bool (*ModifyHit)(const Definition&,GroundHitProperties&)=nullptr;
};
struct Ricochet
{
    std::uint32_t Minimum=0,Maximum=0,PerStack=0;
    float Range=0,Retention=0;
    bool FixedDamagePerTarget=false;
    GULINETCORE_API double DamageScale(std::uint32_t hop) const;
};
struct RicochetRank
{
    std::uint32_t Targets=0;
    float DamageFraction=0;
};
struct RicochetProfile
{
    float Range=0;
    std::vector<RicochetRank> Ranks;
};
struct ExplosivePackage
{
    std::vector<std::uint32_t> Intervals;
    std::uint32_t Level=0,BulletVfx=0,ImpactVfx=0;
    float DamageMultiplier=0,Radius=0;
    float SpeedMultiplier=0,OutwardOffset=0;
    std::uint32_t Interval() const {return Level && Level<=Intervals.size()?Intervals[Level-1]:0;}
};
struct ExplosiveEnhancement
{
    std::vector<float> RadiusBonuses,DamageBonuses;
    std::uint32_t Level=0;
};
struct BarrageRank
{
    std::uint32_t IntervalTicks=0, ShotCount=0;
    float DamageBonus=0, ConeHalfAngleDegrees=0;
};
struct Barrage
{
    std::vector<BarrageRank> Ranks;
    std::uint32_t Level=0;
};
struct AILockRank
{
    float Seconds=0, RangeCm=0;
    float AperturePixels=0, HexPixels=0;
};
struct AIEngine
{
    std::vector<AILockRank> Ranks;
    std::uint32_t Level=0;
};
GULINETCORE_API Bytes EncodeAIEngine(const AIEngine&);
GULINETCORE_API bool DecodeAIEngine(std::span<const std::uint8_t>,AIEngine&);
GULINETCORE_API AILockRank GetAILockRank(const Set&);
inline constexpr std::uint32_t MaximumLockTargets=16;
struct SplitBarrel
{
    std::vector<std::uint32_t> Ranks;
    std::uint32_t Level=0;
};
struct ElectromagneticRank
{
    float SlowFraction=0, Duration=0, ShieldBonus=0;
};
struct Electromagnetic
{
    std::vector<ElectromagneticRank> Ranks;
    std::uint32_t Level=0, ArcVfx=0;
};
GULINETCORE_API Bytes EncodeSplitBarrel(const SplitBarrel&);
GULINETCORE_API bool DecodeSplitBarrel(std::span<const std::uint8_t>,SplitBarrel&);
GULINETCORE_API std::uint32_t LockCapacity(const Set&);
GULINETCORE_API Bytes EncodeElectromagnetic(const Electromagnetic&);
GULINETCORE_API bool DecodeElectromagnetic(std::span<const std::uint8_t>,Electromagnetic&);
GULINETCORE_API ElectromagneticRank GetElectromagnetic(const Set&,std::uint32_t* ArcVfx=nullptr);
inline constexpr const char* SlowCapability="electromagnetic_slow";
GULINETCORE_API float MovementMultiplier(std::span<const std::uint8_t> Status,Tick Now);
struct BulletRank
{
    float DamageMultiplier=1, RateMultiplier=1;
    std::uint32_t Vfx=0;
};
struct EnhancedBullet
{
    std::vector<BulletRank> Ranks;
    std::uint32_t Level=0;
};
GULINETCORE_API Bytes EncodeEnhancedBullet(const EnhancedBullet&);
GULINETCORE_API bool DecodeEnhancedBullet(std::span<const std::uint8_t>,EnhancedBullet&);
GULINETCORE_API BulletRank GetBulletRank(const Set&);
GULINETCORE_API Bytes EncodeBarrage(const Barrage&);
GULINETCORE_API bool DecodeBarrage(std::span<const std::uint8_t>,Barrage&);
GULINETCORE_API const Definition* FindBarrage(const Set&);
GULINETCORE_API Bytes EncodeExplosiveEnhancement(const ExplosiveEnhancement&);
GULINETCORE_API bool DecodeExplosiveEnhancement(std::span<const std::uint8_t>,ExplosiveEnhancement&);
inline constexpr float MaximumGroundFireRate=30.f;
GULINETCORE_API Bytes EncodeFireRate(float Multiplier);
GULINETCORE_API bool DecodeFireRate(std::span<const std::uint8_t>,float&);
GULINETCORE_API float FireRateMultiplier(const Set&);
GULINETCORE_API bool HasEnhancedBullet(const Set&);
GULINETCORE_API Bytes EncodeExplosive(const ExplosivePackage&);
GULINETCORE_API bool DecodeExplosive(std::span<const std::uint8_t>,ExplosivePackage&);
GULINETCORE_API ExplosivePackage GetExplosive(const Set&);
inline constexpr std::uint32_t MaximumRicochetHops=(MaximumCapabilityBytes-8192)/192;
GULINETCORE_API bool Register(Handler);
GULINETCORE_API const Handler* Find(const std::string&);
GULINETCORE_API bool Validate(const Definition&);
GULINETCORE_API bool Validate(const Set&);
GULINETCORE_API Bytes Encode(const Set&);
GULINETCORE_API bool Decode(std::span<const std::uint8_t>,Set&);
GULINETCORE_API Bytes EncodeRicochet(const Ricochet&);
GULINETCORE_API bool DecodeRicochet(std::span<const std::uint8_t>,Ricochet&);
GULINETCORE_API Bytes EncodeRicochetProfile(const RicochetProfile&);
GULINETCORE_API Ricochet GetRicochet(const Set&);
GULINETCORE_API bool AcceptedHit(const Set&,const TransactionRequest&,const CommitResult&,const std::map<ObjectKey,ObjectRecord>&,std::map<ObjectKey,ObjectRecord>&);
GULINETCORE_API bool ResolveHitProperties(const Set&,GroundHitProperties&);
GULINETCORE_API bool ResolveGroundShot(const GroundWeaponBase&,const Set&,GroundShotStats&);
GULINETCORE_API bool FreezePresentation(const Set&,std::uint32_t& Bullet,std::uint32_t& Impact,std::uint32_t& Muzzle);
}
