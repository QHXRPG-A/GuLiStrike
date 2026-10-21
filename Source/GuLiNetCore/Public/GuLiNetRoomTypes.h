#pragma once
#include "GuLiNetProtocol.h"
#include "GuLiNetResourceSnapshot.h"

namespace GuLiNet
{
struct Member
{
    Id Player;
    Team Faction = Team::None;
    Role Seat = Role::Commander;
    Revision SeatVersion = 1;
    std::uint32_t OrdinalSlot = 0;
    Transport Carrier = Transport::LocalUdp;
    std::string ProductUserId;
    std::string Endpoint;
    bool Online = true;
    bool Ready = false;
    double MeasuredCost = 0;
    double ReservedCost = 0;
    double LastSeen = 0;
};
struct Assignment
{
    ObjectKey Object;
    Id ProposedOwner;
    Revision ProposedEpoch = 0;
    double Deadline = 0;
};
struct RoleBinding { Team Faction=Team::None; Role Seat=Role::Observer; Revision Version=1; };
struct OwnershipDefinition
{
    OwnershipPolicy Policy=OwnershipPolicy::Fixed;
    Role ControllerSeat=Role::Commander;
    bool RetireOnVacancy=false;
    std::string Group;
};
struct OwnershipGroupAuthority
{
    Id Owner;
    Revision Version=0;
    Id Backup;
    Revision RequestEpoch=1;
};
struct ObjectRegistration
{
    Authority Grant;
    std::string Definition;
    std::uint32_t Ordinal=0;
    ObjectKey Parent;
};
enum class MembershipOperation : std::uint8_t { Join, Leave };
struct MembershipChange
{
    Revision Version=0;
    MembershipOperation Operation=MembershipOperation::Join;
    Id Player;
    bool Abrupt=false;
    double Deadline=0;
    std::set<Id> Waiting;
};
}
