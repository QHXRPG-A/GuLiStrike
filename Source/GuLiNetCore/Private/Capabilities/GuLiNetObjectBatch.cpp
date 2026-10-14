#include "GuLiNetObjectBatch.h"
#include "GuLiNetManifest.h"
#include "GuLiNetHash.h"
#include <algorithm>
#include <cmath>
namespace GuLiNet::ObjectBatch
{
Bytes Encode(const std::vector<Placement>& Values)
{Writer W(4096);W.U32(Version);W.U32(std::uint32_t(Values.size()));for (const auto& P:Values) {W.F64(P.X);W.F64(P.Y);W.F64(P.Z);W.F32(P.Yaw);}return W.Take();}
ObjectKey Child(ObjectKey Source,Id Event,ObjectKind Kind,std::uint32_t Index)
{Writer W;W.Text("GuLiObjectBatch/v1");W.Key(Source);W.Identity(Event);W.U32(Index);Id Id;Id::Parse(Sha256(W.Take()).substr(0,32),Id);return {Kind,Id,1};}
namespace
{
bool RuleValid(const Rule& R)
{
    return R.Kind==TransactionKind::Capability && R.Capability==Capability && R.CapabilityVersion==Version && R.CapabilityOperation=="create"
        && R.SpawnKind==ObjectKind::Soldier && !R.TargetDefinition.empty() && R.SpawnHealth>0 && R.SpawnShield>=0
        && R.SpawnCompleted && R.MaximumLiveChildren>0 && R.MaximumLiveChildren<=32 && R.Damage==0 && !R.Amount.Positive();
}
bool Initialize(RuntimeManifest& M,std::string&)
{
    std::set<std::string> Sources;
    for (const auto& [_,R]:M.Rules) if (R.Capability==Capability) Sources.insert(R.SourceDefinition);
    for (auto& [_,O]:M.Objects) if (Sources.contains(O.Definition)) O.Capabilities[Capability]={Version,{}};
    for (auto& [_,R]:M.Rules) if (R.SpawnKind!=ObjectKind::None && Sources.contains(R.TargetDefinition)) R.SpawnCapabilities[Capability]={Version,{}};
    return true;
}
CommitStatus Prepare(const CapabilityContext& C,CapabilityDelta& D)
{
    const auto& R=C.Request;const auto& Rule=C.Operation;
    if (R.Target!=R.Header.Object || C.Source.RelationVersion!=R.ExpectedRelationVersion || C.Source.Grant.Faction!=R.Faction) return CommitStatus::BadRequest;
    Reader Reader(R.CapabilityPayload);std::uint32_t V,N;if (!Reader.U32(V) || V!=Version || !Reader.U32(N) || N!=Rule.MaximumLiveChildren) return CommitStatus::BadRequest;
    std::vector<Placement> Places(N);for (auto& P:Places)
        if (!Reader.F64(P.X) || !Reader.F64(P.Y) || !Reader.F64(P.Z) || !Reader.F32(P.Yaw)
            || !std::isfinite(P.X) || !std::isfinite(P.Y) || !std::isfinite(P.Z) || !std::isfinite(P.Yaw)) return CommitStatus::BadRequest;
    if (!Reader.Done()) return CommitStatus::BadRequest;
    std::uint32_t Ordinal=0;std::size_t Living=0;
    for (const auto& [K,O]:C.Objects)
    {if (OrdinalScope(K.Kind)==OrdinalScope(Rule.SpawnKind)) Ordinal=std::max(Ordinal,O.Ordinal);if (O.Grant.State<Life::Dead && O.Grant.Faction==R.Faction && O.Definition==Rule.TargetDefinition) ++Living;}
    if (Ordinal>UINT32_MAX-N || (Rule.MaximumFactionObjects && Living+N>Rule.MaximumFactionObjects)) return CommitStatus::InvalidRule;
    Id Controller;
    for (const auto& [Id,M]:C.Members) if (M.Online && M.Ready && M.Faction==R.Faction && M.Seat!=Role::Observer)
    {if (!Controller.Valid() || M.Seat==Role::Commander) Controller=Id;if (M.Seat==Role::Commander) break;}
    if (!Controller.Valid()) return CommitStatus::Unauthorized;
    for (std::uint32_t I=0;I<N;++I)
    {
        ObjectRecord O;auto& G=O.Grant;G.Object=Child(R.Header.Object,R.Header.Event,Rule.SpawnKind,I);
        if (C.Objects.contains(G.Object)) return CommitStatus::InvalidRule;
        G.Faction=R.Faction;G.Controller=Controller;G.ControlEpoch=1;G.State=Life::Registered;G.SourceLease=R.Header.Event;G.EstimatedCost=Rule.SpawnCost;
        O.Definition=Rule.TargetDefinition;O.Ordinal=++Ordinal;O.BornAt=R.Header.At;O.Parent=R.Header.Object;O.RelationVersion=1;O.Completed=true;
        O.Vitals.Value=O.Vitals.Maximum=Rule.SpawnHealth;O.Vitals.Shield=O.Vitals.MaximumShield=Rule.SpawnShield;O.Vitals.Version=1;
        O.X=Places[I].X;O.Y=Places[I].Y;O.Z=Places[I].Z;O.FacingYaw=Places[I].Yaw;O.Capabilities=Rule.SpawnCapabilities;O.Upgrades=Rule.SpawnUpgrades;
        O.ShieldRadius=Rule.SpawnShieldRadius;
        if (!DeriveSpawnCapabilities(C.Source,C.Objects,R,O)) return CommitStatus::InvalidRule;
        D.Objects.emplace(G.Object,std::move(O));
    }
    return CommitStatus::Committed;
}
bool Create(const ObjectRecord& O,const std::map<ObjectKey,ObjectRecord>& Previous)
{
    const auto P=Previous.find(O.Parent);
    return P!=Previous.end() && P->second.Capabilities.contains(Capability) && O.Grant.Faction==P->second.Grant.Faction
        && O.Grant.Object.Kind==ObjectKind::Soldier && O.Ordinal && O.Vitals.Maximum>0 && O.Completed
        && O.BornAt>=P->second.BornAt && O.RelationVersion==1;
}
const bool Registered=RegisterCapability({Capability,Version,
    [](const ObjectRecord&,const CapabilityState& S){return S.Data.empty();},RuleValid,Prepare,
    [](const ObjectRecord&,const ObjectRecord&){return false;},Initialize,{},{},{},{},Create,true});
}
}
