#pragma once
#include "GuLiNetRoomTypes.h"

namespace GuLiNet
{
enum class TeamDomain : std::uint8_t { Mass, World };
constexpr bool IsMassObject(ObjectKind Kind)
{ return Kind==ObjectKind::Soldier || Kind==ObjectKind::Engineer || Kind==ObjectKind::Miner; }
GULINETCORE_API Id TeamAuthorityGroup(Team Faction,TeamDomain Domain);
GULINETCORE_API bool DecodeTeamAuthorityGroup(Id Group,Team& Faction,TeamDomain& Domain);
GULINETCORE_API ObjectKey MassAuthorityObject(Team Faction);
// Cargo stays keyed by its vehicle for presentation, but the state fragment is
// owned by the faction account, independently of the Mass body.
GULINETCORE_API std::string InventoryCapability(ObjectKey Carrier,bool Processor);
GULINETCORE_API bool ReadInventoryCapability(const std::string& Name,ObjectKey& Carrier,bool& Processor);
GULINETCORE_API Id TerritoryAuthorityGroup(ObjectKey Territory);
GULINETCORE_API Team NeutralAuthorityTeam(Id Identity);
GULINETCORE_API Id ResolveAuthorityGroup(const Authority& Grant,const OwnershipDefinition& Definition,
    const Authority* Parent=nullptr);
GULINETCORE_API Id ResolveTeamSeat(const std::map<Id,Member>& Members,Team Faction,Role Seat,Id Exclude={});
GULINETCORE_API bool CanUseTeamSeat(const std::map<Id,Member>& Members,Id Player,Role Seat);
}
