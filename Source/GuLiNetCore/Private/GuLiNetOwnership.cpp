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
    if (!Pending.empty() || !std::isfinite(S.IdleSeconds) || S.IdleSeconds<0
        || !std::isfinite(S.EvaluationSeconds) || S.EvaluationSeconds<=0
        || !std::isfinite(S.HandoffTimeoutSeconds) || S.HandoffTimeoutSeconds<=0
        || !std::isfinite(S.MinimumRelativeGap) || S.MinimumRelativeGap<0 || S.MinimumRelativeGap>=1
        || !std::isfinite(S.MinimumGapMilliseconds) || S.MinimumGapMilliseconds<0
        || !S.MaximumBackgroundBatch || S.MaximumBackgroundBatch>4096) return false;
    Config=S;return true;
}
double OwnershipScheduler::Load::Mean() const
{double Sum=0;for (std::uint32_t I=0;I<Count;++I) Sum+=Samples[I];return Count?Sum/Count:0;}
bool OwnershipScheduler::SetMember(OwnershipMember M)
{
    if (!M.Player.Valid() || !Playable(M.Faction) || M.Seat>Role::Observer) return false;
    const auto Old=Members.find(M.Player);
    if (Old!=Members.end() && (Old->second.Faction!=M.Faction || Old->second.Seat!=M.Seat || !M.Ready))
    {
        for (auto& [K,A]:Activity) if (Entries.at(K).Grant.Controller==M.Player)
        {A.Selected=false;A.LastDeselection=ActiveSeconds(LastWallTime);}
        Selections.erase(M.Player);SelectionSets.erase(M.Player);
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
    for (auto& [K,A]:Activity) if (Entries.at(K).Grant.Controller==Player)
    {A.Selected=false;A.LastDeselection=ActiveSeconds(LastWallTime);}
    Members.erase(Player);Loads.erase(Player);Selections.erase(Player);SelectionSets.erase(Player);
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
        if (O.Grant.Controller!=Old->second.Grant.Controller)
        {Activity[K].Selected=false;Activity[K].LastDeselection=ActiveSeconds(Now);}
    }
    else Activity.emplace(K,OwnershipActivity{false,ActiveSeconds(Now),ActiveSeconds(Now)});
    Entries[K]=std::move(O);return true;
}
void OwnershipScheduler::RemoveObject(ObjectKey K)
{Cancel(K);Entries.erase(K);Activity.erase(K);}
bool OwnershipScheduler::ReportLoad(Id Player,double Milliseconds)
{
    if (!Members.contains(Player) || !std::isfinite(Milliseconds) || Milliseconds<0) return false;
    auto& L=Loads[Player];L.Samples[L.Next]=Milliseconds;L.Next=(L.Next+1)%3;L.Count=std::min(L.Count+1,3u);return true;
}
double OwnershipScheduler::ActiveSeconds(double Now) const
{return std::max(0.0,(MembershipPaused?PausedAt:Now)-PausedDuration);}
bool OwnershipScheduler::SetMembershipPaused(bool Paused,double Now)
{
    if (!std::isfinite(Now) || Now<LastWallTime) return false;
    LastWallTime=Now;if (Paused==MembershipPaused) return true;
    if (Paused) PausedAt=Now;else PausedDuration+=Now-PausedAt;
    MembershipPaused=Paused;return true;
}
bool OwnershipScheduler::EligibleCommander(Id Player,ObjectKey K) const
{
    const auto M=Members.find(Player);const auto O=Entries.find(K);
    return M!=Members.end() && M->second.Ready && M->second.Seat==Role::Commander
        && O!=Entries.end() && O->second.Policy==OwnershipPolicy::CommanderActivity
        && O->second.Grant.Faction==M->second.Faction && O->second.Grant.Controller==Player
        && O->second.Grant.State==Life::Alive && !O->second.Grant.Dormant;
}
bool OwnershipScheduler::Select(Id Player,Revision Version,std::span<const ObjectKey> Keys,double Now)
{
    if (!Version || !std::isfinite(Now) || Now<LastWallTime || Keys.size()>100000) return false;
    const auto M=Members.find(Player);
    if (M==Members.end() || M->second.Seat!=Role::Commander || !M->second.Ready) return false;
    std::set<ObjectKey> Selected;
    for (const auto& K:Keys) if (!EligibleCommander(Player,K) || !Selected.insert(K).second) return false;
    if (const auto V=Selections.find(Player);V!=Selections.end() && Version<=V->second)
        return Version==V->second && SelectionSets.at(Player)==Selected;
    LastWallTime=Now;const auto At=ActiveSeconds(Now);Selections[Player]=Version;SelectionSets[Player]=Selected;
    for (auto& [K,A]:Activity) if (Entries.at(K).Grant.Controller==Player)
    {
        const bool IsSelected=Selected.contains(K);
        if (A.Selected && !IsSelected) A.LastDeselection=At;
        A.Selected=IsSelected;
        if (IsSelected)
        {
            A.LastOperation=At;
            Request(K,Player,HandoffReason::Selection,At);
        }
    }
    return true;
}
bool OwnershipScheduler::Operate(Id Player,std::span<const ObjectKey> Keys,double Now)
{
    if (!std::isfinite(Now) || Now<LastWallTime || Keys.empty() || Keys.size()>100000) return false;
    for (const auto& K:Keys) if (!EligibleCommander(Player,K)) return false;
    LastWallTime=Now;const auto At=ActiveSeconds(Now);
    for (const auto& K:Keys) {Activity[K].LastOperation=At;Request(K,Player,HandoffReason::DirectOperation,At);}
    return true;
}
bool OwnershipScheduler::Request(ObjectKey K,Id Target,HandoffReason Reason,double Now,double Reservation,Id Group)
{
    if (!Entries.contains(K)) return false;
    std::vector<ObjectKey> Scope{K};std::set<ObjectKey> Seen{K};
    const Id Domain=Entries.at(K).Grant.OwnershipGroup;
    if (Domain.Valid())
        for (const auto& [Other,O]:Entries) if (Other!=K && O.Grant.OwnershipGroup==Domain
            && O.Grant.State!=Life::Destroyed && O.Grant.SimOwner.Valid())
        {Scope.push_back(Other);Seen.insert(Other);}
    for (std::size_t I=0;I<Scope.size();++I)
        for (const auto& [Child,O]:Entries) if (O.Parent==Scope[I] && O.Policy==OwnershipPolicy::ParentWork
            && O.Grant.State<Life::Dead && Seen.insert(Child).second) Scope.push_back(Child);
    if (!Group.Valid())
    {
        if (const auto Existing=Pending.find(K);Existing!=Pending.end() && Existing->second.Target==Target) Group=Existing->second.Group;
        if (!Group.Valid() && Scope.size()>1) Group=StableId("GuLiObjectWorkHandoff/v1",K.StableId.ToString()+std::to_string(NextTransfer));
    }
    if (Reason==HandoffReason::IdleOffload)
    {
        std::size_t Count=0;for (const auto& [_,T]:Pending) if (T.Reason==Reason && Entries.at(T.Object).Grant.Faction==Entries.at(K).Grant.Faction) ++Count;
        if (Count+Scope.size()>Config.MaximumBackgroundBatch) return false;
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
    T.ReservedMilliseconds=Reservation>0?Reservation:Estimate(K,Target);
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
double OwnershipScheduler::Weight(Id Player) const
{
    double W=0;for (const auto& [K,O]:Entries) if (O.Grant.SimOwner==Player && O.Grant.State==Life::Alive && !O.Grant.Dormant)
        W+=O.Grant.EstimatedCost;return W;
}
double OwnershipScheduler::Reserved(Id Player) const
{
    double Cost=0;for (const auto& [K,T]:Pending) if (T.Target==Player) Cost+=T.ReservedMilliseconds;return Cost;
}
double OwnershipScheduler::Estimate(ObjectKey K,Id Target) const
{
    const auto& O=Entries.at(K);const double TargetWeight=Weight(Target);
    const auto T=Loads.find(Target);
    if (TargetWeight>0 && T!=Loads.end() && T->second.Count)
        return T->second.Mean()*O.Grant.EstimatedCost/TargetWeight;
    const double SourceWeight=Weight(O.Grant.SimOwner);const auto S=Loads.find(O.Grant.SimOwner);
    return SourceWeight>0 && S!=Loads.end() && S->second.Count
        ? S->second.Mean()*O.Grant.EstimatedCost/SourceWeight : 0;
}
void OwnershipScheduler::Offload(Team Faction,double Now)
{
    Id Commander;for (const auto& [P,M]:Members) if (M.Faction==Faction && M.Seat==Role::Commander && M.Ready) Commander=P;
    if (!Commander.Valid() || !Loads.contains(Commander) || Loads.at(Commander).Count<3) return;
    for (const auto& [K,T]:Pending) if (T.Reason==HandoffReason::IdleOffload && Entries.at(K).Grant.Faction==Faction) return;
    const double SourceWeight=Weight(Commander);if (SourceWeight<=0) return;
    const double SourceMean=Loads.at(Commander).Mean();double Remaining=SourceMean;
    std::uint32_t Count=0;
    for (const auto& [K,O]:Entries)
    {
        const auto& A=Activity.at(K);
        if (O.Policy!=OwnershipPolicy::CommanderActivity || O.Grant.SimOwner!=Commander || O.Grant.Controller!=Commander
            || O.Grant.State!=Life::Alive || O.Grant.Dormant || A.Selected || Pending.contains(K)
            || Now-std::max(A.LastOperation,A.LastDeselection)<Config.IdleSeconds || O.Grant.EstimatedCost<=0) continue;
        const double Released=SourceMean*O.Grant.EstimatedCost/SourceWeight;
        Id Best;double BestMaximum=Remaining,BestReserve=0;
        for (const auto& [P,M]:Members)
        {
            const auto L=Loads.find(P);const double TargetWeight=Weight(P);
            if (P==Commander || M.Faction!=Faction || !M.Ready || M.Seat==Role::Observer || L==Loads.end() || L->second.Count<3) continue;
            const double Target=L->second.Mean()+Reserved(P);
            const double Difference=Remaining-Target;
            if (Difference<Config.MinimumGapMilliseconds || Difference<Remaining*Config.MinimumRelativeGap) continue;
            // Relative weights are converted to milliseconds using this client's
            // measured cost. A currently empty peer uses the source conversion.
            const double Added=TargetWeight>0?L->second.Mean()*O.Grant.EstimatedCost/TargetWeight:Released;
            const double Maximum=std::max(std::max(0.0,Remaining-Released),Target+Added);
            if (Maximum<BestMaximum) {Best=P;BestMaximum=Maximum;BestReserve=Added;}
        }
        if (Best.Valid() && Request(K,Best,HandoffReason::IdleOffload,Now,BestReserve))
        {Remaining=std::max(0.0,Remaining-Released);if (++Count>=Config.MaximumBackgroundBatch) break;}
    }
}
void OwnershipScheduler::Poll(double Now)
{
    if (!std::isfinite(Now) || Now<LastWallTime) return;LastWallTime=Now;
    const double At=ActiveSeconds(Now);
    std::vector<ObjectKey> Keys;for (const auto& [K,T]:Pending) Keys.push_back(K);
    for (const auto& K:Keys) if (const auto T=Pending.find(K);T!=Pending.end())
        {if (T->second.Deadline<=Now) Cancel(K);else TryCommit(K);}
    if (MembershipPaused) return;
    // Retry pinned selections after a cancelled transfer, without unpinning them.
    for (const auto& [K,A]:Activity) if (A.Selected && EligibleCommander(Entries.at(K).Grant.Controller,K))
        Request(K,Entries.at(K).Grant.Controller,HandoffReason::Selection,At);
    if (At<NextEvaluation) return;NextEvaluation=At+Config.EvaluationSeconds;
    Offload(Team::Red,At);Offload(Team::Blue,At);
}
std::vector<HandoffAction> OwnershipScheduler::TakeActions()
{return std::exchange(Actions,{});}
}
