#include "GuLiNetTeamAuthority.h"
#include <charconv>
#include "GuLiNetHash.h"

namespace GuLiNet
{
Id TeamAuthorityGroup(Team F,TeamDomain D)
{ return StableId("GuLiTeamAuthority/v1",std::to_string(unsigned(F))+"/"+std::to_string(unsigned(D))); }
bool DecodeTeamAuthorityGroup(Id Group,Team& F,TeamDomain& D)
{
    for (const auto TeamValue:{Team::Red,Team::Blue}) for (const auto Domain:{TeamDomain::Mass,TeamDomain::World})
        if (Group==TeamAuthorityGroup(TeamValue,Domain)) {F=TeamValue;D=Domain;return true;}
    return false;
}
ObjectKey MassAuthorityObject(Team F)
{ return {ObjectKind::BusinessWork,StableId("GuLiMassAuthority/v1",std::to_string(unsigned(F))),1}; }
Id TerritoryAuthorityGroup(ObjectKey Territory)
{ return StableId("GuLiTerritoryAuthority/v1",Territory.StableId.ToString()); }
Team NeutralAuthorityTeam(Id Identity)
{ return (Identity.Words[0]&1)?Team::Blue:Team::Red; }
Id ResolveAuthorityGroup(const Authority& A,const OwnershipDefinition& D,const Authority* Parent)
{
    if (D.Policy!=OwnershipPolicy::SharedGroup) return {};
    if (Parent && Parent->Object.Kind==ObjectKind::Territory && !IsMassObject(A.Object.Kind))
        return TerritoryAuthorityGroup(Parent->Object);
    if (A.Object.Kind==ObjectKind::Territory) return TerritoryAuthorityGroup(A.Object);
    const Team F=Playable(A.Faction)?A.Faction:NeutralAuthorityTeam(A.Object.StableId);
    return TeamAuthorityGroup(F,D.Group=="team/mass" || IsMassObject(A.Object.Kind)?TeamDomain::Mass:TeamDomain::World);
}
Id ResolveTeamSeat(const std::map<Id,Member>& Members,Team F,Role Seat,Id Exclude)
{
    if (Seat==Role::Observer) return {};
    Id Fallback;
    for (const auto& [P,M]:Members)
    {
        if (P==Exclude || !M.Online || !M.Ready || M.Faction!=F || M.Seat==Role::Observer) continue;
        if (MatchesControllerRole(M.Seat,Seat)) return P;
        Fallback=P;
    }
    return Fallback;
}
bool CanUseTeamSeat(const std::map<Id,Member>& Members,Id Player,Role Seat)
{
    const auto M=Members.find(Player);
    return M!=Members.end() && ResolveTeamSeat(Members,M->second.Faction,Seat)==Player;
}
std::string InventoryCapability(ObjectKey K,bool Processor)
{return std::string(Processor?"processor/":"cargo/")+std::to_string(unsigned(K.Kind))+"/"+K.StableId.ToString()+"/"+std::to_string(K.Generation);}
bool ReadInventoryCapability(const std::string& Name,ObjectKey& K,bool& Processor)
{
    Processor=Name.starts_with("processor/");
    if (!Processor && !Name.starts_with("cargo/")) return false;
    const auto Begin=Name.find('/')+1,Split=Name.find('/',Begin),Last=Name.rfind('/');
    if (Split==std::string::npos || Split==Last) return false;
    unsigned Kind=0,Generation=0;
    const auto A=std::from_chars(Name.data()+Begin,Name.data()+Split,Kind);
    const auto B=std::from_chars(Name.data()+Last+1,Name.data()+Name.size(),Generation);
    if (A.ec!=std::errc{} || A.ptr!=Name.data()+Split || B.ec!=std::errc{} || B.ptr!=Name.data()+Name.size()
        || !Generation || Kind>unsigned(ObjectKind::BusinessWork) || !IsMassObject(ObjectKind(Kind))
        || !Id::Parse(Name.substr(Split+1,Last-Split-1),K.StableId)) return false;
    K.Kind=ObjectKind(Kind);K.Generation=Generation;return K.Valid();
}

}
