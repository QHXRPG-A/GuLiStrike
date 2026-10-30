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
