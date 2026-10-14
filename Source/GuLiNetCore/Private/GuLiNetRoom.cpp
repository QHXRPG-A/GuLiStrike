#include "GuLiNetRoom.h"
#include "GuLiNetHash.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace GuLiNet
{
Room::Room(Id IdValue,std::string Config,std::string Map,Defaults Value,OwnershipSettings Ownership)
    :Match(IdValue),ConfigurationHash(std::move(Config)),LevelHash(std::move(Map)),Settings(Value),Scheduler(Ownership) {}
bool Room::ConfigureDefinitions(std::map<std::string,OwnershipDefinition> Values)
{
    if (!Directory.empty()) return false;
    for (const auto& [Name,V]:Values)
        if (Name.empty() || V.Policy>OwnershipPolicy::ParentWork || V.ControllerSeat>Role::Observer
            || (V.RetireOnVacancy && V.Policy!=OwnershipPolicy::ControllerPinned)
            || (V.Policy==OwnershipPolicy::SharedGroup)!=!V.Group.empty()) return false;
    Definitions=std::move(Values);return true;
}
bool Room::Live(Id Player) const
{
    const auto M=Players.find(Player);
    return M!=Players.end() && M->second.Online && M->second.Ready && M->second.Seat!=Role::Observer;
}
Id Room::Controller(Team Faction,Role Seat) const
{
    for (const auto& [P,M]:Players) if (Live(P) && M.Faction==Faction && M.Seat==Seat) return P;
    return {};
}
Id Room::Custodian(Team Faction,Id Exclude) const
{
    for (const auto& [P,M]:Players) if (P!=Exclude && Live(P) && M.Faction==Faction) return P;
    return {};
}
Id Room::Destination(const Authority& A,Id Exclude) const
{
    if (A.Policy==OwnershipPolicy::ControllerPinned && A.Controller!=Exclude && Live(A.Controller)) return A.Controller;
    if (A.Policy==OwnershipPolicy::SharedGroup) return GroupDestination(A.OwnershipGroup,Exclude);
    if (A.Policy==OwnershipPolicy::Fixed)
    {
        const Id C=Controller(A.Faction,A.ControllerSeat);
        if (C!=Exclude && C.Valid()) return C;
        if (A.SimOwner!=Exclude && Live(A.SimOwner)) return A.SimOwner;
        return Custodian(A.Faction,Exclude);
    }
    Id Best;double Cost=0;
    for (const auto& [P,M]:Players)
    {
        if (P==Exclude || !Live(P) || (Playable(A.Faction) && M.Faction!=A.Faction)) continue;
        // Initial placement uses one unit (relative cost). Runtime offload uses
        // the scheduler's measured milliseconds and converted reservations.
        double Candidate=0;
        for (const auto& [K,O]:Directory) if (O.Grant.SimOwner==P && O.Grant.State<Life::Dead) Candidate+=O.Grant.EstimatedCost;
        for (const auto& [K,T]:PendingAssignments) if (T.ProposedOwner==P) Candidate+=Directory.at(K).Grant.EstimatedCost;
        if (!Best.Valid() || Candidate<Cost) {Best=P;Cost=Candidate;}
    }
    return Best;
}
bool Room::Join(Member M,const std::string& Config,const std::string& Map,double Now,std::string& Error)
{
    const auto Reject=[&](const char* Why){Error=Why;return false;};
    if (!M.Player.Valid() || !Playable(M.Faction) || M.Seat>=Role::Observer || !std::isfinite(Now)) return Reject("Invalid member");
    if (Config!=ConfigurationHash || Map!=LevelHash) return Reject("Configuration or map mismatch");
    if (CurrentPhase==RoomPhase::Aborted || CurrentPhase==RoomPhase::Finished) return Reject("Match ended");
    if (const auto Existing=Players.find(M.Player);Existing!=Players.end())
    {
        auto& Old=Existing->second;
        if (Old.Faction!=M.Faction || Old.Carrier!=M.Carrier || Old.ProductUserId!=M.ProductUserId) return Reject("Identity mismatch");
        if (Old.Seat!=M.Seat) return Reject("Role is locked for this match");
        // Reattaching an existing control connection does not create a member
        // change or reset any grant. The host rejects concurrent connections.
        if (Old.Online) {Old.Endpoint=std::move(M.Endpoint);Old.LastSeen=Now;return true;}
        if (Old.SeatVersion==UINT64_MAX) return Reject("Seat version exhausted");
        M.Seat=Old.Seat;M.SeatVersion=Old.SeatVersion+1;M.OrdinalSlot=Old.OrdinalSlot;
    }
    if (MembershipValue) return Reject("Member handoff in progress");
    std::size_t Count=0;
    for (const auto& [P,V]:Players) if (V.Online && V.Faction==M.Faction)
    {++Count;if (M.Seat!=Role::Observer && V.Seat==M.Seat) return Reject("Role seat occupied");}
    if (Count>=3 || (Players.size()>=64 && !Players.contains(M.Player))) return Reject("Room is full");
    if (!M.OrdinalSlot)
    {for (const auto& [P,V]:Players) M.OrdinalSlot=std::max(M.OrdinalSlot,V.OrdinalSlot);++M.OrdinalSlot;}
    M.Online=true;M.Ready=false;M.LastSeen=Now;M.ReservedCost=0;
    Players[M.Player]=M;Scheduler.SetMember({M.Player,M.Faction,M.Seat,false});
    if (CurrentPhase==RoomPhase::Running) return BeginMembership(M.Player,MembershipOperation::Join,false,Now);
    return true;
}
bool Room::Ready(Id Player)
{
    const auto M=Players.find(Player);if (M==Players.end() || !M->second.Online) return false;
    M->second.Ready=true;Scheduler.SetMember({Player,M->second.Faction,M->second.Seat,true});return true;
}
bool Room::Heartbeat(Id Player,double Now)
{
    const auto M=Players.find(Player);if (M==Players.end() || !M->second.Online || !std::isfinite(Now)) return false;
    M->second.LastSeen=Now;return true;
}
bool Room::LoadReport(Id Player,double Cost)
{
    const auto M=Players.find(Player);
    if (M==Players.end() || !M->second.Online || !Scheduler.ReportLoad(Player,Cost)) return false;
    M->second.MeasuredCost=Cost;return true;
}
bool Room::BeginMembership(Id Player,MembershipOperation Op,bool Abrupt,double Now)
{
    if (MembershipValue || !NextMembership) return false;
    MembershipChange V;V.Version=NextMembership++;V.Player=Player;V.Operation=Op;V.Abrupt=Abrupt;V.Deadline=Now+Settings.MembershipTimeoutSeconds;
    for (const auto& [P,M]:Players) if (M.Online && (!Abrupt || P!=Player)) V.Waiting.insert(P);
    MembershipValue=std::move(V);CurrentPhase=RoomPhase::MembershipChanging;Scheduler.SetMembershipPaused(true,Now);return true;
}
bool Room::BeginLeave(Id Player,bool Abrupt,double Now)
{
    const auto M=Players.find(Player);
    if (M==Players.end() || !M->second.Online || !BeginMembership(Player,MembershipOperation::Leave,Abrupt,Now)) return false;
    if (Abrupt) {M->second.Online=false;M->second.Ready=false;Scheduler.RemoveMember(Player);}
    for (auto& [K,O]:Directory)
    {
        auto& A=O.Grant;
        if (A.SimOwner!=Player && A.StateCustodian!=Player) continue;
        if (Abrupt)
        {
            A.SimOwner={};A.StateCustodian={};
            if (A.RetireOnVacancy) A.State=Life::Destroyed;
            Scheduler.SetObject({A,A.Policy,O.Parent},Now);
        }
        else if (const auto To=Destination(A,Player);To.Valid()) Scheduler.Transfer(K,To,HandoffReason::Leave,Now);
    }
    for (auto I=PendingAssignments.begin();I!=PendingAssignments.end();)
        if (I->second.ProposedOwner==Player) I=PendingAssignments.erase(I);else ++I;
    return true;
}
bool Room::MembershipReady(Id Player,Revision Version,double Now)
{
    if (!MembershipValue || MembershipValue->Version!=Version || !Players.contains(Player)) return false;
    MembershipValue->Waiting.erase(Player);FinishMembership(Now);return true;
}
void Room::FinishMembership(double Now)
{
    if (!MembershipValue || !MembershipValue->Waiting.empty() || !PendingAssignments.empty()) return;
    RefreshGroups(Now);
    if (!Scheduler.Transfers().empty()) return;
    const auto V=*MembershipValue;
    for (const auto& [K,O]:Directory)
    {
        const auto& A=O.Grant;
        if (A.State<Life::Dead && !A.Dormant && !A.SimOwner.Valid()
            && !(A.Policy==OwnershipPolicy::ControllerPinned && !A.Controller.Valid())
            && Destination(A,V.Operation==MembershipOperation::Leave?V.Player:Id{}).Valid()) return;
    }
    if (V.Operation==MembershipOperation::Leave)
    {
        for (const auto& [K,T]:Scheduler.Transfers()) if (T.Source==V.Player) return;
        auto& M=Players.at(V.Player);M.Online=false;M.Ready=false;Scheduler.RemoveMember(V.Player);
        for (auto& [K,O]:Directory) if (O.Grant.SimOwner==V.Player || O.Grant.StateCustodian==V.Player)
        {O.Grant.SimOwner={};O.Grant.StateCustodian={};Scheduler.SetObject({O.Grant,O.Grant.Policy,O.Parent},Now);}
    }
    MembershipValue.reset();CurrentPhase=RoomPhase::Running;Scheduler.SetMembershipPaused(false,Now);
    RefreshControllers(Now);RefreshGroups(Now);
}
bool Room::Register(ObjectRegistration O,double Now,Id Creator)
{
    const auto D=Definitions.find(O.Definition);auto& A=O.Grant;
    if (D==Definitions.end() || !A.Object.Valid() || !std::isfinite(A.EstimatedCost) || A.EstimatedCost<0
        || Directory.size()>=100000 || Directory.contains(A.Object) || (Creator.Valid() && !Live(Creator))) return false;
    const Id Group=D->second.Group.empty()?Id{}:StableId("GuLiOwnershipGroup/v1",D->second.Group);
    if (Creator.Valid() && Playable(A.Faction) && Players.at(Creator).Faction!=A.Faction
        && (!Group.Valid() || !Groups.contains(Group) || Groups.at(Group).Owner!=Creator))
    {
        // A resource-group producer may create the other team's ordinary unit.
        // Provenance permits creation, never control of that team's object.
        const auto Parent=Directory.find(O.Parent);
        if (Parent==Directory.end() || Parent->second.Grant.SimOwner!=Creator
            || !Parent->second.Grant.OwnershipGroup.Valid()) return false;
    }
    if (Group.Valid())
    {
        auto& G=Groups[Group];A.OwnershipGroup=Group;A.GroupEpoch=G.Version;
        if (Creator.Valid() && G.Owner.Valid() && Creator!=G.Owner) return false;
    }
    A.Policy=D->second.Policy;A.ControllerSeat=D->second.ControllerSeat;A.RetireOnVacancy=D->second.RetireOnVacancy;
    A.Controller=Controller(A.Faction,A.ControllerSeat);A.ControlEpoch=1;A.OwnerEpoch=0;A.SimOwner={};A.StateCustodian={};A.State=Life::Registered;
    if (Creator.Valid()) {A.SimOwner=Creator;A.OwnerEpoch=1;A.State=Life::Alive;}
    if ((A.Policy==OwnershipPolicy::RetainedSource || A.Policy==OwnershipPolicy::ParentWork) && Creator.Valid())
    {A.Controller=Creator;A.SimOwner=Creator;A.OwnerEpoch=1;A.State=Life::Alive;}
    if (A.Policy==OwnershipPolicy::ControllerPinned && !A.Controller.Valid())
    {
        if (!A.RetireOnVacancy) {A.Dormant=true;A.State=Life::Alive;A.StateCustodian=Custodian(A.Faction);A.OwnerEpoch=1;}
        else if (CurrentPhase!=RoomPhase::Preparing) A.State=Life::Destroyed;
    }
    Directory.emplace(A.Object,O);return Scheduler.SetObject({A,A.Policy,O.Parent},Now);
}
bool Room::UpdateLifecycle(Id Sender,ObjectKey K,Revision Version,Life State,double Now)
{
    const auto O=Directory.find(K);if (O==Directory.end()) return false;auto& A=O->second.Grant;
    if (!Live(Sender) || (A.Dormant?A.StateCustodian:A.SimOwner)!=Sender || A.OwnerEpoch!=Version || State<A.State || State>Life::Destroyed) return false;
    A.State=State;return Scheduler.SetObject({A,A.Policy,O->second.Parent},Now);
}
bool Room::SetController(ObjectKey K,Id Player,double Now)
{
    const auto O=Directory.find(K);const auto M=Players.find(Player);
    if (O==Directory.end() || M==Players.end() || !Live(Player)) return false;
    auto& A=O->second.Grant;
    if (A.Faction!=M->second.Faction || A.ControllerSeat!=M->second.Seat || (A.Policy==OwnershipPolicy::RetainedSource || A.Policy==OwnershipPolicy::ParentWork)
        || A.State>=Life::Dead || A.ControlEpoch==UINT64_MAX) return false;
    if (A.Controller==Player) return true;
    if (A.Policy==OwnershipPolicy::ControllerPinned && A.SimOwner.Valid() && A.SimOwner!=Player) return false;
    A.Controller=Player;++A.ControlEpoch;A.Dormant=false;A.StateCustodian={};return Scheduler.SetObject({A,A.Policy,O->second.Parent},Now);
}
void Room::RefreshControllers(double Now)
{
    for (auto& [K,O]:Directory)
    {
        auto& A=O.Grant;if (A.State>=Life::Dead || (A.Policy==OwnershipPolicy::RetainedSource || A.Policy==OwnershipPolicy::ParentWork)) continue;
        const auto Wanted=Controller(A.Faction,A.ControllerSeat);
        if (A.Controller==Wanted && (!A.Dormant || Live(A.StateCustodian) || !Custodian(A.Faction).Valid())) continue;
        if (A.ControlEpoch==UINT64_MAX || A.OwnerEpoch==UINT64_MAX) {Abort("Authority version exhausted");return;}
        if (A.Policy==OwnershipPolicy::ControllerPinned)
        {
            if (A.RetireOnVacancy && A.State==Life::Registered && !A.Controller.Valid())
            {
                if (Wanted.Valid()) {A.Controller=Wanted;++A.ControlEpoch;}
                else if (CurrentPhase!=RoomPhase::Preparing) A.State=Life::Destroyed;
            }
            else if (A.RetireOnVacancy && A.Controller!=Wanted) {A.State=Life::Destroyed;A.Controller={};++A.ControlEpoch;}
            else if (!Wanted.Valid())
            {A.Controller={};++A.ControlEpoch;A.Dormant=true;const auto Previous=A.SimOwner;A.StateCustodian=Live(Previous)?Previous:Custodian(A.Faction);A.SimOwner={};if (Previous!=A.StateCustodian) ++A.OwnerEpoch;}
            else if (A.SimOwner==Wanted || (A.Dormant && A.StateCustodian==Wanted))
            {A.Controller=Wanted;++A.ControlEpoch;A.SimOwner=Wanted;A.Dormant=false;A.StateCustodian={};}
            else if (!A.SimOwner.Valid() && !A.StateCustodian.Valid())
            {A.Controller=Wanted;++A.ControlEpoch;A.Dormant=false;}
            else if (MembershipValue && Wanted.Valid())
                Scheduler.Transfer(K,Wanted,HandoffReason::ControllerJoin,Now);
        }
        else {A.Controller=Wanted;++A.ControlEpoch;}
        Scheduler.SetObject({A,A.Policy,O.Parent},Now);
    }
}
Id Room::GroupDestination(Id Group,Id Exclude) const
{
    const auto G=Groups.find(Group);const Id Current=G==Groups.end()?Id{}:G->second.Owner;
    if (Current!=Exclude && Live(Current) && Players.at(Current).Seat==Role::Commander) return Current;
    for (const auto& [P,M]:Players) if (P!=Exclude && Live(P) && M.Seat==Role::Commander) return P;
    if (Current!=Exclude && Live(Current)) return Current;
    for (const auto& [P,M]:Players) if (P!=Exclude && Live(P)) return P;
    return {};
}
void Room::RefreshGroups(double Now)
{
    const Id Exclude=MembershipValue && MembershipValue->Operation==MembershipOperation::Leave?MembershipValue->Player:Id{};
    for (auto& [Key,G]:Groups)
    {
        const Id Wanted=GroupDestination(Key,Exclude);if (!Wanted.Valid() || Wanted==G.Owner) continue;
        if (!Live(G.Owner))
        {
            if (G.Version==UINT64_MAX) {Abort("Group version exhausted");return;}
            G.Owner=Wanted;++G.Version;
            for (auto& [K,O]:Directory) if (O.Grant.OwnershipGroup==Key)
            {O.Grant.GroupEpoch=G.Version;Scheduler.SetObject({O.Grant,O.Grant.Policy,O.Parent},Now);}
        }
        else if (MembershipValue)
        {
            for (const auto& [K,O]:Directory) if (O.Grant.OwnershipGroup==Key && O.Grant.State!=Life::Destroyed && O.Grant.SimOwner==G.Owner)
            {Scheduler.Transfer(K,Wanted,HandoffReason::SharedGroup,Now);break;}
        }
    }
}
std::vector<Assignment> Room::Propose(double Now)
{
    std::vector<Assignment> Out;
    if (CurrentPhase==RoomPhase::Aborted || CurrentPhase==RoomPhase::Finished) return Out;
    RefreshControllers(Now);RefreshGroups(Now);
    // New ordinary units start at their producer for atomic birth, then enter
    // their own team's simulation pool through the normal state handoff.
    for (const auto& [K,O]:Directory)
        if (O.Grant.Policy==OwnershipPolicy::CommanderActivity && O.Grant.State==Life::Alive
            && Live(O.Grant.SimOwner) && Players.at(O.Grant.SimOwner).Faction!=O.Grant.Faction)
            if (const auto To=Destination(O.Grant);To.Valid()) Scheduler.Transfer(K,To,HandoffReason::Placement,Now);
    for (const auto& [K,O]:Directory)
    {
        const auto& A=O.Grant;
        if (A.State>=Life::Dead || A.Dormant || A.SimOwner.Valid() || PendingAssignments.contains(K) || A.OwnerEpoch==UINT64_MAX
            || (A.Policy==OwnershipPolicy::ControllerPinned && !A.Controller.Valid())) continue;
        const Id Exclude=MembershipValue && MembershipValue->Operation==MembershipOperation::Leave?MembershipValue->Player:Id{};
        const auto Best=Destination(A,Exclude);if (!Best.Valid()) continue;
        Assignment Offer{K,Best,A.OwnerEpoch+1,Now+Settings.ReadyTimeoutSeconds};PendingAssignments.emplace(K,Offer);Out.push_back(Offer);
    }
    return Out;
}
bool Room::CommitAssignment(Id Player,ObjectKey K,Revision Version,double Now)
{
    const auto I=PendingAssignments.find(K);const auto O=Directory.find(K);
    if (!Live(Player) || I==PendingAssignments.end() || O==Directory.end() || I->second.ProposedOwner!=Player || I->second.ProposedEpoch!=Version) return false;
    auto& A=O->second.Grant;if (A.Policy==OwnershipPolicy::ControllerPinned && A.Controller!=Player) return false;
    A.SimOwner=Player;A.OwnerEpoch=Version;if (A.State==Life::Registered) A.State=Life::Alive;
    PendingAssignments.erase(I);return Scheduler.SetObject({A,A.Policy,O->second.Parent},Now);
}
bool Room::Select(Id P,Revision V,std::span<const ObjectKey> K,double Now) {return Scheduler.Select(P,V,K,Now);}
bool Room::Operate(Id P,std::span<const ObjectKey> K,double Now) {return Scheduler.Operate(P,K,Now);}
std::vector<HandoffAction> Room::TakeOwnershipActions(double Now)
{
    auto Actions=Scheduler.TakeActions();
    for (const auto& V:Actions)
    {
        if (V.Kind==HandoffActionKind::Commit)
        {
            auto& A=Directory.at(V.Transfer.Object).Grant;
            if (A.Dormant) A.StateCustodian=V.Transfer.Target;else A.SimOwner=V.Transfer.Target;
            A.OwnerEpoch=V.Transfer.NextOwnerVersion;
        }
    }
    for (const auto& V:Actions) if (V.Kind==HandoffActionKind::Commit)
    {
        auto& A=Directory.at(V.Transfer.Object).Grant;
        if (A.OwnershipGroup.Valid())
        {
            auto& G=Groups.at(A.OwnershipGroup);
            if (G.Owner!=V.Transfer.Target) {G.Owner=V.Transfer.Target;++G.Version;}
            A.GroupEpoch=G.Version;
        }
    }
    return Actions;
}
bool Room::ImportMember(Member M)
{
    if (!M.Player.Valid() || !Playable(M.Faction) || M.Seat>Role::Observer || !M.SeatVersion || Players.contains(M.Player)) return false;
    Players.emplace(M.Player,M);return Scheduler.SetMember({M.Player,M.Faction,M.Seat,M.Online && M.Ready});
}
void Room::Abort(std::string Reason) {FailureReason=std::move(Reason);CurrentPhase=RoomPhase::Aborted;}
void Room::Poll(double Now)
{
    Scheduler.Poll(Now);
    for (auto I=PendingAssignments.begin();I!=PendingAssignments.end();)
        if (I->second.Deadline<=Now) I=PendingAssignments.erase(I);else ++I;
    if (CurrentPhase==RoomPhase::Preparing && InitialRosterReady && !Players.empty() && !Directory.empty() && PendingAssignments.empty())
    {
        bool Ready=true;
        for (const auto& [P,M]:Players) if (M.Online && !M.Ready) Ready=false;
        for (const auto& [K,O]:Directory) if (O.Grant.State<Life::Dead && !O.Grant.Dormant && !O.Grant.SimOwner.Valid()
            && !(O.Grant.RetireOnVacancy && !O.Grant.Controller.Valid())) Ready=false;
        if (Ready) CurrentPhase=RoomPhase::Running;
    }
    if (MembershipValue && MembershipValue->Deadline<=Now)
    {
        // A join that cannot receive the initial state is rejected locally to
        // membership; existing objects never rewind to an earlier battle state.
        const Id P=MembershipValue->Player;
        if (MembershipValue->Operation==MembershipOperation::Join)
        {Players.at(P).Online=false;Players.at(P).Ready=false;Scheduler.RemoveMember(P);MembershipValue->Waiting.clear();}
        else
        {
            for (const auto& [K,O]:Directory) if (O.Grant.SimOwner==P || O.Grant.StateCustodian==P)
                if (const auto T=Scheduler.Transfers().find(K);T!=Scheduler.Transfers().end()) Scheduler.Reject(P,K,T->second.IdValue);
            Players.at(P).Online=false;Players.at(P).Ready=false;Scheduler.RemoveMember(P);
            for (auto& [K,O]:Directory) if (O.Grant.SimOwner==P || O.Grant.StateCustodian==P)
            {O.Grant.SimOwner={};O.Grant.StateCustodian={};Scheduler.SetObject({O.Grant,O.Grant.Policy,O.Parent},Now);}
            MembershipValue->Waiting.clear();
        }
    }
    RefreshGroups(Now);FinishMembership(Now);
    if (CurrentPhase==RoomPhase::Running)
    {
        for (const auto F:{Team::Red,Team::Blue})
        {
            const auto Index=F==Team::Red?0:1;bool Online=false;
            for (const auto& [P,M]:Players) if (M.Online && M.Faction==F) Online=true;
            if (Online) EmptySince[Index]=-1;
            else if (EmptySince[Index]<0) EmptySince[Index]=Now;
            else if (Now-EmptySince[Index]>=Settings.EmptyTeamTimeoutSeconds) Abort("Empty team timeout");
        }
    }
}
}
