#pragma once
#include "GuLiNetCapability.h"
namespace GuLiNet::Relocation
{
// A full selection, including poses and grant versions, fits one bounded
// capability request. Fragmentation remains the transport's responsibility.
inline constexpr std::uint32_t Version=1,MaximumMembers=640;
inline constexpr const char* CasterCapability="relocation_cast";
inline constexpr const char* BodyCapability="relocation_body";
enum class Phase:std::uint8_t {Idle,Windup,Locking,Choosing,Landing,Recovery,Releasing,Finished};
enum class Operation:std::uint8_t {Begin,Collect,Destination,Return,Advance,Release,SetLevel,Acknowledge};
struct Pose {double X=0,Y=0,Z=0;float Pitch=0,Yaw=0,Roll=0,ScaleX=1,ScaleY=1,ScaleZ=1;};
struct Profile
{
    std::string Skill;std::uint32_t Level=1;double Radius=0,MaximumHeight=2000,BeamHeight=10000;
    Tick Windup=180,Wait=600,Recovery=30,Cooldown=0;bool Vehicles=false;
};
struct Body
{
    bool Vehicle=false,Airborne=false,FollowParent=false;
    ObjectKey Root;Id Cast;std::uint32_t Step=0,Acknowledged=0;
    Tick ChangedAt=0,AppliedAt=0;
    bool Accepted=true,Locked=false,Phased=false,Displace=false;
    Pose Original,Destination;
};
struct State
{
    std::map<std::string,Profile> Profiles;
    std::map<std::string,Tick> ReadyAt;
    std::map<std::string,std::uint32_t> Levels;
    Id Cast,LastRequest;std::string ProfileId;Phase Stage=Phase::Idle;
    std::uint32_t Step=0;Tick ChangedAt=0,Deadline=0;
    bool CancelPending=false;
    Pose Source,Destination;std::vector<ObjectKey> Members;
};
struct Member {ObjectKey Object;Revision OwnerEpoch=0,ControlEpoch=0;Pose Transform;};
struct Command
{
    Operation Op=Operation::Begin;Id Cast;std::string ProfileId;
    Pose Point;std::uint32_t Step=0;bool Accepted=true;std::vector<Member> Members;
};
GULINETCORE_API Bytes Encode(const State&);
GULINETCORE_API Bytes Encode(const Body&);
GULINETCORE_API Bytes Encode(const Command&);
GULINETCORE_API bool Decode(std::span<const std::uint8_t>,State&);
GULINETCORE_API bool Decode(std::span<const std::uint8_t>,Body&);
GULINETCORE_API bool Decode(std::span<const std::uint8_t>,Command&);
GULINETCORE_API bool Read(const ObjectRecord&,State&);
GULINETCORE_API bool Read(const ObjectRecord&,Body&);
}
