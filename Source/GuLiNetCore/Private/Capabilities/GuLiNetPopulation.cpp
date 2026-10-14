#include "GuLiNetPopulation.h"
#include "GuLiNetManifest.h"
#include "GuLiNetOwnerState.h"
#include <cmath>
namespace GuLiNet::Population
{
bool Read(const ObjectRecord& O,Claim& C)
{
    const auto I=O.Capabilities.find(Capability);if (I==O.Capabilities.end() || I->second.Version!=Version) return false;
    Reader R(I->second.Data);return R.Text(C.Group,128) && !C.Group.empty() && R.U32(C.Units) && C.Units && R.U32(C.Limit)
        && C.Limit && C.Limit<=100000 && C.Units<=C.Limit && R.Done();
}
std::uint64_t Used(const std::map<ObjectKey,ObjectRecord>& Objects,Team Faction,const std::string& Group)
{
    std::uint64_t Total=0;for (const auto& [_,O]:Objects) {Claim C;if (O.Grant.State<Life::Dead && O.Grant.Faction==Faction && Read(O,C) && C.Group==Group) Total+=C.Units;}return Total;
}
namespace
{
bool ValidateInitial(const std::map<ObjectKey,ObjectRecord>& Previous,std::span<const ObjectRecord> Changes)
{
    std::map<ObjectKey,const ObjectRecord*> Overlay;
    for (const auto& O:Changes) if (!Overlay.emplace(O.Grant.Object,&O).second) return false;
    std::map<std::pair<Team,std::string>,std::pair<std::uint64_t,std::uint32_t>> Totals;
    const auto Add=[&](const ObjectRecord& O)
    {
        if (!O.Capabilities.contains(Capability)) return true;Claim C;
        if (!Read(O,C) || !Playable(O.Grant.Faction)) return false;
        auto& T=Totals[{O.Grant.Faction,C.Group}];if (T.second && T.second!=C.Limit) return false;T.second=C.Limit;
        if (O.Grant.State<Life::Dead) T.first+=C.Units;return T.first<=T.second;
    };
    for (const auto& [K,O]:Previous) if (!Overlay.contains(K) && !Add(O)) return false;
    for (const auto& [_,O]:Overlay) if (!Add(*O)) return false;
    return true;
}
constexpr const char* AccountCapability="population-account";
using Counts=std::map<std::string,std::pair<std::uint32_t,std::uint32_t>>;
bool ReadCounts(const ObjectRecord& O,Counts& Out)
{
    const auto I=O.Capabilities.find(AccountCapability);if (I==O.Capabilities.end()) return false;
    Reader R(I->second.Data);std::uint32_t N;
    if (I->second.Version!=1 || !R.U32(N) || N>128) return false;
    Counts V;
    for (std::uint32_t J=0;J<N;++J)
    {
        std::string Name;std::uint32_t Used,Limit;
        if (!R.Text(Name,128) || Name.empty() || !R.U32(Used) || !R.U32(Limit) || !Limit || Limit>100000 || Used>Limit
            || !V.emplace(Name,std::pair{Used,Limit}).second) return false;
    }
    if (!R.Done()) return false;Out=std::move(V);return true;
}
void WriteCounts(ObjectRecord& O,const Counts& C)
{
    Writer W;W.U32(static_cast<std::uint32_t>(C.size()));
    for (const auto& [Group,N]:C) {W.Text(Group);W.U32(N.first);W.U32(N.second);}
    O.Capabilities[AccountCapability]={1,W.Take()};
}
bool ValidateClaims(const std::map<ObjectKey,ObjectRecord>&,std::span<const ObjectRecord> Changes)
{
    for (const auto& O:Changes) if (O.Capabilities.contains(Capability))
    {Claim C;if (!Read(O,C) || !Playable(O.Grant.Faction)) return false;}
    return true;
}
bool InitializeAccounts(std::map<ObjectKey,ObjectRecord>& Objects)
{
    for (const auto F:{Team::Red,Team::Blue})
    {
        const auto Account=Objects.find(TeamAccountObject(F));if (Account==Objects.end()) return false;
        Counts Values;
        for (const auto& [K,O]:Objects)
        {
            Claim C;if (O.Grant.Faction!=F || !Read(O,C)) continue;
            auto& V=Values[C.Group];if (V.second && V.second!=C.Limit) return false;V.second=C.Limit;
            if (O.Grant.State<Life::Dead) V.first+=C.Units;
            if (V.first>V.second) return false;
        }
        WriteCounts(Account->second,Values);
    }
    return true;
}
bool Project(const std::map<ObjectKey,ObjectRecord>& Previous,std::map<ObjectKey,ObjectRecord>& Changes)
{
    std::map<std::pair<Team,std::string>,std::pair<std::int64_t,std::uint32_t>> Deltas;
    const auto Accumulate=[&](const ObjectRecord& O,int Sign)
    {
        if (!O.Capabilities.contains(Capability)) return true;Claim C;if (!Read(O,C)) return false;
        auto& D=Deltas[{O.Grant.Faction,C.Group}];if (D.second && D.second!=C.Limit) return false;D.second=C.Limit;
        if (O.Grant.State<Life::Dead) D.first+=static_cast<std::int64_t>(Sign)*C.Units;
        return true;
    };
    for (const auto& [K,O]:Changes)
    {
        const auto Old=Previous.find(K);if (Old!=Previous.end() && !Accumulate(Old->second,-1)) return false;
        if (!Accumulate(O,1)) return false;
    }
    for (const auto F:{Team::Red,Team::Blue})
    {
        const auto Key=TeamAccountObject(F);const auto Account=Previous.find(Key);if (Account==Previous.end()) return false;
        ObjectRecord Changed=Changes.contains(Key)?Changes.at(Key):Account->second;Counts C;if (!ReadCounts(Changed,C)) return false;
        bool Updated=false;
        for (const auto& [Group,D]:Deltas) if (Group.first==F && D.first)
        {
            auto& Value=C[Group.second];if (Value.second && Value.second!=D.second) return false;
            const auto New=static_cast<std::int64_t>(Value.first)+D.first;
            if (New<0 || New>D.second) return false;
            Value={static_cast<std::uint32_t>(New),D.second};Updated=true;
        }
        if (Updated) {WriteCounts(Changed,C);Changes.insert_or_assign(Key,std::move(Changed));}
    }
    return true;
}
const bool AccountRegistered=RegisterCapability({AccountCapability,1,
    [](const ObjectRecord& O,const CapabilityState&){Counts C;return O.Grant.Policy==OwnershipPolicy::SharedGroup && ReadCounts(O,C);},
    [](const Rule&){return false;},[](const CapabilityContext&,CapabilityDelta&){return CommitStatus::InvalidRule;},
    [](const ObjectRecord&,const ObjectRecord&){return false;}});
bool Initialize(RuntimeManifest& M,std::string& Error)
{
    const auto Bind=[&](const std::string& D,CapabilityStates& S)
    {
        const auto I=M.Strings.find("population/definition/"+D);if (I==M.Strings.end()) return true;
        const auto Limit=M.Numbers.find("population/group/"+I->second+"/limit");
        const auto Units=M.Numbers.find("population/definition/"+D+"/units");
        if (I->second.empty() || I->second.size()>128 || Limit==M.Numbers.end() || Units==M.Numbers.end()
            || !std::isfinite(Limit->second) || !std::isfinite(Units->second) || Limit->second<1 || Limit->second>100000
            || Units->second<1 || Units->second>Limit->second || std::floor(Units->second)!=Units->second || std::floor(Limit->second)!=Limit->second) return false;
        Writer W;W.Text(I->second);W.U32(std::uint32_t(Units->second));W.U32(std::uint32_t(Limit->second));S[Capability]={Version,W.Take()};return true;
    };
    for (auto& [_,O]:M.Objects) if (!Bind(O.Definition,O.Capabilities)) {Error="Invalid population definition";return false;}
    for (auto& [_,R]:M.Rules) if (R.SpawnKind!=ObjectKind::None && !Bind(R.TargetDefinition,R.SpawnCapabilities)) {Error="Invalid population spawn claim";return false;}
    M.Numbers["capability/population/version"]=Version;return true;
}
const bool Registered=RegisterCapability({Capability,Version,
    [](const ObjectRecord& O,const CapabilityState&) {Claim C;return Read(O,C);},
    [](const Rule&){return false;},[](const CapabilityContext&,CapabilityDelta&){return CommitStatus::InvalidRule;},
    [](const ObjectRecord&,const ObjectRecord&){return false;},Initialize,
    [](const RuntimeManifest& M,std::string& Error)
    {
        RuntimeManifest Expected=M;if (!Initialize(Expected,Error)) return false;
        for (const auto& [K,O]:M.Objects) if (O.Capabilities!=Expected.Objects.at(K).Capabilities) {Error="Population binding differs from hashed definition";return false;}
        for (const auto& [K,R]:M.Rules) if (R.SpawnCapabilities!=Expected.Rules.at(K).SpawnCapabilities) {Error="Population spawn binding differs from hashed definition";return false;}
        if (!ValidateInitial(M.Objects,{})) {Error="Initial population exceeds its declared group";return false;}return true;
    },{},{},{},{},false,ValidateClaims,Project,InitializeAccounts});
}
}
