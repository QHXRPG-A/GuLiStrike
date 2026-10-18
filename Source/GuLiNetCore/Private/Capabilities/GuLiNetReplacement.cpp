#include "GuLiNetReplacement.h"
#include "GuLiNetManifest.h"
#include "GuLiNetHash.h"
#include <algorithm>
namespace GuLiNet::Replacement
{
ObjectKey Child(ObjectKey Source,Id Event,ObjectKind Kind)
{Writer W;W.Text("GuLiReplacement/v1");W.Key(Source);W.Identity(Event);Id ID;Id::Parse(Sha256(W.Take()).substr(0,32),ID);return {Kind,ID,1};}
bool Result(const ObjectRecord& O,ObjectKey& Key)
{
    const auto I=O.Capabilities.find(Capability);if (I==O.Capabilities.end() || I->second.Version!=Version) return false;
    if (I->second.Data.empty()) {Key={};return true;}
    Reader R(I->second.Data);return R.Key(Key) && Key.Valid() && R.Done();
}
namespace
{
bool Initialize(RuntimeManifest& M,std::string&)
{
    std::set<std::string> Definitions;for (const auto& [_,R]:M.Rules) if (R.Capability==Capability) Definitions.insert(R.SourceDefinition);
    for (auto& [_,O]:M.Objects) if (Definitions.contains(O.Definition)) O.Capabilities[Capability]={Version,{}};
    for (auto& [_,R]:M.Rules) if (R.SpawnKind!=ObjectKind::None && Definitions.contains(R.TargetDefinition)) R.SpawnCapabilities[Capability]={Version,{}};
    M.Numbers["capability/replacement/version"]=Version;return true;
}
CommitStatus Prepare(const CapabilityContext& C,CapabilityDelta& D)
{
    const auto& S=C.Source;const auto& R=C.Request;const auto& Rule=C.Operation;ObjectKey Replaced;
    if (!Result(S,Replaced) || Replaced.Valid() || R.Target!=S.Grant.Object || R.ExpectedRelationVersion!=S.RelationVersion
        || S.RelationVersion==UINT64_MAX || S.Grant.State!=Life::Alive || !S.Completed || !ValidHealth(S.Vitals) || S.Vitals.Dead
        || S.Vitals.Version==UINT32_MAX || R.Faction!=S.Grant.Faction || !R.CapabilityPayload.empty()
        || R.ReservationToken.Valid() || R.ReservationKey.Request || R.Related.Valid()) return CommitStatus::InvalidRule;
    ObjectRecord O;O.Grant.Object=Child(R.Target,R.Header.Event,Rule.SpawnKind);
    if (C.Objects.contains(O.Grant.Object)) return CommitStatus::InvalidRule;
    std::uint32_t Ordinal=0;for (const auto& [K,V]:C.Objects) if (OrdinalScope(K.Kind)==OrdinalScope(Rule.SpawnKind)) Ordinal=std::max(Ordinal,V.Ordinal);
    if (Ordinal==UINT32_MAX) return CommitStatus::InvalidRule;
    Id Controller;
    for (const auto& [ID,M]:C.Members) if (M.Online && M.Ready && M.Faction==R.Faction && M.Seat==Rule.SpawnControllerSeat)
    {Controller=ID;break;}
    O.Grant.Faction=R.Faction;O.Grant.Controller=Controller;O.Grant.ControlEpoch=1;O.Grant.State=Life::Registered;
    O.Grant.SourceLease=R.Header.Event;O.Grant.EstimatedCost=Rule.SpawnCost;O.Definition=Rule.TargetDefinition;O.Ordinal=Ordinal+1;
    O.Parent=S.Grant.Object;O.BornAt=R.Header.At;O.RelationVersion=1;O.Completed=Rule.SpawnCompleted;
    O.X=S.X;O.Y=S.Y;O.Z=S.Z;O.FacingYaw=S.FacingYaw;O.ShieldRadius=Rule.SpawnShieldRadius;
    O.Vitals.Maximum=Rule.SpawnHealth;O.Vitals.Value=std::min(S.Vitals.Value,Rule.SpawnHealth);
    O.Vitals.MaximumShield=Rule.SpawnShield;O.Vitals.Shield=std::min(S.Vitals.Shield,Rule.SpawnShield);O.Vitals.Version=1;
    O.Capabilities=Rule.SpawnCapabilities;O.Upgrades=Rule.SpawnUpgrades;
    if (!DeriveSpawnCapabilities(S,C.Objects,R,O)) return CommitStatus::InvalidRule;
    auto Retired=S;Retired.Grant.State=Life::Destroyed;Retired.DiedAt=R.Header.At;Retired.Vitals.Value=0;Retired.Vitals.Dead=true;
    ++Retired.Vitals.Version;++Retired.RelationVersion;Retired.Supplied=false;
    Writer W;W.Key(O.Grant.Object);Retired.Capabilities[Capability]={Version,W.Take()};
    D.Objects.emplace(O.Grant.Object,std::move(O));D.Objects.emplace(R.Target,std::move(Retired));return CommitStatus::Committed;
}
bool Transition(const ObjectRecord& Before,const ObjectRecord& After)
{
    ObjectKey A,B;if (!Result(Before,A) || A.Valid() || !Result(After,B) || !B.Valid() || Before.Grant.State!=Life::Alive
        || After.Grant.State!=Life::Destroyed || After.DiedAt<Before.BornAt || !After.Vitals.Dead || After.Vitals.Value!=0
        || Before.Vitals.Version==UINT32_MAX || After.Vitals.Version!=Before.Vitals.Version+1
        || Before.RelationVersion==UINT64_MAX || After.RelationVersion!=Before.RelationVersion+1 || After.Supplied) return false;
    auto Copy=After;Copy.Grant.State=Before.Grant.State;Copy.DiedAt=Before.DiedAt;Copy.Vitals=Before.Vitals;
    Copy.RelationVersion=Before.RelationVersion;Copy.Supplied=Before.Supplied;Copy.Capabilities[Capability]=Before.Capabilities.at(Capability);
    Writer X,Y;WriteObject(X,Before);WriteObject(Y,Copy);return X.Take()==Y.Take();
}
bool Mutation(const std::map<ObjectKey,ObjectRecord>& Before,std::span<const ObjectRecord> Changes,const std::set<ObjectKey>* Members)
{
    std::map<ObjectKey,const ObjectRecord*> Overlay;
    for (const auto& O:Changes) if (!Overlay.emplace(O.Grant.Object,&O).second) return false;
    const auto Find=[&](ObjectKey K)->const ObjectRecord*
    {if (const auto I=Overlay.find(K);I!=Overlay.end()) return I->second;const auto I=Before.find(K);return I==Before.end()?nullptr:&I->second;};
    const auto Check=[&](const ObjectRecord& O)
    {
        if (!O.Capabilities.contains(Capability)) return true;ObjectKey K;if (!Result(O,K)) return false;
        if (!K.Valid()) return true;
        const auto* Target=Find(K);
        if (!Target || K!=Child(O.Grant.Object,Target->Grant.SourceLease,K.Kind)
            || O.Grant.State!=Life::Destroyed) return false;
        // A newly retired source and its new lifetime must be one delta. A
        // replay or restored directory may already contain the complete pair.
        const auto Old=Before.find(O.Grant.Object);ObjectKey Previous;
        if (Old!=Before.end() && Result(Old->second,Previous) && !Previous.Valid())
            return Overlay.contains(K) && !Before.contains(K) && Target->Parent==O.Grant.Object && Target->Grant.Faction==O.Grant.Faction;
        return true;
    };
    if (Members)
    {
        for (const auto K:*Members) if (!Overlay.contains(K))
        {const auto I=Before.find(K);if (I==Before.end() || !Check(I->second)) return false;}
    }
    else for (const auto& [K,O]:Before) if (!Overlay.contains(K) && !Check(O)) return false;
    for (const auto& [_,O]:Overlay) if (!Check(*O)) return false;
    return true;
}
const bool Registered=RegisterCapability({Capability,Version,
    [](const ObjectRecord& O,const CapabilityState&){ObjectKey K;return Result(O,K) && (!K.Valid() || O.Grant.State==Life::Destroyed);},
    [](const Rule& R){return R.Kind==TransactionKind::Capability && R.Capability==Capability && R.CapabilityVersion==Version
        && R.CapabilityOperation=="replace" && R.SpawnKind!=ObjectKind::None
        && !R.TargetDefinition.empty() && R.SpawnHealth>0 && R.SpawnCompleted && !R.Amount.Positive();},
    Prepare,Transition,Initialize,{},{},{},{},
    [](const ObjectRecord& O,const std::map<ObjectKey,ObjectRecord>& Previous)
    {const auto P=Previous.find(O.Parent);ObjectKey K;return P!=Previous.end() && Result(P->second,K) && !K.Valid()
        && O.Grant.Object==Child(O.Parent,O.Grant.SourceLease,O.Grant.Object.Kind) && O.Grant.Faction==P->second.Grant.Faction
        && O.Completed && O.Ordinal && O.RelationVersion==1 && O.Vitals.Maximum>0;},false,Mutation});
}
}
