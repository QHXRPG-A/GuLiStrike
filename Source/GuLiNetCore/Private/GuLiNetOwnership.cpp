#include "GuLiNetOwnership.h"
#include "GuLiNetHash.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace GuLiNet
{
OwnershipScheduler::OwnershipScheduler(OwnershipSettings Settings)
{Configure(Settings);}
bool OwnershipScheduler::Configure(OwnershipSettings S)
{
    if (!Pending.empty() || !std::isfinite(S.HandoffTimeoutSeconds) || S.HandoffTimeoutSeconds<=0
        || !S.MaximumLeaveBatch || S.MaximumLeaveBatch>4096) return false;
    Config=S;return true;
}
bool OwnershipScheduler::SetMember(OwnershipMember M)
{
    if (!M.Player.Valid() || !Playable(M.Faction) || M.Seat>Role::Observer) return false;
    const auto Old=Members.find(M.Player);
    if (Old!=Members.end() && (Old->second.Faction!=M.Faction || Old->second.Seat!=M.Seat || !M.Ready))
    {
        std::vector<ObjectKey> Cancelled;
        for (const auto& [K,T]:Pending) if (T.Target==M.Player || T.Source==M.Player) Cancelled.push_back(K);
        for (const auto& K:Cancelled) Cancel(K);
    }
    Members[M.Player]=M;return true;
}
void OwnershipScheduler::RemoveMember(Id Player)
{
    std::vector<ObjectKey> Cancelled;
    for (const auto& [K,T]:Pending) if (T.Source==Player || T.Target==Player) Cancelled.push_back(K);
    for (const auto& K:Cancelled) Cancel(K);
    Members.erase(Player);
}
void OwnershipScheduler::IndexEntry(const OwnershipEntry& O)
{
    if (O.Grant.OwnershipGroup.Valid()) GroupMembers[O.Grant.OwnershipGroup].insert(O.Grant.Object);
    if (O.Policy==OwnershipPolicy::ParentWork && O.Parent.Valid()) ParentWorkChildren[O.Parent].insert(O.Grant.Object);
}
void OwnershipScheduler::UnindexEntry(const OwnershipEntry& O)
{
    auto Remove=[&](auto& Index,const auto& Key)
    {
        if (const auto I=Index.find(Key);I!=Index.end())
        {I->second.erase(O.Grant.Object);if (I->second.empty()) Index.erase(I);}
    };
    if (O.Grant.OwnershipGroup.Valid()) Remove(GroupMembers,O.Grant.OwnershipGroup);
    if (O.Policy==OwnershipPolicy::ParentWork && O.Parent.Valid()) Remove(ParentWorkChildren,O.Parent);
}
bool OwnershipScheduler::SetObject(OwnershipEntry O,double Now)
{
    if (!O.Grant.Object.Valid() || O.Policy>OwnershipPolicy::ParentWork || !std::isfinite(Now) || Now<LastWallTime
        || !std::isfinite(O.Grant.EstimatedCost) || O.Grant.EstimatedCost<0) return false;
    LastWallTime=Now;const auto K=O.Grant.Object;const auto Old=Entries.find(K);
    if (Old!=Entries.end())
    {
        if (O.Grant.OwnerEpoch<Old->second.Grant.OwnerEpoch || O.Grant.ControlEpoch<Old->second.Grant.ControlEpoch) return false;
        if (O.Grant.OwnerEpoch!=Old->second.Grant.OwnerEpoch || O.Grant.SimOwner!=Old->second.Grant.SimOwner
            || O.Grant.ControlEpoch!=Old->second.Grant.ControlEpoch || O.Grant.Controller!=Old->second.Grant.Controller
            || O.Grant.State!=Old->second.Grant.State || O.Grant.Dormant!=Old->second.Grant.Dormant
            || O.Grant.StateCustodian!=Old->second.Grant.StateCustodian || O.Policy!=Old->second.Policy) Cancel(K);
        UnindexEntry(Old->second);
    }
    IndexEntry(O);Entries[K]=std::move(O);return true;
}
void OwnershipScheduler::RemoveObject(ObjectKey K)
{Cancel(K);if (const auto I=Entries.find(K);I!=Entries.end()) UnindexEntry(I->second);Entries.erase(K);}
double OwnershipScheduler::ActiveSeconds(double Now) const
{return std::max(0.0,(MembershipPaused?PausedAt:Now)-PausedDuration);}
bool OwnershipScheduler::SetMembershipPaused(bool Paused,double Now)
{
    if (!std::isfinite(Now) || Now<LastWallTime) return false;
    LastWallTime=Now;if (Paused==MembershipPaused) return true;
    if (Paused) PausedAt=Now;else PausedDuration+=Now-PausedAt;
    MembershipPaused=Paused;return true;
}
bool OwnershipScheduler::Request(ObjectKey K,Id Target,HandoffReason Reason,double Now,double Reservation,Id Group)
{
    if (!Entries.contains(K)) return false;
    std::vector<ObjectKey> Scope{K};std::set<ObjectKey> Seen{K};
    const Id Domain=Entries.at(K).Grant.OwnershipGroup;
    if (const auto DomainMembers=GroupMembers.find(Domain);Domain.Valid() && DomainMembers!=GroupMembers.end())
        for (const auto& Other:DomainMembers->second)
        {
            const auto& O=Entries.at(Other);
            if (Other!=K && O.Grant.State!=Life::Destroyed && O.Grant.SimOwner.Valid())
            {Scope.push_back(Other);Seen.insert(Other);}
        }
    for (std::size_t I=0;I<Scope.size();++I)
        if (const auto Children=ParentWorkChildren.find(Scope[I]);Children!=ParentWorkChildren.end())
            for (const auto& Child:Children->second)
                if (Entries.at(Child).Grant.State<Life::Dead && Seen.insert(Child).second) Scope.push_back(Child);
    if (!Group.Valid())
    {
        if (const auto Existing=Pending.find(K);Existing!=Pending.end() && Existing->second.Target==Target) Group=Existing->second.Group;
        if (!Group.Valid() && Scope.size()>1) Group=StableId("GuLiObjectWorkHandoff/v1",K.StableId.ToString()+std::to_string(NextTransfer));
    }

    for (const auto& Key:Scope) if (!RequestOne(Key,Target,Reason,Now,Key==K?Reservation:0,Group))
    {
        if (Group.Valid()) for (const auto& KeyToCancel:Scope)
            if (const auto T=Pending.find(KeyToCancel);T!=Pending.end() && T->second.Group==Group) Cancel(KeyToCancel);
        return false;
    }
    return true;
}
bool OwnershipScheduler::RequestOne(ObjectKey K,Id Target,HandoffReason Reason,double Now,double Reservation,Id Group)
{
    auto& O=Entries.at(K);const auto Owner=O.Grant.Dormant?O.Grant.StateCustodian:O.Grant.SimOwner;
    const auto Source=Members.find(Owner);const auto Recipient=Members.find(Target);
    if (Recipient==Members.end() || !Recipient->second.Ready || Recipient->second.Seat==Role::Observer
        || Source==Members.end() || !Source->second.Ready || (!O.Grant.OwnershipGroup.Valid() && O.Policy!=OwnershipPolicy::ParentWork && Playable(O.Grant.Faction) && O.Grant.Faction!=Recipient->second.Faction)) return false;
    const auto Existing=Pending.find(K);
    if (Existing!=Pending.end())
    {
        if (Existing->second.Target==Target && Existing->second.Group==Group) return true;
        if (Reason==HandoffReason::IdleOffload || (Existing->second.Group.Valid() && Existing->second.Reason==HandoffReason::SharedGroup)) return false;
        Cancel(K);
    }
    if (Owner==Target) return true;
    if (!NextTransfer || O.Grant.OwnerEpoch==UINT64_MAX) return false;
    ObjectHandoff T;T.IdValue=NextTransfer++;T.Object=K;T.Source=Owner;T.Target=Target;T.Group=Group;
    T.PreviousOwnerVersion=O.Grant.OwnerEpoch;T.NextOwnerVersion=O.Grant.OwnerEpoch+1;
    T.Reason=Reason;T.Deadline=LastWallTime+Config.HandoffTimeoutSeconds;
    T.ReservedMilliseconds=0;
    Pending.emplace(K,T);Actions.push_back({HandoffActionKind::PrepareTarget,T});return true;
}
bool OwnershipScheduler::Transfer(ObjectKey K,Id Target,HandoffReason Reason,double Now,Id Group)
{
    if (!Entries.contains(K) || !std::isfinite(Now) || Now<LastWallTime) return false;
    LastWallTime=Now;return Request(K,Target,Reason,ActiveSeconds(Now),0,Group);
}
void OwnershipScheduler::Cancel(ObjectKey K)
{
    const auto I=Pending.find(K);if (I==Pending.end()) return;
    const Id Group=I->second.Group;
    std::vector<ObjectKey> Keys{K};
    if (Group.Valid()) for (const auto& [Other,T]:Pending) if (Other!=K && T.Group==Group) Keys.push_back(Other);
    for (const auto& Key:Keys) {Actions.push_back({HandoffActionKind::Cancel,Pending.at(Key)});Pending.erase(Key);}
}
bool OwnershipScheduler::TargetPrepared(Id Player,ObjectKey K,Revision Transfer)
{
    const auto I=Pending.find(K);if (I==Pending.end() || I->second.IdValue!=Transfer || I->second.Target!=Player) return false;
    if (!I->second.TargetPrepared)
    {I->second.TargetPrepared=true;Actions.push_back({HandoffActionKind::FreezeSource,I->second});}
    return true;
}
bool OwnershipScheduler::SourceFrozen(Id Player,ObjectKey K,Revision Transfer,Revision Version)
{
    const auto I=Pending.find(K);
    if (!Version || I==Pending.end() || I->second.IdValue!=Transfer || I->second.Source!=Player || !I->second.TargetPrepared
        || (I->second.FrozenState && I->second.FrozenState!=Version)) return false;
    I->second.FrozenState=Version;TryCommit(K);return true;
}
bool OwnershipScheduler::TargetInstalled(Id Player,ObjectKey K,Revision Transfer,Revision Version)
{
    const auto I=Pending.find(K);
    if (!Version || I==Pending.end() || I->second.IdValue!=Transfer || I->second.Target!=Player || !I->second.TargetPrepared
        || (I->second.InstalledState && I->second.InstalledState!=Version)) return false;
    I->second.InstalledState=Version;TryCommit(K);return true;
}
void OwnershipScheduler::TryCommit(ObjectKey K)
{
    const auto I=Pending.find(K);if (I==Pending.end()) return;const auto T=I->second;
    if (!T.FrozenState || T.InstalledState!=T.FrozenState) return;
    std::vector<ObjectHandoff> Group;
    if (T.Group.Valid())
    {
        for (const auto& [Other,V]:Pending) if (V.Group==T.Group)
        {if (!V.FrozenState || V.InstalledState!=V.FrozenState) return;Group.push_back(V);}
    }
    else Group.push_back(T);
    for (const auto& V:Group)
    {
        const auto& A=Entries.at(V.Object).Grant;
        if ((A.Dormant?A.StateCustodian:A.SimOwner)!=V.Source || A.OwnerEpoch!=V.PreviousOwnerVersion) {Cancel(K);return;}
    }
    for (const auto& V:Group)
    {
        auto& A=Entries.at(V.Object).Grant;
        if (A.Dormant) A.StateCustodian=V.Target;else A.SimOwner=V.Target;
        A.OwnerEpoch=V.NextOwnerVersion;if (A.OwnershipGroup.Valid()) ++A.GroupEpoch;Pending.erase(V.Object);Actions.push_back({HandoffActionKind::Commit,V});
    }
}
bool OwnershipScheduler::Reject(Id Player,ObjectKey K,Revision Transfer)
{
    const auto I=Pending.find(K);
    if (I==Pending.end() || I->second.IdValue!=Transfer || (I->second.Source!=Player && I->second.Target!=Player)) return false;
    Cancel(K);return true;
}
void OwnershipScheduler::Poll(double Now)
{
    if (!std::isfinite(Now) || Now<LastWallTime) return;LastWallTime=Now;
    const double At=ActiveSeconds(Now);
    std::vector<ObjectKey> Keys;for (const auto& [K,T]:Pending) Keys.push_back(K);
    for (const auto& K:Keys) if (const auto T=Pending.find(K);T!=Pending.end())
        {if (T->second.Deadline<=At) Cancel(K);else TryCommit(K);}

}
std::vector<HandoffAction> OwnershipScheduler::TakeActions()
{return std::exchange(Actions,{});}
}
