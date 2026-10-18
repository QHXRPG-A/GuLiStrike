#include "GuLiNetControlPoint.h"
#include "GuLiNetCapability.h"
#include "GuLiNetManifest.h"
#include "GuLiNetHash.h"
#include <algorithm>
#include <cmath>

namespace GuLiNet
{
bool ValidControlPoint(const ControlPointState& V)
{
    return V.Index<256 && V.UpkeepRank<256 && V.Owner<=Team::Blue && std::isfinite(V.Progress) && V.Progress>=-1 && V.Progress<=1
        && V.Red<=100000 && V.Blue<=100000 && V.SamplePeriod>0 && V.CaptureDuration>=V.SamplePeriod
        && V.CaptureDuration<=TicksPerSecond*86400 && V.UpkeepPeriod>0 && V.UpkeepPeriod<=TicksPerSecond*86400
        && V.UpkeepCost.Nonnegative();
}
void WriteControlPoint(Writer& W,const ControlPointState& V)
{
    W.U32(V.Index);if (!V.Enabled()) return;
    W.U32(V.UpkeepRank);
    W.U8(static_cast<std::uint8_t>(V.Owner));W.F32(V.Progress);W.U32(V.Red);W.U32(V.Blue);
    W.U64(V.SampledAt);W.U64(V.SamplePeriod);W.U64(V.CaptureDuration);W.U64(V.UpkeepPeriod);
    WriteResources(W,V.UpkeepCost);W.Identity(V.FirstCapture);
}
bool ReadControlPoint(Reader& R,ControlPointState& V)
{
    V={};if (!R.U32(V.Index)) return false;if (!V.Enabled()) return true;
    std::uint8_t Owner=0;
    if (!R.U32(V.UpkeepRank) || !R.U8(Owner) || Owner>2 || !R.F32(V.Progress) || !R.U32(V.Red) || !R.U32(V.Blue)
        || !R.U64(V.SampledAt) || !R.U64(V.SamplePeriod) || !R.U64(V.CaptureDuration) || !R.U64(V.UpkeepPeriod)
        || !ReadResources(R,V.UpkeepCost) || !R.Identity(V.FirstCapture)) return false;
    V.Owner=static_cast<Team>(Owner);return ValidControlPoint(V);
}
bool SameControlPointDefinition(const ControlPointState& A,const ControlPointState& B)
{
    return A.Index==B.Index && A.UpkeepRank==B.UpkeepRank && A.SamplePeriod==B.SamplePeriod && A.CaptureDuration==B.CaptureDuration
        && A.UpkeepPeriod==B.UpkeepPeriod && A.UpkeepCost==B.UpkeepCost;
}
bool AdvanceControlPoint(ControlPointState& V,std::uint32_t Red,std::uint32_t Blue,Tick At,Id Event)
{
    if (!ValidControlPoint(V) || Red>100000 || Blue>100000 || !Event.Valid()
        || At<V.SampledAt || At-V.SampledAt<V.SamplePeriod) return false;
    V.Red=Red;V.Blue=Blue;
    // Preserve the existing single-precision capture order, including ties.
    const float Seconds=static_cast<float>(At-V.SampledAt)/static_cast<float>(TicksPerSecond);
    const float Duration=static_cast<float>(V.CaptureDuration)/static_cast<float>(TicksPerSecond);
    if (Red!=Blue) V.Progress=std::clamp(V.Progress+(Blue>Red?1.f:-1.f)*Seconds/Duration,-1.f,1.f);
    V.SampledAt=At;
    const Team Captor=V.Progress==1.f?Team::Blue:V.Progress==-1.f?Team::Red:Team::None;
    if (Playable(Captor) && Captor!=V.Owner) {V.Owner=Captor;if (!V.FirstCapture.Valid()) V.FirstCapture=Event;}
    return true;
}

bool GetControlPoint(const ObjectRecord& O,ControlPointState& Out)
{
    const auto I=O.Capabilities.find(ControlPointCapability);
    if (I==O.Capabilities.end() || I->second.Version!=ControlPointCapabilityVersion) return false;
    Reader R(I->second.Data);return ReadControlPoint(R,Out) && Out.Enabled() && R.Done();
}
bool SetControlPoint(ObjectRecord& O,const ControlPointState& Value)
{
    if (!ValidControlPoint(Value)) return false;
    Writer W;WriteControlPoint(W,Value);O.Capabilities[ControlPointCapability]={ControlPointCapabilityVersion,W.Take()};return true;
}
Bytes EncodeControlPointSample(std::uint32_t Red,std::uint32_t Blue)
{Writer W;W.U32(Red);W.U32(Blue);return W.Take();}
namespace
{
bool InitializeManifest(RuntimeManifest& V,std::string& Error)
{
    auto Bound=[&](const ObjectRecord& O) {const auto I=V.Strings.find("control_point/definition/"+O.Definition);
        return I!=V.Strings.end() && I->second==ControlPointCapability;};
    bool Needed=false;for (const auto& [_,O]:V.Objects) Needed|=Bound(O);
    if (!Needed) return true;
    ResourceLedgerState Catalog;if (!ReadResourceCatalog(V,Catalog,Error) || !Catalog.Configured) return false;
    auto Number=[&](const std::string& Name,std::uint64_t Low,std::uint64_t High,std::uint64_t& Out)
    {
        const auto I=V.Numbers.find(std::string("control_point/")+Name);
        if (I==V.Numbers.end() || !std::isfinite(I->second) || I->second<Low || I->second>High || std::floor(I->second)!=I->second) return false;
        Out=static_cast<std::uint64_t>(I->second);return true;
    };
    ControlPointState Definition;std::uint64_t Blue=0,Red=0;
    if (!Number("sample_ticks",1,TicksPerSecond*86400,Definition.SamplePeriod)
        || !Number("capture_ticks",1,TicksPerSecond*86400,Definition.CaptureDuration)
        || !Number("upkeep_ticks",1,TicksPerSecond*86400,Definition.UpkeepPeriod)
        || !Number("upkeep_blue",0,INT32_MAX,Blue) || !Number("upkeep_red",0,INT32_MAX,Red))
    {Error="Missing control point numerical contract";return false;}
    Definition.UpkeepCost.Blue=static_cast<std::int32_t>(Blue);Definition.UpkeepCost.Red=static_cast<std::int32_t>(Red);
    std::set<std::uint32_t> Indices,Ranks;
    for (auto& [Key,O]:V.Objects) if (Bound(O))
    {
        if (Key.Kind!=ObjectKind::Territory || !O.Ordinal || O.Ordinal>Catalog.Owners.size()
            || !Indices.insert(O.Ordinal-1).second || O.Grant.Faction!=Team::None)
        {Error="Invalid control point startup identity";return false;}
        auto P=Definition;P.Index=O.Ordinal-1;P.Owner=Catalog.Owners[P.Index];
        std::uint64_t Rank=0;
        if (!Number(std::to_string(P.Index)+"/upkeep_rank",0,Catalog.Owners.size()-1,Rank)
            || !Ranks.insert(static_cast<std::uint32_t>(Rank)).second)
        {Error="Missing or duplicate control point upkeep order";return false;}
        P.UpkeepRank=static_cast<std::uint32_t>(Rank);
        P.Progress=P.Owner==Team::Red?-1.f:P.Owner==Team::Blue?1.f:0.f;
        if (Playable(P.Owner)) P.FirstCapture=Key.StableId;
        if (!SetControlPoint(O,P)) {Error="Invalid control point capability definition";return false;}
    }
    if (Indices.size()!=Catalog.Owners.size()) {Error="Control point startup directory is incomplete";return false;}
    return true;
}

bool ValidRuleContract(const Rule& R)
{
    return R.CapabilityOperation=="capture"
        && R.TargetDefinition==R.SourceDefinition && R.MaxTargets==1 && R.Relation==RelationKind::None
        && !R.Amount.Positive() && R.Damage==0 && R.SpawnKind==ObjectKind::None && R.SpawnHealth==0
        && R.SpawnShield==0 && R.SpawnShieldRadius==0;
}
CommitStatus Prepare(const CapabilityContext& C,CapabilityDelta& Out)
{
    const auto& R=C.Request;const auto& Point=C.Source;ControlPointState P;
    if (R.Target!=R.Header.Object || !GetControlPoint(Point,P)
        || R.ExpectedRelationVersion!=Point.RelationVersion || Point.RelationVersion==UINT64_MAX
        || R.ReservationToken.Valid() || R.ReservationKey.Account.Valid() || R.ReservationKey.Request
        || R.Related.Valid() || R.ResourceNode || R.TargetX || R.TargetY || R.TargetZ || R.FacingYaw
        || !C.Resources.Snapshot().Configured || P.Index>=C.Resources.Snapshot().Owners.size()) return CommitStatus::BadRequest;
    Out.Objects.emplace(R.Target,Point);
    if (C.Operation.CapabilityOperation=="capture")
    {
        std::uint32_t Red=0,Blue=0;Reader Payload(R.CapabilityPayload);
        if (R.Faction!=Team::None || !Payload.U32(Red) || !Payload.U32(Blue) || !Payload.Done()) return CommitStatus::BadRequest;
        if (!AdvanceControlPoint(P,Red,Blue,R.Header.At,R.Header.Event)) return CommitStatus::InvalidRule;
        // Sampling unchanged ownership still advances the control-point
        // capability, but owns no resource-catalog mutation or new revision.
        if (C.Resources.Snapshot().Owners[P.Index]!=P.Owner) Out.TerritoryOwners.emplace(P.Index,P.Owner);
        SetControlPoint(Out.Objects.at(R.Target),P);
        const auto UpdateChild=[&](const ObjectKey& Key,const ObjectRecord& O)
        {
            if (O.Parent!=R.Target || O.Grant.State>=Life::Dead
                || (O.Grant.Faction==P.Owner && O.Supplied==Point.Supplied)) return true;
            if (O.RelationVersion==UINT64_MAX) return false;
            auto Next=O;Next.Supplied=Point.Supplied;
            if (O.Grant.Faction!=P.Owner)
            {
                if (O.Grant.ControlEpoch==UINT64_MAX) return false;
                Id Controller;
                for (const auto& [Id,M]:C.Members) if (M.Online && M.Ready && M.Seat!=Role::Observer && M.Faction==P.Owner)
                {if (!Controller.Valid() || M.Seat==Role::Commander) Controller=Id;if (M.Seat==Role::Commander) break;}
                Next.Grant.Faction=P.Owner;Next.Grant.Controller=Controller;++Next.Grant.ControlEpoch;
            }
            // Capture changes who can order a facility, not which process is
            // already simulating it. Existing ownership remains fixed.
            Out.Objects.emplace(Key,std::move(Next));
            return true;
        };
        if (Playable(P.Owner))
        {
            if (C.LiveChildren)
            {
                for (const auto Key:*C.LiveChildren)
                {
                    const auto I=C.Objects.find(Key);
                    if (I==C.Objects.end() || !UpdateChild(Key,I->second)) return CommitStatus::InvalidRule;
                }
            }
            else for (const auto& [Key,O]:C.Objects) if (!UpdateChild(Key,O)) return CommitStatus::InvalidRule;
        }
    }
    for (auto& [_,O]:Out.Objects) ++O.RelationVersion;
    return CommitStatus::Committed;
}
bool ValidateTransition(const ObjectRecord& Before,const ObjectRecord& After)
{
    if (Before.Parent.Kind==ObjectKind::Territory && !Before.Capabilities.contains(ControlPointCapability))
    {
        if (!Playable(After.Grant.Faction) || Before.RelationVersion==UINT64_MAX || After.RelationVersion!=Before.RelationVersion+1
            || Before.Grant.State>=Life::Dead) return false;
        const bool ControlChanged=Before.Grant.Faction!=After.Grant.Faction || Before.Grant.Controller!=After.Grant.Controller;
        if (ControlChanged ? Before.Grant.ControlEpoch==UINT64_MAX || After.Grant.ControlEpoch!=Before.Grant.ControlEpoch+1
            : After.Grant.ControlEpoch!=Before.Grant.ControlEpoch) return false;
        auto Unchanged=After;Unchanged.Grant.Faction=Before.Grant.Faction;Unchanged.Grant.Controller=Before.Grant.Controller;
        Unchanged.Grant.ControlEpoch=Before.Grant.ControlEpoch;Unchanged.RelationVersion=Before.RelationVersion;Unchanged.Supplied=Before.Supplied;
        Writer X,Y;WriteObject(X,Before);WriteObject(Y,Unchanged);return X.Take()==Y.Take();
    }
    ControlPointState A,B;
    if (!GetControlPoint(Before,A) || !GetControlPoint(After,B) || !SameControlPointDefinition(A,B)
        || Before.RelationVersion==UINT64_MAX || After.RelationVersion!=Before.RelationVersion+1
        || B.SampledAt<A.SampledAt
        || (A.FirstCapture.Valid() && A.FirstCapture!=B.FirstCapture)) return false;
    auto Unchanged=After;Unchanged.Capabilities[ControlPointCapability]=Before.Capabilities.at(ControlPointCapability);
    Unchanged.Supplied=Before.Supplied;Unchanged.RelationVersion=Before.RelationVersion;
    Writer X,Y;WriteObject(X,Before);WriteObject(Y,Unchanged);return X.Take()==Y.Take();
}
bool ValidateManifest(const RuntimeManifest& M,std::string& Error)
{
    bool Needed=false;for (const auto& [_,O]:M.Objects)
    {
        const auto Binding=M.Strings.find("control_point/definition/"+O.Definition);
        const bool Bound=Binding!=M.Strings.end() && Binding->second==ControlPointCapability;
        if (O.Capabilities.contains(ControlPointCapability)!=Bound)
        {Error="Control point capability differs from its explicit definition binding";return false;}
        Needed|=Bound;
    }
    if (!Needed) return true;
    const auto Version=M.Numbers.find("capability/control_point/version");
    if (Version==M.Numbers.end() || Version->second!=ControlPointCapabilityVersion) {Error="Missing control point capability version";return false;}
    // Recreate the immutable definition from the hashed data and require exact startup state.
    RuntimeManifest Expected=M;if (!InitializeManifest(Expected,Error)) return false;
    for (const auto& [Key,O]:M.Objects) if (O.Capabilities!=Expected.Objects.at(Key).Capabilities)
    {Error="Control point startup data differs from its capability definition";return false;}
    return true;
}
const bool Registered=RegisterCapability({ControlPointCapability,ControlPointCapabilityVersion,
    [](const ObjectRecord& O,const CapabilityState&) {ControlPointState P;return O.Grant.Object.Kind==ObjectKind::Territory
        && O.Grant.Faction==Team::None && GetControlPoint(O,P);},ValidRuleContract,Prepare,ValidateTransition,
    InitializeManifest,ValidateManifest,[](const ObjectRecord& O) {ControlPointState P;return GetControlPoint(O,P)?P.Owner:Team::None;},
    {},[](const ObjectRecord& Source,const std::map<ObjectKey,ObjectRecord>&,const TransactionRequest&,ObjectRecord& Child)
    {
        ControlPointState P;if (!GetControlPoint(Source,P) || Child.Parent!=Source.Grant.Object) return false;
        Child.Supplied=Source.Supplied;return true;
    }});
}
}
