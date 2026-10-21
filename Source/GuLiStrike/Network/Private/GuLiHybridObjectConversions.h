#pragma once
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/Private/GuLiHybridMessageConversions.h"
#include "GuLiNetClient.h"
#include "GuLiNetCapability.h"

namespace GuLiNetBridge
{
inline GuLiNet::ObjectKey ToCore(const FGuLiHybridObjectKey& V)
{
    // Reflected keys default their future life to one. An absent optional key
    // still has the portable protocol's canonical all-zero representation.
    if (V.Kind==EGuLiHybridObjectKind::None && !V.StableId.IsValid()) return {};
    return {static_cast<GuLiNet::ObjectKind>(V.Kind), ToCore(V.StableId), V.LifeGeneration};
}
inline FGuLiHybridObjectKey ToUE(const GuLiNet::ObjectKey& V)
{
    FGuLiHybridObjectKey O; O.Kind=static_cast<EGuLiHybridObjectKind>(V.Kind);
    O.StableId=ToUE(V.StableId); O.LifeGeneration=V.Generation; return O;
}
inline FGuLiHybridObjectState ToUEView(const GuLiNet::ObjectRecord& V)
{
    FGuLiHybridObjectState O; O.Key=ToUE(V.Grant.Object); O.Controller=ToUE(V.Grant.Controller);
    O.SimulationOwner=ToUE(V.Grant.SimOwner); O.ControlEpoch=V.Grant.ControlEpoch; O.OwnerEpoch=V.Grant.OwnerEpoch;
    O.SpawnEvent=ToUE(V.Grant.SourceLease);
    O.OwnershipGroup=ToUE(V.Grant.OwnershipGroup);O.GroupEpoch=V.Grant.GroupEpoch;
    O.BornAt=V.BornAt;O.LogicalAge=V.LogicalAge;O.OwnershipPolicy=static_cast<uint8>(V.Grant.Policy);O.ControllerSeat=static_cast<uint8>(V.Grant.ControllerSeat);O.bRetireOnVacancy=V.Grant.RetireOnVacancy;
    O.bDormant=V.Grant.Dormant; O.StateCustodian=ToUE(V.Grant.StateCustodian);
    O.Team=ToUE(V.Grant.Faction); O.LifeState=static_cast<uint8>(V.Grant.State);
    O.Definition=FName(UTF8_TO_TCHAR(V.Definition.c_str())); O.Ordinal=V.Ordinal; O.FacingYawDegrees=V.FacingYaw;
    O.Location=FVector(V.X,V.Y,V.Z); Apply(V.Vitals,O.Health);
    O.bCompleted=V.Completed; O.bSupplied=V.Supplied; O.bPhased=V.Phased;
    O.Parent=ToUE(V.Parent); O.RelationRevision=V.RelationVersion;
    O.Affiliation=ToUE(GuLiNet::ObjectAffiliation(V));
    return O;
}
inline FGuLiHybridObjectState ToUE(const GuLiNet::ObjectRecord& V)
{
    auto O=ToUEView(V);
    for (const auto& [Name,Value]:V.Capabilities)
    {
        auto& C=O.Capabilities.Add(FName(UTF8_TO_TCHAR(Name.c_str())));C.Version=Value.Version;
        C.Data.Append(Value.Data.data(),static_cast<int32>(Value.Data.size()));
    }
    if (!V.FrozenWork.empty()) O.FrozenWork.Append(V.FrozenWork.data(),static_cast<int32>(V.FrozenWork.size()));
    for (const auto& [Id,S]:V.Upgrades)
    {
        auto& U=O.Upgrades.Add(FName(UTF8_TO_TCHAR(Id.c_str())));U.CatalogRevision=S.CatalogVersion;U.Revision=S.Version;
        for (const auto& N:S.Chosen) U.Chosen.Add(FName(UTF8_TO_TCHAR(N.c_str())));
    }
    return O;
}
inline GuLiNet::FrozenCommand ToCore(const FGuLiHybridCommand& V)
{
    GuLiNet::FrozenCommand O; O.Event=ToCore(V.Event); O.At=V.LogicalTick; O.Operation=TCHAR_TO_UTF8(*V.Operation.ToString());
    if (!V.Arguments.IsEmpty()) O.Arguments.assign(V.Arguments.GetData(),V.Arguments.GetData()+V.Arguments.Num());
    for (const auto& M : V.Members) O.Members.push_back({ToCore(M.Key),M.ControlEpoch,M.OwnerEpoch,M.Slot,M.Destination.X,M.Destination.Y,M.Destination.Z});
    return O;
}
inline FGuLiHybridCommandMember ToUE(const GuLiNet::CommandMember& M)
{ return {ToUE(M.Object),M.ControlEpoch,M.OwnerEpoch,M.Slot,FVector(M.X,M.Y,M.Z)}; }
inline FGuLiHybridCommand ToUE(const GuLiNet::FrozenCommand& V)
{
    FGuLiHybridCommand O; O.Event=ToUE(V.Event); O.LogicalTick=V.At; O.Operation=FName(UTF8_TO_TCHAR(V.Operation.c_str()));
    if (!V.Arguments.empty()) O.Arguments.Append(V.Arguments.data(),static_cast<int32>(V.Arguments.size()));
    for (const auto& M : V.Members) O.Members.Add(ToUE(M)); return O;
}
// Object execution already receives its exact member. Keep the original batch
// in the protocol layer; do not convert its entire selection for every object.
inline FGuLiHybridCommand ToUECommandMember(const GuLiNet::FrozenCommand& V,const GuLiNet::CommandMember& Member)
{
    FGuLiHybridCommand O; O.Event=ToUE(V.Event); O.LogicalTick=V.At; O.Operation=FName(UTF8_TO_TCHAR(V.Operation.c_str()));
    if (!V.Arguments.empty()) O.Arguments.Append(V.Arguments.data(),static_cast<int32>(V.Arguments.size()));
    O.Members.Add(ToUE(Member)); return O;
}
}
