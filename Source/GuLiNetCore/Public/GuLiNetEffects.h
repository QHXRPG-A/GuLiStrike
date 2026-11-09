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
struct Handler
{
    std::string Name;
    std::uint32_t VersionValue=1;
    std::function<bool(const Definition&)> Validate;
    std::function<bool(const Definition&,std::uint32_t,Set&)> Accumulate;
    std::function<bool(const Set&,const TransactionRequest&,const CommitResult&,const std::map<ObjectKey,ObjectRecord>&,std::map<ObjectKey,ObjectRecord>&)> AcceptedHit;
    std::function<bool(const Definition&,std::uint32_t&,std::uint32_t&,std::uint32_t&)> Presentation;
};
struct Ricochet
{
    std::uint32_t Minimum=0,Maximum=0,PerStack=0;
    float Range=0,Retention=0;
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
GULINETCORE_API Ricochet GetRicochet(const Set&);
GULINETCORE_API bool AcceptedHit(const Set&,const TransactionRequest&,const CommitResult&,const std::map<ObjectKey,ObjectRecord>&,std::map<ObjectKey,ObjectRecord>&);
GULINETCORE_API bool FreezePresentation(const Set&,std::uint32_t& Bullet,std::uint32_t& Impact,std::uint32_t& Muzzle);
}
