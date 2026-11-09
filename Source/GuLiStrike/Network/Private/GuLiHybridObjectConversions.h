#pragma once
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/Private/GuLiHybridMessageConversions.h"
#include "GuLiNetClient.h"
#include "GuLiNetCapability.h"
#include <array>
#include <functional>

namespace GuLiNetBridge
{
inline FName ToUEName(const std::string& Name)
{
    // Compare the full UTF-8 spelling, including case. FName pool entries are
    // immutable; neither object ownership nor mutable ledger data is cached.
    struct FEntry {std::string Text;FName Value;bool Valid=false;};
    static thread_local std::array<FEntry,256> Names;
    if (Name.size()>256) return FName(UTF8_TO_TCHAR(Name.c_str()));
    auto& Entry=Names[std::hash<std::string>{}(Name)&(Names.size()-1)];
    if (!Entry.Valid || Entry.Text!=Name)
    {Entry.Text=Name;Entry.Value=FName(UTF8_TO_TCHAR(Name.c_str()));Entry.Valid=true;}
    return Entry.Value;
}
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
inline void ApplyView(const GuLiNet::ObjectRecord& V,FGuLiHybridObjectState& O)
{
    O.Key=ToUE(V.Grant.Object); O.Controller=ToUE(V.Grant.Controller);
    O.SimulationOwner=ToUE(V.Grant.SimOwner); O.ControlEpoch=V.Grant.ControlEpoch; O.OwnerEpoch=V.Grant.OwnerEpoch;
    O.SpawnEvent=ToUE(V.Grant.SourceLease);
    O.OwnershipGroup=ToUE(V.Grant.OwnershipGroup);O.GroupEpoch=V.Grant.GroupEpoch;
    O.BornAt=V.BornAt;O.LogicalAge=V.LogicalAge;O.OwnershipPolicy=static_cast<uint8>(V.Grant.Policy);O.ControllerSeat=static_cast<uint8>(V.Grant.ControllerSeat);O.bRetireOnVacancy=V.Grant.RetireOnVacancy;
    O.bDormant=V.Grant.Dormant; O.StateCustodian=ToUE(V.Grant.StateCustodian);
    O.Team=ToUE(V.Grant.Faction); O.LifeState=static_cast<uint8>(V.Grant.State);
    O.Definition=ToUEName(V.Definition); O.Ordinal=V.Ordinal; O.FacingYawDegrees=V.FacingYaw;
    O.Location=FVector(V.X,V.Y,V.Z); Apply(V.Vitals,O.Health);
    O.bCompleted=V.Completed; O.bSupplied=V.Supplied; O.bPhased=V.Phased;
    O.Parent=ToUE(V.Parent); O.RelationRevision=V.RelationVersion;
    O.Affiliation=ToUE(GuLiNet::ObjectAffiliation(V));
}
inline FGuLiHybridObjectState ToUEView(const GuLiNet::ObjectRecord& V)
{
    FGuLiHybridObjectState O;ApplyView(V,O);return O;
}
inline void ToUE(const GuLiNet::ObjectRecord& V,FGuLiHybridObjectState& O)
{
    ApplyView(V,O);
    // Preserve the canonical insertion order as well as the complete bytes.
    // Unchanged layouts can reuse both map slots and capability byte buffers;
    // versions alone are not treated as proof that the data is unchanged.
    bool SameLayout=O.Capabilities.Num()==int32(V.Capabilities.size());
    if (SameLayout)
    {
        auto It=O.Capabilities.CreateConstIterator();
        for (const auto& Entry:V.Capabilities)
        {
            const FName Name=ToUEName(Entry.first);
            if (It.Key().GetDisplayIndex()!=Name.GetDisplayIndex() || It.Key().GetNumber()!=Name.GetNumber())
            {SameLayout=false;break;}
            ++It;
        }
    }
    if (!SameLayout) O.Capabilities.Reset();
    auto AssignCapability=[](const GuLiNet::CapabilityState& Value,FGuLiHybridCapabilityState& C)
    {
        C.Version=Value.Version;
        C.Data.SetNumUninitialized(static_cast<int32>(Value.Data.size()),EAllowShrinking::No);
        if (!Value.Data.empty()) FMemory::Memcpy(C.Data.GetData(),Value.Data.data(),Value.Data.size());
    };
    if (SameLayout)
    {
        auto It=O.Capabilities.CreateIterator();
        for (const auto& [Name,Value]:V.Capabilities) {AssignCapability(Value,It.Value());++It;}
    }
    else for (const auto& [Name,Value]:V.Capabilities) AssignCapability(Value,O.Capabilities.FindOrAdd(ToUEName(Name)));
    O.FrozenWork.Reset();
    if (!V.FrozenWork.empty()) O.FrozenWork.Append(V.FrozenWork.data(),static_cast<int32>(V.FrozenWork.size()));
    O.Upgrades.Reset();
    for (const auto& [Id,S]:V.Upgrades)
    {
        auto& U=O.Upgrades.Add(FName(UTF8_TO_TCHAR(Id.c_str())));U.CatalogRevision=S.CatalogVersion;U.Revision=S.Version;
        for (const auto& N:S.Chosen) U.Chosen.Add(FName(UTF8_TO_TCHAR(N.c_str())));
    }
}
inline FGuLiHybridObjectState ToUE(const GuLiNet::ObjectRecord& V)
{FGuLiHybridObjectState O;ToUE(V,O);return O;}
inline GuLiNet::FrozenCommand ToCore(const FGuLiHybridCommand& V)
{
    GuLiNet::FrozenCommand O; O.Event=ToCore(V.Event); O.At=V.LogicalTick; O.Operation=TCHAR_TO_UTF8(*V.Operation.ToString());
    O.AcceptedSequence=V.AcceptedSequence;
    if (!V.Arguments.IsEmpty()) O.Arguments.assign(V.Arguments.GetData(),V.Arguments.GetData()+V.Arguments.Num());
    for (const auto& M : V.Members) O.Members.push_back({ToCore(M.Key),M.ControlEpoch,M.OwnerEpoch,M.Slot,M.Destination.X,M.Destination.Y,M.Destination.Z});
    return O;
}
inline FGuLiHybridCommandMember ToUE(const GuLiNet::CommandMember& M)
{ return {ToUE(M.Object),M.ControlEpoch,M.OwnerEpoch,M.Slot,FVector(M.X,M.Y,M.Z)}; }
inline FGuLiHybridCommand ToUE(const GuLiNet::FrozenCommand& V)
{
    FGuLiHybridCommand O; O.Event=ToUE(V.Event); O.LogicalTick=V.At; O.Operation=FName(UTF8_TO_TCHAR(V.Operation.c_str()));
    O.AcceptedSequence=V.AcceptedSequence;
    if (!V.Arguments.empty()) O.Arguments.Append(V.Arguments.data(),static_cast<int32>(V.Arguments.size()));
    for (const auto& M : V.Members) O.Members.Add(ToUE(M)); return O;
}
// Object execution already receives its exact member. Keep the original batch
// in the protocol layer; do not convert its entire selection for every object.
inline FGuLiHybridCommand ToUECommandMember(const GuLiNet::FrozenCommand& V,const GuLiNet::CommandMember& Member)
{
    FGuLiHybridCommand O; O.Event=ToUE(V.Event); O.LogicalTick=V.At; O.Operation=FName(UTF8_TO_TCHAR(V.Operation.c_str()));
    O.AcceptedSequence=V.AcceptedSequence;
    if (!V.Arguments.empty()) O.Arguments.Append(V.Arguments.data(),static_cast<int32>(V.Arguments.size()));
    O.Members.Add(ToUE(Member)); return O;
}
}
