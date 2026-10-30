#pragma once
#include "GuLiNetEffects.h"

namespace GuLiNet::Buffs
{
inline constexpr const char* Name="buff_status";
inline constexpr const char* HostName="buff_host";
inline constexpr const char* DefinitionName="buff/status";
inline constexpr std::uint32_t Version=1,MaximumTargets=2048;
struct Definition
{
    std::uint32_t Id=0,VersionValue=1,Parent=0;
    std::string Handler;
    Tick Duration=0,Interval=0;
    float Ratio=0,RadiusMultiplier=0;
    std::uint32_t Vfx=0,BulletVfx=0;
    friend bool operator==(const Definition&,const Definition&)=default;
};
struct Handler
{
    std::string Name;
    std::uint32_t Priority=0;
    bool Area=false;
    std::function<bool(const Definition&)> Validate;
    std::function<float(const Definition&,const Health&)> Damage;
};
struct Slot{Definition Frozen;Tick Next=0;};
struct Hit{ObjectKey Target;bool Settled=false;};
struct Pulse
{
    Id Event;
    ObjectKey Source;
    std::uint32_t Buff=0;
    Tick At=0;
    float Damage=0,Radius=0;
    double X=0,Y=0,Z=0;
    std::vector<Hit> Targets;
};
struct State
{
    ObjectKey Host,Source;
    Id Instance,Application;
    Tick Started=0,Expires=0;
    std::uint64_t ApplicationOrder=0;
    std::uint32_t Root=0;
    bool Active=true;
    std::vector<Slot> Slots;
    Pulse Pending;
    // Last burst is presentation history only; bootstrapping clients never replay it.
    Id LastBurst;
    double BurstX=0,BurstY=0,BurstZ=0;
    float BurstRadius=0;
    std::uint32_t BurstVfx=0;
};
struct Command
{
    std::string Operation;
    double X=0,Y=0,Z=0;
    float HostRadius=0;
    std::vector<ObjectKey> Targets;
    ObjectKey Skip;
};
GULINETCORE_API bool Register(Handler);
GULINETCORE_API const Handler* Find(const std::string&);
GULINETCORE_API bool Validate(const Definition&);
GULINETCORE_API Bytes EncodeDefinition(const Definition&);
GULINETCORE_API bool DecodeDefinition(std::span<const std::uint8_t>,Definition&);
GULINETCORE_API std::vector<Definition> FromEffects(const Effects::Set&);
GULINETCORE_API Bytes Encode(const State&);
GULINETCORE_API bool Decode(std::span<const std::uint8_t>,State&);
GULINETCORE_API bool Get(const ObjectRecord&,State&);
GULINETCORE_API Bytes EncodeCommand(const Command&);
GULINETCORE_API bool DecodeCommand(std::span<const std::uint8_t>,Command&);
GULINETCORE_API ObjectKey Key(const ObjectKey& Host,std::uint32_t Root,const Id& Instance);
GULINETCORE_API Id PulseEvent(const ObjectKey&,std::uint32_t Buff,Tick);
GULINETCORE_API Id HitEvent(const Id& Pulse,const ObjectKey& Target);
GULINETCORE_API const Slot* Due(const State&);
GULINETCORE_API bool HasPending(const State&);
}
