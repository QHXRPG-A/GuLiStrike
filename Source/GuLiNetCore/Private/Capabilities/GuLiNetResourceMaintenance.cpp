#include "GuLiNetResourceMaintenance.h"
#include "GuLiNetControlPoint.h"
#include "GuLiNetCapability.h"
#include "GuLiNetManifest.h"
#include "GuLiNetHash.h"
#include <algorithm>
#include <cmath>

namespace GuLiNet
{
bool ReadResourceMaintenance(Reader& R,ResourceMaintenanceState& V)
{
    return R.U64(V.Period) && V.Period && V.Period<=TicksPerSecond*86400
        && R.U64(V.Next[0]) && R.U64(V.Next[1]) && V.Next[0]>=V.Period && V.Next[1]>=V.Period;
}
bool GetResourceMaintenance(const ObjectRecord& O,ResourceMaintenanceState& V)
{
    const auto I=O.Capabilities.find(ResourceMaintenanceCapability);
    if (I==O.Capabilities.end() || I->second.Version!=ResourceMaintenanceVersion) return false;
    Reader R(I->second.Data);return ReadResourceMaintenance(R,V) && R.Done();
}
bool SetResourceMaintenance(ObjectRecord& O,const ResourceMaintenanceState& V)
{
    Writer W;W.U64(V.Period);W.U64(V.Next[0]);W.U64(V.Next[1]);
    O.Capabilities[ResourceMaintenanceCapability]={ResourceMaintenanceVersion,W.Take()};
    ResourceMaintenanceState Check;return GetResourceMaintenance(O,Check);
}
namespace
{
bool Initialize(RuntimeManifest& M,std::string& Error)
{
    for (auto& [K,O]:M.Objects) if (M.Strings.contains("resource_maintenance/definition/"+O.Definition))
    {
        const auto I=M.Numbers.find("control_point/upkeep_ticks");
        if (I==M.Numbers.end() || !std::isfinite(I->second) || I->second<1
            || I->second>TicksPerSecond*86400 || std::floor(I->second)!=I->second)
        {Error="Missing resource maintenance period";return false;}
        const Tick Period=static_cast<Tick>(I->second);
        if (!SetResourceMaintenance(O,{Period,{Period,Period}})) return false;
    }
    return true;
}
CommitStatus Prepare(const CapabilityContext& C,CapabilityDelta& Out)
{
    const auto& R=C.Request;ResourceMaintenanceState State;
    if (!GetResourceMaintenance(C.Source,State) || !Playable(R.Faction) || R.Target!=R.Header.Object
        || R.ExpectedRelationVersion!=C.Source.RelationVersion || !R.CapabilityPayload.empty()
        || C.Source.RelationVersion==UINT64_MAX) return CommitStatus::BadRequest;
    Tick& Next=State.Next[R.Faction==Team::Red?0:1];
    if (R.Header.At<Next || R.Header.At>UINT64_MAX-State.Period) return CommitStatus::InvalidRule;
    const Tick Cycle=R.Header.At/State.Period;Next=(Cycle+1)*State.Period;
    auto Root=C.Source;SetResourceMaintenance(Root,State);++Root.RelationVersion;
    Out.Objects.emplace(Root.Grant.Object,std::move(Root));Out.Accounts=C.Accounts;
    std::vector<std::pair<std::uint32_t,ObjectKey>> Order;
    for (const auto& [K,O]:C.Objects)
    {ControlPointState P;if (GetControlPoint(O,P) && P.Owner==R.Faction) Order.emplace_back(P.UpkeepRank,K);}
    std::sort(Order.begin(),Order.end());
    for (const auto& [_,K]:Order)
    {
        const auto& Before=C.Objects.at(K);ControlPointState Point;GetControlPoint(Before,Point);
        Writer Identity;Identity.Identity(R.Header.Event);Identity.Key(K);Id Token;
        Id::Parse(Sha256(Identity.Take()).substr(0,32),Token);Reservation Payment;
        const bool Paid=!Point.UpkeepCost.Positive() || Out.Accounts->Reserve(R.Faction,{DeriveEconomyAccount(K),Cycle},Point.UpkeepCost,Token,Payment);
        if (Payment.Token.Valid() && !Out.Accounts->Finalize(Payment,false)) return CommitStatus::InvalidReservation;
        const auto Supply=[&](const ObjectRecord& O)
        {
            if (O.Supplied==Paid) return true;
            if (O.RelationVersion==UINT64_MAX) return false;
            auto Changed=O;Changed.Supplied=Paid;++Changed.RelationVersion;
            Out.Objects.insert_or_assign(O.Grant.Object,std::move(Changed));return true;
        };
        if (!Supply(Before)) return CommitStatus::InvalidRule;
        for (const auto& [Child,O]:C.Objects) if (O.Parent==K && O.Grant.State<Life::Dead && !Supply(O)) return CommitStatus::InvalidRule;
    }
    return CommitStatus::Committed;
}
bool Transition(const ObjectRecord& Before,const ObjectRecord& After)
{
    if (Before.RelationVersion==UINT64_MAX || After.RelationVersion!=Before.RelationVersion+1) return false;
    auto Same=After;Same.RelationVersion=Before.RelationVersion;
    if (Before.Capabilities.contains(ResourceMaintenanceCapability))
    {
        ResourceMaintenanceState A,B;
        if (!GetResourceMaintenance(Before,A) || !GetResourceMaintenance(After,B) || A.Period!=B.Period
            || B.Next[0]<A.Next[0] || B.Next[1]<A.Next[1]) return false;
        Same.Capabilities[ResourceMaintenanceCapability]=Before.Capabilities.at(ResourceMaintenanceCapability);
    }
    else
    {
        ControlPointState P;
        if (!GetControlPoint(Before,P) && Before.Parent.Kind!=ObjectKind::Territory) return false;
        Same.Supplied=Before.Supplied;
    }
    Writer A,B;WriteObject(A,Before);WriteObject(B,Same);return A.Take()==B.Take();
}
const bool Registered=RegisterCapability({ResourceMaintenanceCapability,ResourceMaintenanceVersion,
    [](const ObjectRecord& O,const CapabilityState&){ResourceMaintenanceState S;return O.Grant.Object.Kind==ObjectKind::BusinessWork
        && O.Grant.Faction==Team::None && GetResourceMaintenance(O,S);},
    [](const Rule& R){return R.CapabilityOperation=="upkeep" && R.SourceDefinition==R.TargetDefinition
        && R.MaxTargets==1 && !R.Amount.Positive() && R.Damage==0;},Prepare,Transition,Initialize,
    [](const RuntimeManifest& M,std::string& Error)
    {
        RuntimeManifest Expected=M;if (!Initialize(Expected,Error)) return false;
        for (const auto& [K,O]:M.Objects) if (O.Capabilities!=Expected.Objects.at(K).Capabilities)
        {Error="Resource maintenance startup state differs";return false;}
        return true;
    }});
}
}
