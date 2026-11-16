#include "GuLiNetRoom.h"
#include "GuLiNetTeamAuthority.h"
#include "GuLiNetHash.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace GuLiNet
{
Room::Room(Id IdValue,std::string Config,std::string Map,Defaults Value,OwnershipSettings Ownership)
    :Match(IdValue),ConfigurationHash(std::move(Config)),LevelHash(std::move(Map)),Settings(Value),Scheduler(Ownership),
    LeaveTransferWindow(std::max(1u,Ownership.MaximumLeaveBatch)) {}
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
{return Seat==Role::Observer?Id{}:ResolveTeamSeat(Players,Faction,Seat);}
Id Room::Custodian(Team Faction,Id Exclude) const
{
    for (const auto& [P,M]:Players) if (P!=Exclude && Live(P) && M.Faction==Faction) return P;
    return {};
}
Id Room::Destination(const Authority& A,Id Exclude,const std::set<Id>* Unavailable) const
{
    const auto Desired=A.OwnershipGroup.Valid()?GroupDestination(A.OwnershipGroup,Exclude)
        :ResolveTeamSeat(Players,A.Faction,IsMassObject(A.Object.Kind)?Role::Commander:Role::Ground,Exclude);
    return Unavailable && Unavailable->contains(Desired)?Id{}:Desired;
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
        if (!Old.Online || (MembershipValue && MembershipValue->Operation==MembershipOperation::Leave
            && MembershipValue->Player==M.Player)) return Reject("Player has left this match");
        if (Old.Faction!=M.Faction || Old.Carrier!=M.Carrier || Old.ProductUserId!=M.ProductUserId) return Reject("Identity mismatch");
        if (Old.Seat!=M.Seat) return Reject("Role is locked for this match");
        // Only current members can reattach their control connection.
        Old.Endpoint=std::move(M.Endpoint);Old.LastSeen=Now;return true;
    }
    if (MembershipValue) return Reject("Member handoff in progress");
    std::size_t Count=0;
    const auto Slot = RoleSlotIndex(M.Faction, M.Seat);
    for (const auto& [P,V]:Players) if (V.Online && V.Faction==M.Faction && V.Seat!=Role::Observer)
    {
        ++Count;
        if (RoleSlotIndex(V.Faction, V.Seat) == Slot) return Reject("Role seat occupied");
    }
    if (Count>=SeatsPerTeam || Players.size()>=64) return Reject("Room is full");
    if (!M.OrdinalSlot)
    {for (const auto& [P,V]:Players) M.OrdinalSlot=std::max(M.OrdinalSlot,V.OrdinalSlot);++M.OrdinalSlot;}
    M.Online=true;M.Ready=false;M.LastSeen=Now;M.ReservedCost=0;
    AuthorityDirty=true;Players[M.Player]=M;Scheduler.SetMember({M.Player,M.Faction,M.Seat,false});
    if (CurrentPhase==RoomPhase::Running) return BeginMembership(M.Player,MembershipOperation::Join,false,Now);
    return true;
}
bool Room::Ready(Id Player)
{
    const auto M=Players.find(Player);if (M==Players.end() || !M->second.Online) return false;
    AuthorityDirty=true;M->second.Ready=true;Scheduler.SetMember({Player,M->second.Faction,M->second.Seat,true});return true;
}
bool Room::Heartbeat(Id Player,double Now)
{
    const auto M=Players.find(Player);if (M==Players.end() || !M->second.Online || !std::isfinite(Now)) return false;
    M->second.LastSeen=Now;return true;
}
bool Room::BeginMembership(Id Player,MembershipOperation Op,bool Abrupt,double Now)
{
    if (MembershipValue || !NextMembership) return false;
    MembershipChange V;V.Version=NextMembership++;V.Player=Player;V.Operation=Op;V.Abrupt=Abrupt;V.Deadline=Now+Settings.MembershipTimeoutSeconds;
    for (const auto& [P,M]:Players) if (M.Online && (!Abrupt || P!=Player)) V.Waiting.insert(P);
    AuthorityDirty=true;MembershipValue=std::move(V);CurrentPhase=RoomPhase::MembershipChanging;Scheduler.SetMembershipPaused(true,Now);return true;
}
bool Room::BeginLeave(Id Player,bool Abrupt,double Now)
{
    const auto M=Players.find(Player);
    if (M==Players.end() || !M->second.Online || !std::isfinite(Now)) return false;
    // Concurrent disconnects belong to the same paused roster change. A dead
    // peer cannot remain a candidate while another departure is being drained.
    if (MembershipValue)
    {if (!Abrupt) return false;MembershipValue->Waiting.erase(Player);}
    else if (!BeginMembership(Player,MembershipOperation::Leave,Abrupt,Now)) return false;
    for (auto& [Key,G]:Groups) if (G.Owner==Player && !ResourceRecoveries.contains(Key))
    {
        if (G.RequestEpoch==UINT64_MAX) {Abort("Resource request epoch exhausted");return false;}
        // The backup stores and publishes the cut; it need not simulate it.
        // Install directly on the normal final destination so FinishMembership
        // does not immediately schedule another full-state group transfer.
        const Id Target=GroupDestination(Key,Player);
        if (!Target.Valid() || (Abrupt&&(!G.Backup.Valid()||!Live(G.Backup))))
        {Abort("Resource owner lost without an available snapshot custodian");return false;}
        ResourceRecovery V;auto& S=V.State;S.Group=Key;S.Owner=G.Owner;S.Backup=G.Backup;S.OwnerVersion=G.Version;
        S.RequestEpoch=++G.RequestEpoch;S.Recovery=MembershipValue->Version;S.PreviousOwner=Player;
        S.Publisher=Abrupt?G.Backup:Player;S.Target=Target;S.Abrupt=Abrupt;S.Deadline=Now+Settings.MembershipTimeoutSeconds;
        for (const auto& [P,Member]:Players) if (Member.Online&&(!Abrupt||P!=Player)) V.Waiting.insert(P);
        ResourceRecoveries.emplace(Key,std::move(V));
    }
    for (auto& [Key,V]:ResourceRecoveries) if (Abrupt)
    {
        V.Waiting.erase(Player);
        if (Player==V.State.Publisher||Player==V.State.Target)
        {Abort("Resource snapshot publisher or install target disconnected during recovery");return false;}
    }
    if (Abrupt) {M->second.Online=false;M->second.Ready=false;Scheduler.RemoveMember(Player);}
    for (auto& [K,O]:Directory)
    {
        auto& A=O.Grant;
        if (A.SimOwner!=Player && A.StateCustodian!=Player) continue;
        if (Abrupt)
        {
            A.SimOwner={};A.StateCustodian={};
            if (A.RetireOnVacancy || A.Policy==OwnershipPolicy::RetainedSource) A.State=Life::Destroyed;
            MarkDirectoryChanged(K);
            Scheduler.SetObject({A,A.Policy,O.Parent},Now);
        }
    }
    for (auto I=PendingAssignments.begin();I!=PendingAssignments.end();)
        if (I->second.ProposedOwner==Player) I=PendingAssignments.erase(I);else ++I;
    ScheduleLeaveTransfers(Now);
    return true;
}
void Room::ScheduleLeaveTransfers(double Now)
{
    if (!MembershipValue || MembershipValue->Operation!=MembershipOperation::Leave || MembershipValue->Abrupt
        || !Players.at(MembershipValue->Player).Online) return;
    const Id Player=MembershipValue->Player;
    std::map<Id,std::pair<Id,std::size_t>> Batches;
    // A global storage window does not bound one destination's wire backlog.
    // Admit at most two ordinary 32-root cuts per peer before starting more
    // five-second timers. A required ownership group still enters whole.
    const auto PeerWindow=std::min<std::size_t>(LeaveTransferWindow,64);
    std::map<Id,std::size_t> PerPeer;std::set<Id> Saturated;
    for (const auto& [K,T]:Scheduler.Transfers()) if (T.Source==Player) ++PerPeer[T.Target];
    for (const auto& [P,Count]:PerPeer) if (Count>=PeerWindow) Saturated.insert(P);
    const auto Admitted=[&](Id To,std::size_t Before)
    {
        PerPeer[To]+=Scheduler.Transfers().size()-Before;
        if (PerPeer[To]>=PeerWindow) Saturated.insert(To);
    };
    for (const auto& [K,O]:Directory)
    {
        if (Scheduler.Transfers().size()>=LeaveTransferWindow) break;
        const auto Current=Scheduler.Objects().find(K);
        if (Current==Scheduler.Objects().end() || !HasPrivateCustody(Current->second.Grant,Player)
            || Scheduler.Transfers().contains(K)) continue;
        // Only admitted transfers start their five-second timer. A resource or
        // parent/work group still enters as one indivisible scheduler request.
        if (const auto To=Destination(Current->second.Grant,Player,&Saturated);To.Valid())
        {
            if (Saturated.contains(To)) continue;
            const auto& A=Current->second.Grant;
            if (A.OwnershipGroup.Valid() || A.Policy==OwnershipPolicy::ParentWork)
            {
                const auto Before=Scheduler.Transfers().size();
            if (!InResourceRecovery(K)&&Scheduler.Transfer(K,To,HandoffReason::Leave,Now)) Admitted(To,Before);
                continue;
            }
            // Ordinary custody uses the existing atomic group/cut protocol as
            // well. One private-state packet per object serializes a late
            // departure behind hundreds of capture/ACK turns. Keep the same
            // admission window and deadlines, with at most 32 roots per cut.
            auto& Batch=Batches[To];
            if (!Batch.first.Valid() || Batch.second>=std::min<std::size_t>(32,LeaveTransferWindow))
            {
                const auto Seed=std::to_string(MembershipValue->Version)+":"+To.ToString()+":"
                    +std::to_string(static_cast<unsigned>(K.Kind))+":"+K.StableId.ToString()+":"
                    +std::to_string(K.Generation)+":"+std::to_string(A.OwnerEpoch);
                Batch={StableId("GuLiLeaveCut/v1",Seed),0};
            }
            const auto PreviousSize=Scheduler.Transfers().size();
            if (Scheduler.Transfer(K,To,HandoffReason::Leave,Now,Batch.first))
            {
                Batch.second+=Scheduler.Transfers().size()-PreviousSize;
                Admitted(To,PreviousSize);
            }
        }
    }
}
bool Room::MembershipReady(Id Player,Revision Version,double Now)
{
    if (!MembershipValue || MembershipValue->Version!=Version || !Players.contains(Player)) return false;
    MembershipValue->Waiting.erase(Player);FinishMembership(Now);return true;
}
void Room::FinishMembership(double Now)
{
    if (!MembershipValue || !MembershipValue->Waiting.empty() || !PendingAssignments.empty() || !ResourceRecoveries.empty()) return;
    // The last ready receipt may arrive before the next scheduling poll. A
    // returning pinned controller must recover private custody while this
    // membership change still permits that handoff, before resuming the room.
    RefreshControllers(Now);RefreshGroups(Now);
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
        if (!V.Abrupt)
            for (const auto& [K,O]:Directory)
                if (HasPrivateCustody(O.Grant,V.Player) && Destination(O.Grant,V.Player).Valid()) return;
        for (const auto& [K,T]:Scheduler.Transfers()) if (T.Source==V.Player) return;
        auto& M=Players.at(V.Player);M.Online=false;M.Ready=false;Scheduler.RemoveMember(V.Player);
        for (auto& [K,O]:Directory) if (O.Grant.SimOwner==V.Player || O.Grant.StateCustodian==V.Player)
        {O.Grant.SimOwner={};O.Grant.StateCustodian={};MarkDirectoryChanged(K);Scheduler.SetObject({O.Grant,O.Grant.Policy,O.Parent},Now);}
    }
    MembershipValue.reset();CurrentPhase=RoomPhase::Running;Scheduler.SetMembershipPaused(false,Now);
    RefreshControllers(Now);RefreshGroups(Now);
}
bool Room::CanRecoverBirth(const ObjectRegistration& O,Id Player) const
{
    const auto& A=O.Grant;const auto D=Definitions.find(O.Definition);const auto Old=Players.find(A.SimOwner);
    if (!Live(Player) || D==Definitions.end() || A.Policy!=D->second.Policy || !A.SourceLease.Valid()
        || !A.OwnerEpoch || A.OwnerEpoch==UINT64_MAX || !A.ControlEpoch || A.ControlEpoch==UINT64_MAX
        || !A.SimOwner.Valid() || A.SimOwner==Player || Old==Players.end() || Old->second.Online) return false;
    if (D->second.Policy==OwnershipPolicy::SharedGroup)
    {
        const auto Parent=Directory.find(O.Parent);
        const auto Group=Groups.find(ResolveAuthorityGroup(A,D->second,Parent==Directory.end()?nullptr:&Parent->second.Grant));
        if (Group==Groups.end() || Group->second.Owner!=Player) return false;
    }
    std::set<ObjectKey> Seen;ObjectKey Parent=O.Parent;
    while (Parent.Valid() && Seen.size()<64 && Seen.insert(Parent).second)
    {
        const auto I=Directory.find(Parent);if (I==Directory.end()) return false;const auto& P=I->second.Grant;
        if (HasPrivateCustody(P,Player) || (P.State==Life::Destroyed && P.SimOwner==Player)) return true;
        if (P.State!=Life::Destroyed) return false;
        Parent=I->second.Parent;
    }
    return false;
}
bool Room::Register(ObjectRegistration O,double Now,Id Creator)
{
    const auto D=Definitions.find(O.Definition);auto& A=O.Grant;
    if (D==Definitions.end() || !A.Object.Valid() || !std::isfinite(A.EstimatedCost) || A.EstimatedCost<0
        || LivingDirectory.size()>=MaxLiveObjects || Directory.contains(A.Object) || (Creator.Valid() && !Live(Creator))) return false;
    const auto Original=A;
    const auto PriorOwner=Players.find(A.SimOwner);
    const bool Recovery=Creator.Valid() && A.SimOwner.Valid() && A.SimOwner!=Creator
        && PriorOwner!=Players.end() && !PriorOwner->second.Online;
    if (Recovery && !CanRecoverBirth(O,Creator)) return false;
    const auto ParentEntry=Directory.find(O.Parent);
    const Id Group=ResolveAuthorityGroup(A,D->second,ParentEntry==Directory.end()?nullptr:&ParentEntry->second.Grant);
    if (Creator.Valid() && !Recovery && Playable(A.Faction) && Players.at(Creator).Faction!=A.Faction
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
        if (Creator.Valid() && G.Owner.Valid() && Creator!=G.Owner
            && (ParentEntry==Directory.end() || ParentEntry->second.Grant.SimOwner!=Creator)) return false;
    }
    A.Policy=D->second.Policy;A.ControllerSeat=D->second.ControllerSeat;A.RetireOnVacancy=D->second.RetireOnVacancy;
    A.Controller=Controller(A.Faction,A.ControllerSeat);A.ControlEpoch=1;A.OwnerEpoch=0;A.SimOwner={};A.StateCustodian={};A.State=Life::Registered;
    if (Creator.Valid()) {A.SimOwner=Group.Valid()?GroupDestination(Group):Creator;A.OwnerEpoch=1;A.State=Life::Alive;}
    if ((A.Policy==OwnershipPolicy::RetainedSource || A.Policy==OwnershipPolicy::ParentWork) && Creator.Valid())
    {A.Controller=Creator;A.SimOwner=Creator;A.OwnerEpoch=1;A.State=Life::Alive;}
    if (Recovery)
    {
        A.OwnerEpoch=Original.OwnerEpoch+1;A.ControlEpoch=Original.ControlEpoch;
        A.State=std::max(Life::Alive,Original.State);
        if (A.Policy==OwnershipPolicy::RetainedSource || A.Policy==OwnershipPolicy::ParentWork) A.Controller=Original.Controller;
        if (A.Controller!=Original.Controller) ++A.ControlEpoch;
        // A vacancy ended the old Air tenure even if a replacement is ready
        // before this last committed birth's metadata can be recovered.
        if (A.Policy==OwnershipPolicy::ControllerPinned && A.RetireOnVacancy && A.Controller!=Original.Controller)
        {A.State=Life::Destroyed;A.Controller={};}
    }
    if (A.Policy==OwnershipPolicy::ControllerPinned && !A.Controller.Valid())
    {
        if (!A.RetireOnVacancy)
        {A.Dormant=true;A.State=std::max(Life::Alive,A.State);A.StateCustodian=Custodian(A.Faction);A.SimOwner={};if (!Recovery) A.OwnerEpoch=1;}
        else if (CurrentPhase!=RoomPhase::Preparing) A.State=Life::Destroyed;
    }
    Directory.emplace(A.Object,O);MarkDirectoryChanged(A.Object);return Scheduler.SetObject({A,A.Policy,O.Parent},Now);
}
bool Room::UpdateLifecycle(Id Sender,ObjectKey K,Revision Version,Life State,double Now)
{
    const auto O=Directory.find(K);if (O==Directory.end()) return false;auto& A=O->second.Grant;
    if (!Live(Sender) || (A.Dormant?A.StateCustodian:A.SimOwner)!=Sender || A.OwnerEpoch!=Version || State<A.State || State>Life::Destroyed) return false;
    if (A.State!=State) {A.State=State;MarkDirectoryChanged(K);}
    return Scheduler.SetObject({A,A.Policy,O->second.Parent},Now);
}
bool Room::SetController(ObjectKey K,Id Player,double Now)
{
    const auto O=Directory.find(K);const auto M=Players.find(Player);
    if (O==Directory.end() || M==Players.end() || !Live(Player)) return false;
    auto& A=O->second.Grant;
    if (A.Faction!=M->second.Faction || !MatchesControllerRole(M->second.Seat,A.ControllerSeat) || (A.Policy==OwnershipPolicy::RetainedSource || A.Policy==OwnershipPolicy::ParentWork)
        || A.State>=Life::Dead || A.ControlEpoch==UINT64_MAX) return false;
    if (A.Controller==Player) return true;
    if (A.Policy==OwnershipPolicy::ControllerPinned && A.SimOwner.Valid() && A.SimOwner!=Player) return false;
    A.Controller=Player;++A.ControlEpoch;A.Dormant=false;A.StateCustodian={};MarkDirectoryChanged(K);
    return Scheduler.SetObject({A,A.Policy,O->second.Parent},Now);
}
void Room::RefreshControllers(double Now)
{
    for (auto& [K,O]:Directory)
    {
        auto& A=O.Grant;if (A.State>=Life::Dead || (A.Policy==OwnershipPolicy::RetainedSource || A.Policy==OwnershipPolicy::ParentWork)) continue;
        const auto Wanted=Controller(A.Faction,A.ControllerSeat);
        if (A.Controller==Wanted && (!A.Dormant || Live(A.StateCustodian) || !Custodian(A.Faction).Valid())) continue;
        if (A.ControlEpoch==UINT64_MAX || A.OwnerEpoch==UINT64_MAX) {Abort("Authority version exhausted");return;}
        const auto PreviousGrant=A;
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
        if (A!=PreviousGrant) MarkDirectoryChanged(K);
        Scheduler.SetObject({A,A.Policy,O.Parent},Now);
    }
}
Id Room::GroupDestination(Id Group,Id Exclude) const
{
    Team F=Team::None;TeamDomain Domain=TeamDomain::World;
    if (!DecodeTeamAuthorityGroup(Group,F,Domain))
    {
        const auto Root=GroupRoots.find(Group);
        if (Root==GroupRoots.end()) return {};
        const auto O=Directory.find(Root->second);if (O==Directory.end()) return {};
        F=Playable(O->second.Grant.Faction)?O->second.Grant.Faction:NeutralAuthorityTeam(Root->second.StableId);
    }
    return ResolveTeamSeat(Players,F,Domain==TeamDomain::Mass?Role::Commander:Role::Ground,Exclude);
}
void Room::RefreshGroups(double Now)
{
    const Id Exclude=MembershipValue && MembershipValue->Operation==MembershipOperation::Leave?MembershipValue->Player:Id{};
    for (auto& [Key,G]:Groups)
    {
        if (ResourceRecoveries.contains(Key)) continue;
        const Id Wanted=GroupDestination(Key,Exclude);if (!Wanted.Valid() || Wanted==G.Owner) continue;
        const bool UnassignedStartup=CurrentPhase==RoomPhase::Preparing && std::none_of(Directory.begin(),Directory.end(),[&](const auto& Entry)
        {const auto& A=Entry.second.Grant;return A.OwnershipGroup==Key && A.State<Life::Dead
            && (A.Dormant?A.StateCustodian:A.SimOwner).Valid();});
        if (!Live(G.Owner) || UnassignedStartup)
        {
            if (G.Version==UINT64_MAX) {Abort("Group version exhausted");return;}
            G.Owner=Wanted;++G.Version;
            for (auto& [K,O]:Directory) if (O.Grant.OwnershipGroup==Key)
            {O.Grant.GroupEpoch=G.Version;MarkDirectoryChanged(K);Scheduler.SetObject({O.Grant,O.Grant.Policy,O.Parent},Now);}
        }
        else
        {
            for (const auto& [K,O]:Directory) if (O.Grant.OwnershipGroup==Key && O.Grant.State!=Life::Destroyed && O.Grant.SimOwner==G.Owner)
            {Scheduler.Transfer(K,Wanted,HandoffReason::SharedGroup,Now);break;}
        }
    }
    RefreshResourceBackups();
}
std::vector<Assignment> Room::Propose(double Now)
{
    std::vector<Assignment> Out;
    if (CurrentPhase==RoomPhase::Aborted || CurrentPhase==RoomPhase::Finished) return Out;
    if (AuthorityDirty) {AuthorityDirty=false;RefreshControllers(Now);RefreshGroups(Now);}
    ScheduleLeaveTransfers(Now);
    for (const auto K:UnassignedObjects)
    {
        const auto& A=Directory.at(K).Grant;
        if (InResourceRecovery(K) || A.State>=Life::Dead || A.Dormant || A.SimOwner.Valid()
            || PendingAssignments.contains(K) || A.OwnerEpoch==UINT64_MAX) continue;
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
    if (A.Policy==OwnershipPolicy::SharedGroup)
    {
        const auto Group=Groups.find(A.OwnershipGroup);
        if (Group==Groups.end() || Group->second.Owner!=Player)
        {PendingAssignments.erase(I);return false;}
        A.GroupEpoch=Group->second.Version;
    }
    A.SimOwner=Player;A.OwnerEpoch=Version;if (A.State==Life::Registered) A.State=Life::Alive;
    MarkDirectoryChanged(K);
    PendingAssignments.erase(I);return Scheduler.SetObject({A,A.Policy,O->second.Parent},Now);
}
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
            MarkDirectoryChanged(V.Transfer.Object);
        }
    }
    for (const auto& V:Actions) if (V.Kind==HandoffActionKind::Commit)
    {
        auto& A=Directory.at(V.Transfer.Object).Grant;
        if (A.OwnershipGroup.Valid())
        {
            auto& G=Groups.at(A.OwnershipGroup);
            if (G.Owner!=V.Transfer.Target) {G.Owner=V.Transfer.Target;++G.Version;AuthorityDirty=true;}
            A.GroupEpoch=G.Version;
            MarkDirectoryChanged(V.Transfer.Object);
        }
    }
    return Actions;
}
bool Room::ImportMember(Member M)
{
    if (!M.Player.Valid() || !Playable(M.Faction) || M.Seat>Role::Observer || !M.SeatVersion || Players.contains(M.Player)) return false;
    if (M.Online && M.Seat != Role::Observer)
    {
        const auto Slot = RoleSlotIndex(M.Faction, M.Seat);
        for (const auto& [Player, Existing] : Players)
            if (Existing.Online && RoleSlotIndex(Existing.Faction, Existing.Seat) == Slot)
                return false;
    }
    if (!Scheduler.SetMember({M.Player,M.Faction,M.Seat,M.Online && M.Ready})) return false;
    Players.emplace(M.Player,M);AuthorityDirty=true;
    return true;
}
void Room::MirrorMember(Member M)
{
    Players.insert_or_assign(M.Player,M);AuthorityDirty=true;
    Scheduler.SetMember({M.Player,M.Faction,M.Seat,M.Online&&M.Ready});
}
bool Room::MirrorRegistration(const ObjectRegistration& O,double Now)
{
    if (!Definitions.contains(O.Definition) || !O.Grant.Object.Valid()) return false;
    const auto Old=Directory.find(O.Grant.Object);
    if (Old!=Directory.end() && (Old->second.Grant.OwnerEpoch>O.Grant.OwnerEpoch
        || Old->second.Grant.ControlEpoch>O.Grant.ControlEpoch)) return true;
    Directory.insert_or_assign(O.Grant.Object,O);MarkDirectoryChanged(O.Grant.Object);
    if (O.Grant.OwnershipGroup.Valid())
    {
        auto& G=Groups[O.Grant.OwnershipGroup];
        if (O.Grant.GroupEpoch>=G.Version) {G.Version=O.Grant.GroupEpoch;G.Owner=O.Grant.SimOwner;}
    }
    return Scheduler.SetObject({O.Grant,O.Grant.Policy,O.Parent},Now);
}
bool Room::ObserveRegistration(Id Sender,const ObjectRegistration& O,double Now)
{
    const auto I=Directory.find(O.Grant.Object);
    if (I==Directory.end()) return Register(O,Now,Sender);
    const auto& A=I->second.Grant;
    if (!Live(Sender) || A.SimOwner!=Sender || O.Grant.SimOwner!=Sender || O.Grant.OwnerEpoch!=A.OwnerEpoch
        || O.Definition!=I->second.Definition || O.Ordinal!=I->second.Ordinal || O.Parent!=I->second.Parent
        || O.Grant.OwnershipGroup!=A.OwnershipGroup || O.Grant.ControlEpoch<A.ControlEpoch) return false;
    if (A.Faction!=O.Grant.Faction) AuthorityDirty=true;
    return MirrorRegistration(O,Now);
}
void Room::MirrorGroups(const std::map<Id,ResourceGroupState>& Values,double Now)
{
    for (const auto& [Key,V]:Values)
    {
        Groups[Key]={V.Owner,V.OwnerVersion,V.Backup,V.RequestEpoch};
        if (V.Recovery)
        {
            auto& R=ResourceRecoveries[Key];R.State=V;R.State.Deadline=Now+Settings.MembershipTimeoutSeconds;
            for (const auto& [P,M]:Players) if (M.Online && P!=V.PreviousOwner) R.Waiting.insert(P);
        }
        else ResourceRecoveries.erase(Key);
    }
}
void Room::MirrorMembership(const MembershipChange& V)
{
    MembershipValue=V;NextMembership=std::max(NextMembership,V.Version+1);
    CurrentPhase=RoomPhase::MembershipChanging;AuthorityDirty=true;
}
void Room::Abort(std::string Reason) {FailureReason=std::move(Reason);CurrentPhase=RoomPhase::Aborted;}
void Room::Poll(double Now)
{
    for (const auto& [Key,R]:ResourceRecoveries) if (Now>=R.State.Deadline)
    {Abort("Team state recovery timed out");return;}
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
        Abort("Membership recovery timed out; match ended without a result");return;
    }
    if (AuthorityDirty) {AuthorityDirty=false;RefreshControllers(Now);RefreshGroups(Now);}
    FinishMembership(Now);
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
