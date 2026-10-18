#include "GuLiNetCapability.h"
#include "GuLiNetManifest.h"
#include <cmath>
#include <algorithm>

namespace GuLiNet
{
namespace
{
struct Registry {std::map<std::string,CapabilityContract> Values;bool Valid=true;};
Registry& Contracts() {static Registry Value;return Value;}
bool ValidName(const std::string& V) {return !V.empty() && V.size()<=192 && V.find('\0')==std::string::npos;}
}
bool RegisterCapability(CapabilityContract C)
{
    auto& R=Contracts();
    if (!ValidName(C.Name) || !C.Version || !C.ValidateState || !C.ValidateRule || !C.Prepare || !C.ValidateTransition
        || R.Values.contains(C.Name)) {R.Valid=false;return false;}
    const auto Name=C.Name;R.Values.emplace(Name,std::move(C));return true;
}
const CapabilityContract* FindCapability(const std::string& Name)
{
    const auto& R=Contracts();if (!R.Valid) return nullptr;
    const auto I=R.Values.find(Name);return I==R.Values.end()?nullptr:&I->second;
}
void WriteCapabilityStates(Writer& W,const CapabilityStates& Values)
{
    W.U32(static_cast<std::uint32_t>(Values.size()));
    for (const auto& [Name,V]:Values) {W.Text(Name);W.U32(V.Version);W.Blob(V.Data);}
}
bool ReadCapabilityStates(Reader& R,CapabilityStates& Out)
{
    std::uint32_t Count=0;CapabilityStates Values;std::size_t Total=0;
    if (!R.U32(Count) || Count>MaximumObjectCapabilities) return false;
    for (std::uint32_t I=0;I<Count;++I)
    {
        std::string Name;CapabilityState V;Bytes Data;
        if (!R.Text(Name,192) || !ValidName(Name) || !R.U32(V.Version) || !R.Blob(Data,MaximumCapabilityBytes)) return false;
        V.Data=std::move(Data);
        Total+=V.Data.size();const auto* C=FindCapability(Name);
        if (Total>MaximumCapabilityBytes || !C || C->Version!=V.Version || !Values.emplace(Name,std::move(V)).second) return false;
    }
    Out=std::move(Values);return true;
}
bool ValidateObjectCapabilities(const ObjectRecord& O)
{
    if (!Contracts().Valid || O.Capabilities.size()>MaximumObjectCapabilities) return false;
    std::size_t Total=0;
    for (const auto& [Name,V]:O.Capabilities)
    {
        Total+=V.Data.size();const auto* C=FindCapability(Name);
        if (!C || C->Version!=V.Version || Total>MaximumCapabilityBytes || !C->ValidateState(O,V)) return false;
    }
    return true;
}
bool InitializeManifestCapabilities(RuntimeManifest& M,std::string& Error)
{
    if (!Contracts().Valid) {Error="Duplicate or invalid capability registration";return false;}
    std::map<std::string,CompiledUpgradeCatalog> Catalogs;
    if (!ReadUpgradeCatalogs(M,Catalogs,Error)) return false;
    const auto Initial=[&](const std::string& Definition,UpgradeSelections& States)
    {
        for (const auto& [Domain,C]:Catalogs)
        {
            const std::string Base="initial_upgrade/"+Domain+"/"+Definition+"/";
            const auto I=M.Numbers.find(Base+"count");if (I==M.Numbers.end()) continue;
            if (!std::isfinite(I->second) || I->second<1 || I->second>MaximumUpgradeNodes || std::floor(I->second)!=I->second) return false;
            UpgradeSelection S{C.Data.Version,0,{}};
            for (std::uint32_t N=0;N<static_cast<std::uint32_t>(I->second);++N)
            {
                const auto Node=M.Strings.find(Base+std::to_string(N));
                if (Node==M.Strings.end() || !CanChooseUpgrade(C,S.Chosen,Node->second,Error)) return false;
                S.Chosen.push_back(Node->second);
            }
            S.Version=S.Chosen.size();States[Domain]=std::move(S);
        }
        return true;
    };
    for (auto& [_,O]:M.Objects) if (!Initial(O.Definition,O.Upgrades)) {Error="Invalid initial upgrade history: "+O.Definition;return false;}
    for (auto& [_,R]:M.Rules) if (R.SpawnKind!=ObjectKind::None && !Initial(R.TargetDefinition,R.SpawnUpgrades))
    {Error="Invalid spawned upgrade history: "+R.TargetDefinition;return false;}
    for (const auto& [_,C]:Contracts().Values) if (C.InitializeManifest && !C.InitializeManifest(M,Error)) return false;
    return true;
}
bool ValidateManifestCapabilities(const RuntimeManifest& M,std::string& Error)
{
    if (!Contracts().Valid) {Error="Duplicate or invalid capability registration";return false;}
    for (const auto& [_,O]:M.Objects) if (!ValidateObjectCapabilities(O)) {Error="Missing or incompatible object capability";return false;}
    for (const auto& [Key,Version]:M.Numbers) if (Key.starts_with("capability/") && Key.ends_with("/version"))
    {
        const auto Name=Key.substr(11,Key.size()-11-8);const auto* C=FindCapability(Name);
        if (!C || Version!=C->Version) {Error="Missing or incompatible required capability: "+Name;return false;}
    }
    for (const auto& [_,C]:Contracts().Values) if (C.ValidateManifest && !C.ValidateManifest(M,Error)) return false;
    return true;
}
bool ValidateCapabilityCommit(const CommitResult& R,const std::map<ObjectKey,ObjectRecord>& Previous,const std::map<std::string,std::set<ObjectKey>>* MutationMembers)
{
    const auto* C=FindCapability(R.Capability);
    if (!C || R.Kind!=TransactionKind::Capability || R.Objects.empty() || R.Objects.size()>MaximumCapabilityObjects) return false;
    std::set<ObjectKey> Keys;
    std::set<std::pair<ObjectKind,std::uint32_t>> Ordinals;
    // Existing-object mutations cannot introduce an ordinal. Building the
    // entire historical set for every territory sample scaled with dead flights.
    const bool CreatesOrdinal=std::any_of(R.Objects.begin(),R.Objects.end(),[&](const auto& O)
        {return O.Ordinal&&!Previous.contains(O.Grant.Object);});
    if (CreatesOrdinal) for (const auto& [K,O]:Previous) if (O.Ordinal) Ordinals.emplace(OrdinalScope(K.Kind),O.Ordinal);
    for (const auto& Next:R.Objects)
    {
        const auto I=Previous.find(Next.Grant.Object);
        if (!Next.Grant.Object.Valid() || !Keys.insert(Next.Grant.Object).second || !ValidateObjectCapabilities(Next)
            || Next.Definition.empty() || Next.Definition.size()>256 || Next.Definition.find('\0')!=std::string::npos
            || !std::isfinite(Next.X) || !std::isfinite(Next.Y) || !std::isfinite(Next.Z) || !std::isfinite(Next.FacingYaw)
            || !std::isfinite(Next.ShieldRadius) || Next.ShieldRadius<0 || !ValidUpgradeSelections(Next.Upgrades)
            || !std::isfinite(Next.Grant.EstimatedCost) || Next.Grant.EstimatedCost<0 || Next.BornAt>Next.DiedAt) return false;
        if (I==Previous.end())
        {
            const auto& G=Next.Grant;
            if (!C->ValidateCreation || !C->ValidateCreation(Next,Previous) || !G.Object.Valid() || G.State!=Life::Registered
                || G.SimOwner.Valid() || G.OwnerEpoch || !G.Controller.Valid() || !G.ControlEpoch || !G.SourceLease.Valid()
                || !Playable(G.Faction) || !ValidHealth(Next.Vitals) || Next.Vitals.Dead || !Next.Parent.Valid()
                || !Previous.contains(Next.Parent) || Next.DiedAt!=UINT64_MAX || !Next.RelationVersion || !Next.FrozenWork.empty()
                || (Next.Ordinal && !Ordinals.emplace(OrdinalScope(G.Object.Kind),Next.Ordinal).second)) return false;
        }
        else if (!C->ValidateTransition(I->second,Next)) return false;
    }
    return ValidateCapabilityMutation(Previous,R.Objects,MutationMembers);
}
bool ValidateCapabilityMutation(const std::map<ObjectKey,ObjectRecord>& Previous,std::span<const ObjectRecord> Changes,const std::map<std::string,std::set<ObjectKey>>* MutationMembers)
{
    if (!Contracts().Valid) return false;
    static const std::set<ObjectKey> Empty;
    for (const auto& [Name,C]:Contracts().Values) if (C.ValidateMutation)
    {
        const std::set<ObjectKey>* Members=nullptr;
        if (MutationMembers)
        {const auto I=MutationMembers->find(Name);Members=I==MutationMembers->end()?&Empty:&I->second;}
        if (!C.ValidateMutation(Previous,Changes,Members)) return false;
    }
    return true;
}
Team ObjectAffiliation(const ObjectRecord& O)
{
    Team Result=O.Grant.Faction;bool Set=false;
    for (const auto& [Name,_]:O.Capabilities) if (const auto* C=FindCapability(Name);C && C->Affiliation)
    {
        const auto Value=C->Affiliation(O);if (Set && Result!=Value) return Team::None;
        Result=Value;Set=true;
    }
    return Result;
}
bool ProjectCapabilityChanges(const std::map<ObjectKey,ObjectRecord>& Previous,std::map<ObjectKey,ObjectRecord>& Changes)
{
    if (!Contracts().Valid) return false;
    for (const auto& [Name,C]:Contracts().Values) if (C.ProjectChanges && !C.ProjectChanges(Previous,Changes)) return false;
    return true;
}
bool InitializeOwnedCapabilityState(std::map<ObjectKey,ObjectRecord>& Objects)
{
    if (!Contracts().Valid) return false;
    for (const auto& [Name,C]:Contracts().Values) if (C.InitializeOwnedState && !C.InitializeOwnedState(Objects)) return false;
    return true;
}
bool ResolveCapabilityRule(const ObjectRecord& O,const std::map<ObjectKey,ObjectRecord>& Objects,const std::string& Id,Rule& R)
{
    const auto Kind=R.Kind;const auto Source=R.SourceDefinition;const auto Target=R.TargetDefinition;
    for (const auto& [Name,_]:O.Capabilities)
    {
        const auto* C=FindCapability(Name);if (!C || (C->ResolveRule && !C->ResolveRule(O,Objects,Id,R))) return false;
    }
    return Kind==R.Kind && Source==R.SourceDefinition && Target==R.TargetDefinition && ValidRule(R);
}
bool DeriveSpawnCapabilities(const ObjectRecord& Source,const std::map<ObjectKey,ObjectRecord>& Objects,const TransactionRequest& R,ObjectRecord& Child)
{
    for (const auto& [Name,_]:Source.Capabilities)
    {
        const auto* C=FindCapability(Name);if (!C || (C->DeriveSpawn && !C->DeriveSpawn(Source,Objects,R,Child))) return false;
    }
    // Capability payloads have already been decoded by their operation. Only
    // ordinary Spawn payloads must be claimed as a child's frozen recipe.
    bool PayloadClaimed=R.Kind!=TransactionKind::Spawn || R.CapabilityPayload.empty();
    for (const auto& [Name,C]:Contracts().Values) if (C.FreezeSpawn && !C.FreezeSpawn(Source,Objects,R,Child,PayloadClaimed)) return false;
    return PayloadClaimed && ValidateObjectCapabilities(Child);
}
}
