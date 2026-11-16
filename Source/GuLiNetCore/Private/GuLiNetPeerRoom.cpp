#include "GuLiNetPeerRoom.h"
#include "GuLiNetHash.h"
#include "GuLiNetTeamAuthority.h"
#include <algorithm>

namespace GuLiNet
{
namespace
{
Bytes Registration(const ObjectRegistration& O)
{Writer W;WriteAuthority(W,O.Grant);W.Text(O.Definition);W.U32(O.Ordinal);W.Key(O.Parent);return W.Take();}
bool ReadRegistration(Reader& R,ObjectRegistration& O)
{return ReadAuthority(R,O.Grant)&&R.Text(O.Definition,256)&&R.U32(O.Ordinal)&&R.Key(O.Parent);}
}
PeerRoom::PeerRoom(Id M,Id P,const RuntimeManifest& Manifest,Send S):Match(M),Player(P),Output(std::move(S))
{
    if (!HashManifest(Manifest,Hashes,Error)) return;
    Defaults DefaultsValue;DefaultsValue.MembershipTimeoutSeconds=15;
    Core=std::make_unique<Room>(M,Hashes.Configuration,Hashes.Map,DefaultsValue);
    std::map<std::string,OwnershipDefinition> Definitions;std::map<ObjectKey,ObjectRecord> Objects;
    if (!ReadOwnershipDefinitions(Manifest,Definitions,Error)||!Core->ConfigureDefinitions(std::move(Definitions))
        ||!RuntimeObjects(Manifest,Objects,Error)) {Error="Invalid peer room manifest";return;}
    // Parents can sort after their children. Preserve the already resolved group
    // from RuntimeObjects when installing this immutable startup directory.
    for (const auto& [K,O]:Objects)
        if (!Core->MirrorRegistration({O.Grant,O.Definition,O.Ordinal,O.Parent},0)) {Error="Invalid startup object";return;}
}
Id PeerRoom::PairIdentity(Id M,Id A,Id B)
{if (B<A) std::swap(A,B);return StableId("GuLiPeerConnection/v2",M.ToString()+A.ToString()+B.ToString());}
Message PeerRoom::Control(MessageKind K,Bytes B,ObjectKey O) const
{Message M;M.Header.Match=Match;M.Header.Epoch=1;M.Header.Kind=K;M.Header.Object=O;M.Body=std::move(B);return M;}
bool PeerRoom::Emit(Id To,const Message& M)
{
    auto& Queue=Outbox[To];
    if (Queue.empty() && Output(To,M)) return true;
    if (OutboxBytes+M.Body.size()>16*1024*1024) {Error="Room control queue capacity exceeded";return false;}
    Queue.push_back(M);OutboxBytes+=M.Body.size();return true;
}
void PeerRoom::Local(const Message& M) {Emit({},M);}
void PeerRoom::Broadcast(const Message& M)
{
    Local(M);Bytes Body;if (!EncodeMessage(M,Body)) {End("Room message exceeds wire budget");return;}
    auto Outer=Control(MessageKind::RoomUpdate,std::move(Body));Outer.Header.Sender=Player;
    for (const auto& [P,V]:Core->Members()) if (P!=Player&&V.Online) Emit(P,Outer);
}
bool PeerRoom::Start(std::map<Id,Member> Members,Id Host,double Now,std::string& Why)
{
    if (!Error.empty()||!Core||Members.size()<2||Members.size()>4||!Members.contains(Player)||!Members.contains(Host))
    {Why=Error.empty()?"Invalid peer room roster":Error;return false;}
    HostPlayer=Host;NowValue=StartedAt=Now;
    bool Red=false,Blue=false;
    for (const auto& [P,M]:Members) {Red|=M.Faction==Team::Red;Blue|=M.Faction==Team::Blue;if (M.Seat>=Role::Observer) {Why="Playable seats required";return false;}}
    if (!Red || !Blue) {Why="Both teams are required";return false;}
    for (auto& [P,M]:Members)
    {
        M.Online=true;M.Ready=false;M.OrdinalSlot=RoleSlotIndex(M.Faction,M.Seat)+1;
        if (!Core->ImportMember(M)) {Why="Duplicate or invalid peer seat";return false;}
        LastSeen[P]=Now;
    }
    Writer W;W.Text(Hashes.Configuration);W.Text(Hashes.Map);
    return Request(Control(MessageKind::Hello,W.Take()),Now);
}
bool PeerRoom::Request(Message M,double Now)
{
    if (!Core||Ended) return false;
    M.Header.Match=Match;M.Header.Epoch=1;M.Header.Sender=Player;
    if (M.Header.Kind==MessageKind::LoadReport||M.Header.Kind==MessageKind::SelectionActivity||M.Header.Kind==MessageKind::DirectOperation) return true;
    // Births and lifecycle metadata are distributed by their owner. They never
    // wait for a room host before the gameplay transaction can be published.
    const bool All=M.Header.Kind==MessageKind::Hello||M.Header.Kind==MessageKind::PeerStatus
        ||M.Header.Kind==MessageKind::RegisterObject||M.Header.Kind==MessageKind::ObjectEvent||M.Header.Kind==MessageKind::Leave;
    Bytes Body;if (!EncodeMessage(M,Body)) return false;
    auto Outer=Control(MessageKind::RoomRequest,std::move(Body));Outer.Header.Sender=Player;
    if (All)
    {
        Handle(Player,M,Now);
        for (const auto& [P,V]:Core->Members()) if (P!=Player&&V.Online&&!Emit(P,Outer)) return false;
        return true;
    }
    if (Player==HostPlayer) {Handle(Player,M,Now);return true;}
    return Emit(HostPlayer,Outer);
}
bool PeerRoom::Receive(Id Sender,const Message& Outer,double Now)
{
    if (!Core||Ended||!Core->Members().contains(Sender)||!Core->Members().at(Sender).Online) return true;
    Message M;if (!DecodeMessage(Outer.Body,M)||M.Header.Match!=Match||M.Header.Epoch!=1) return true;
    Seen(Sender,Now);
    if (Outer.Header.Kind==MessageKind::RoomRequest)
    {if (M.Header.Sender==Sender) Handle(Sender,M,Now);return true;}
    if (Outer.Header.Kind!=MessageKind::RoomUpdate) return true;
    if (Sender!=HostPlayer) return false; // Keep a future host update queued until failure agreement.
    if (M.Header.Sender.Valid()) return true;
    Local(M);if (M.Header.Kind==MessageKind::MatchResult) Ended=true;return true;
}
void PeerRoom::Seen(Id P,double Now) {LastSeen[P]=Now;}
void PeerRoom::Bootstrap()
{
    if (Bootstrapped||Handshakes.size()!=Core->Members().size()) return;
    Bootstrapped=true;
    Writer W;W.Identity(Player);W.Text(Hashes.Configuration);W.Text(Hashes.Map);Local(Control(MessageKind::Welcome,W.Take()));
    Roster();
    for (const auto& [K,O]:Core->Objects()) Local(Control(MessageKind::RegisterObject,Registration(O),K));
    Writer D;D.U32(static_cast<std::uint32_t>(Core->Objects().size()));D.U8(std::uint8_t(RoomPhase::Preparing));D.U32(0);
    Local(Control(MessageKind::DirectoryComplete,D.Take()));
    Core->AcknowledgeDirectoryUpdates();
}
void PeerRoom::Roster()
{
    const auto Build=[&](Id Recipient)
    {
        Writer W;W.U32(static_cast<std::uint32_t>(Core->Members().size()));
        for (const auto& [P,M]:Core->Members())
        {
            W.Identity(P);W.U8(std::uint8_t(M.Faction));W.U8(std::uint8_t(M.Seat));W.U8(std::uint8_t(M.Carrier));
            W.U8(M.Online);W.U8(M.Ready);W.U64(M.SeatVersion);W.U32(M.OrdinalSlot);
            W.Text(M.Endpoint);W.Text(M.ProductUserId);W.Identity(P==Recipient?Id{}:PairIdentity(Match,P,Recipient));
        }
        return Control(MessageKind::Roster,W.Take());
    };
    Local(Build(Player));
    if (Player==HostPlayer) for (const auto& [P,M]:Core->Members()) if (P!=Player&&M.Online)
    {Bytes B;EncodeMessage(Build(P),B);auto V=Control(MessageKind::RoomUpdate,std::move(B));V.Header.Sender=Player;Emit(P,V);}
}
void PeerRoom::DirectoryComplete()
{
    Writer W;W.U32(static_cast<std::uint32_t>(Core->Objects().size()));W.U8(std::uint8_t(Core->Phase()));
    W.U32(static_cast<std::uint32_t>(Core->Ownership().Transfers().size()));
    for (const auto& [K,T]:Core->Ownership().Transfers()) W.Blob(EncodeHandoff(T));
    Broadcast(Control(MessageKind::DirectoryComplete,W.Take()));
}
void PeerRoom::Handle(Id Sender,const Message& M,double Now)
{
    NowValue=Now;Reader R(M.Body);
    if (M.Header.Kind==MessageKind::Hello)
    {
        std::string Config,Map;
        if (!R.Text(Config,128)||!R.Text(Map,128)||!R.Done()||Config!=Hashes.Configuration||Map!=Hashes.Map)
        {End("Peer configuration or map mismatch");return;}
        Handshakes.insert(Sender);Bootstrap();return;
    }
    if (M.Header.Kind==MessageKind::PeerStatus)
    {
        Id P;std::uint8_t Online;
        if (R.Identity(P)&&R.U8(Online)&&Online<2&&R.Done()&&Core->Members().contains(P)) Reports[{Sender,P}]={Online!=0,Now};
        return;
    }
    if (M.Header.Kind==MessageKind::Error)
    {std::string Reason;if (R.Text(Reason,512) && R.Done()) End("Peer stopped the match without a result: "+Reason);return;}
    if (!Bootstrapped||!Core->Members().contains(Sender)||!Core->Members().at(Sender).Online) return;
    if (M.Header.Kind==MessageKind::RegisterObject)
    {
        ObjectRegistration O;if (!ReadRegistration(R,O)||!R.Done()||O.Grant.Object!=M.Header.Object) return;
        if (Core->ObserveRegistration(Sender,O,Now))
            Local(Control(MessageKind::RegisterObject,Registration(Core->Objects().at(O.Grant.Object)),O.Grant.Object));
        return;
    }
    if (M.Header.Kind==MessageKind::ObjectEvent)
    {
        std::uint8_t LifeValue;
        if (R.U8(LifeValue)&&LifeValue<=std::uint8_t(Life::Destroyed)&&R.Done()
            &&Core->UpdateLifecycle(Sender,M.Header.Object,M.Header.OwnerEpoch,Life(LifeValue),Now))
            Local(Control(MessageKind::RegisterObject,Registration(Core->Objects().at(M.Header.Object)),M.Header.Object));
        return;
    }
    if (M.Header.Kind==MessageKind::Leave && R.Done())
    {
        Leaving.insert(Sender);
        if (Sender==Player) {SetPaused(true);Writer W;W.Text("You left the match");Local(Control(MessageKind::MatchResult,W.Take()));Ended=true;}
        return;
    }
    if (Player!=HostPlayer) return;
    switch (M.Header.Kind)
    {
    case MessageKind::WorldReady: if (R.Done()&&Core->Ready(Sender)) Roster();break;
    case MessageKind::AssignmentReady:
    {Revision V;if (R.U64(V)&&R.Done()) Core->CommitAssignment(Sender,M.Header.Object,V,Now);break;}
    case MessageKind::HandoffReady:
    {ObjectHandoff T;if (DecodeHandoff(M.Body,T)) Core->Ownership().TargetPrepared(Sender,T.Object,T.IdValue);break;}
    case MessageKind::HandoffInstalled:
    {
        std::uint8_t Stage;Bytes B;ObjectHandoff T;
        if (R.U8(Stage)&&Stage<2&&R.Blob(B)&&DecodeHandoff(B,T)&&R.Done())
        {if (Stage) Core->Ownership().TargetInstalled(Sender,T.Object,T.IdValue,T.InstalledState);
         else Core->Ownership().SourceFrozen(Sender,T.Object,T.IdValue,T.FrozenState);}break;
    }
    case MessageKind::HandoffCancel:
    {Revision V;if (R.U64(V)&&R.Done()) Core->Ownership().Reject(Sender,M.Header.Object,V);break;}
    case MessageKind::MembershipReady:
    {Revision V;if (R.U64(V)&&R.Done()) Core->MembershipReady(Sender,V,Now);break;}
    case MessageKind::ResourceSnapshotInstalled:
    {
        Id Group;Revision Recovery,Number;std::string Digest;std::uint32_t Count;std::vector<ObjectRegistration> Objects;
        if (!R.Identity(Group)||!R.U64(Recovery)||!R.U64(Number)||!R.Text(Digest,64)||!R.U32(Count)||Count>MaxLiveObjects) break;
        for (std::uint32_t I=0;I<Count;++I) {ObjectRegistration O;if (!ReadRegistration(R,O)) return;Objects.push_back(std::move(O));}
        if (R.Done()) Core->ResourceSnapshotInstalled(Sender,Group,Recovery,Number,Digest,Objects,Now);break;
    }
    default:break;
    }
}
void PeerRoom::Publish(double Now)
{
    const auto Actions=Core->TakeOwnershipActions(Now);
    if (Core->Membership()&&PublishedMembership!=Core->Membership()->Version)
    {
        const auto& V=*Core->Membership();PublishedMembership=V.Version;
        Writer W;W.U64(V.Version);W.U8(std::uint8_t(V.Operation));W.Identity(V.Player);W.U8(V.Abrupt);
        Broadcast(Control(MessageKind::MembershipBegin,W.Take()));
    }
    std::set<Id> Committed;
    for (const auto& A:Actions)
    {
        if (A.Kind==HandoffActionKind::Commit)
        {
            if (!Committed.insert(A.Transfer.Group).second) continue;
            std::vector<const HandoffAction*> Batch;
            for (const auto& B:Actions) if (B.Kind==HandoffActionKind::Commit&&B.Transfer.Group==A.Transfer.Group) Batch.push_back(&B);
            Writer W;W.U32(static_cast<std::uint32_t>(Batch.size()));
            for (const auto* B:Batch) {W.Blob(EncodeHandoff(B->Transfer));WriteAuthority(W,Core->Objects().at(B->Transfer.Object).Grant);}
            Broadcast(Control(MessageKind::HandoffCommit,W.Take()));
        }
        else
        {
            const auto K=A.Kind==HandoffActionKind::PrepareTarget?MessageKind::HandoffPrepare:
                A.Kind==HandoffActionKind::FreezeSource?MessageKind::HandoffFreeze:MessageKind::HandoffCancel;
            Writer W;W.Blob(EncodeHandoff(A.Transfer));Broadcast(Control(K,W.Take(),A.Transfer.Object));
        }
    }
    bool Changed=false;
    for (const auto K:Core->PendingDirectoryUpdates())
    {
        const auto& O=Core->Objects().at(K);
        if (Published.contains(K)&&Published.at(K)==O.Grant) continue;
        Published[K]=O.Grant;Changed=true;Broadcast(Control(MessageKind::RegisterObject,Registration(O),K));
    }
    Core->AcknowledgeDirectoryUpdates();
    Writer G;const auto Groups=Core->ResourceGroups();G.U32(static_cast<std::uint32_t>(Groups.size()));
    for (const auto& [K,V]:Groups) WriteResourceGroup(G,V);
    auto Data=G.Take();if (Data!=PublishedGroups) {PublishedGroups=Data;Broadcast(Control(MessageKind::ResourceGroups,std::move(Data)));}
    if (Core->Phase()!=PublishedPhase)
    {
        PublishedPhase=Core->Phase();
        if (PublishedPhase==RoomPhase::Running) {Roster();Broadcast(Control(MessageKind::MembershipCommit));SetPaused(false);}
        if (PublishedPhase==RoomPhase::Aborted||PublishedPhase==RoomPhase::Finished) {End(Core->Failure());return;}
    }
    if (Changed&&Core->Phase()==RoomPhase::Preparing) DirectoryComplete();
}
void PeerRoom::Observe(const Message& M,double Now)
{
    if (Player==HostPlayer||!Core) return;
    Reader R(M.Body);
    if (M.Header.Kind==MessageKind::RegisterObject)
    {ObjectRegistration O;if (ReadRegistration(R,O)&&R.Done()) Core->MirrorRegistration(O,Now);}
    else if (M.Header.Kind==MessageKind::ResourceGroups)
    {
        std::uint32_t N;std::map<Id,ResourceGroupState> G;if (!R.U32(N)||N>4096) return;
        for (std::uint32_t I=0;I<N;++I) {ResourceGroupState V;if (!ReadResourceGroup(R,V)) return;G.emplace(V.Group,V);}
        if (R.Done()) Core->MirrorGroups(G,Now);
    }
    else if (M.Header.Kind==MessageKind::Roster)
    {
        std::uint32_t N;if (!R.U32(N)||N>4) return;
        for (std::uint32_t I=0;I<N;++I)
        {
            Member V;Id Pair;std::uint8_t F,S,T,Online,Ready;
            if (!R.Identity(V.Player)||!R.U8(F)||!R.U8(S)||!R.U8(T)||!R.U8(Online)||!R.U8(Ready)||!R.U64(V.SeatVersion)
                ||!R.U32(V.OrdinalSlot)||!R.Text(V.Endpoint,512)||!R.Text(V.ProductUserId,128)||!R.Identity(Pair)) return;
            V.Faction=Team(F);V.Seat=Role(S);V.Carrier=Transport(T);V.Online=Online!=0;V.Ready=Ready!=0;Core->MirrorMember(V);
        }
    }
    else if (M.Header.Kind==MessageKind::MembershipBegin)
    {
        MembershipChange V;std::uint8_t Op,Abrupt;
        if (!R.U64(V.Version)||!R.U8(Op)||!R.Identity(V.Player)||!R.U8(Abrupt)||!R.Done()) return;
        V.Operation=MembershipOperation(Op);V.Abrupt=Abrupt!=0;V.Deadline=Now+15;
        for (const auto& [P,A]:Core->Members()) if (A.Online&&P!=V.Player) V.Waiting.insert(P);
        Core->MirrorMembership(V);
    }
    else if (M.Header.Kind==MessageKind::MembershipCommit) {Core->MirrorPhase(RoomPhase::Running);PublishedPhase=RoomPhase::Running;}
}
void PeerRoom::SetPaused(bool V)
{if (V==Paused) return;Paused=V;Writer W;W.U8(V);Local(Control(MessageKind::RoomPause,W.Take()));}
void PeerRoom::End(const std::string& Reason)
{
    if (Ended) return;Ended=true;Error=Reason.empty()?"Match ended without a result":Reason;
    Writer W;W.Text(Error);
    if (Player==HostPlayer && Core) Broadcast(Control(MessageKind::MatchResult,W.Take()));
    else
    {
        Local(Control(MessageKind::MatchResult,W.Take()));
        if (Core)
        {
            Writer Reason;Reason.Text(Error.substr(0,512));auto Inner=Control(MessageKind::Error,Reason.Take());Inner.Header.Sender=Player;
            Bytes Body;EncodeMessage(Inner,Body);auto Outer=Control(MessageKind::RoomRequest,std::move(Body));Outer.Header.Sender=Player;
            for (const auto& [P,M]:Core->Members()) if (P!=Player && M.Online) Emit(P,Outer);
        }
    }
}
void PeerRoom::Tick(double Now)
{
    // Preserve ordering per recipient without letting a disconnected peer's
    // full reliable channel block status reports and grants to surviving peers.
    for (auto& [To,Queue]:Outbox) while (!Queue.empty())
    {
        const auto& M=Queue.front();
        if (To.Valid() && Core && Core->Members().contains(To) && !Core->Members().at(To).Online) {}
        else if (!Output(To,M)) break;
        OutboxBytes-=M.Body.size();Queue.pop_front();
    }
    if (!Core||Ended) return;NowValue=Now;
    if (!Error.empty()) {End(Error);return;}
    if (!Bootstrapped) {if (Now-StartedAt>60) End("Room handshake timed out");return;}
    const bool Running=Core->Phase()==RoomPhase::Running;
    if (Now-LastReport>=1)
    {
        LastReport=Now;
        for (const auto& [P,M]:Core->Members()) if (P!=Player&&M.Online)
        {Writer W;W.Identity(P);W.U8(Now-LastSeen[P]<3);Request(Control(MessageKind::PeerStatus,W.Take()),Now);}
    }
    if (!Running && Core->Phase()==RoomPhase::MembershipChanging)
    {
        for (const auto& [P,M]:Core->Members()) if (P!=Player && M.Online && Now-LastSeen[P]>=10)
        {End("A peer disconnected during recovery; match ended without a result");return;}
    }
    if (Running)
    {
        std::set<Id> Missing;bool Suspect=false;std::size_t Online=0;
        for (const auto& [P,M]:Core->Members()) if (M.Online)
        {++Online;if (P!=Player) {Suspect|=Now-LastSeen[P]>=3;if (Leaving.contains(P) || Now-LastSeen[P]>=10) Missing.insert(P);}}
        for (const auto& [Pair,Report]:Reports)
            if (Core->Members().at(Pair.first).Online && Core->Members().at(Pair.second).Online && !Report.first && Now-Report.second<3) Suspect=true;
        if (Suspect && !SuspicionStarted) SuspicionStarted=Now;
        if (!Suspect) SuspicionStarted=0;
        SetPaused(Suspect);
        if (Suspect && Missing.empty() && Now-SuspicionStarted>10) {End("Network partition: peers disagree; match ended without a result");return;}
        if (!Missing.empty())
        {
            const std::size_t Remaining=Online-Missing.size();bool Agreed=Remaining>Online/2 || std::all_of(Missing.begin(),Missing.end(),[&](Id P){return Leaving.contains(P);});
            for (const auto P:Missing) for (const auto& [Other,M]:Core->Members())
                if (!Leaving.contains(P) && Other!=Player&&M.Online&&!Missing.contains(Other))
                {const auto Report=Reports.find({Other,P});Agreed&=Report!=Reports.end()&&!Report->second.first&&Now-Report->second.second<3;}
            if (!Agreed) {End("Network partition: match ended without a result");return;}
            if (Missing.contains(HostPlayer))
            {
                HostPlayer={};unsigned Best=255;
                for (const auto& [P,M]:Core->Members()) if (M.Online&&!Missing.contains(P)&&RoleSlotIndex(M.Faction,M.Seat)<Best)
                {HostPlayer=P;Best=RoleSlotIndex(M.Faction,M.Seat);}
            }
            if (Player==HostPlayer) for (const auto P:Missing)
            {
                const auto TeamValue=Core->Members().at(P).Faction;bool Teammate=false;
                for (const auto& [Other,M]:Core->Members()) if (M.Online&&!Missing.contains(Other)&&M.Faction==TeamValue) Teammate=true;
                if (!Teammate) {Writer W;W.Text(TeamValue==Team::Red?"Blue wins: red team disconnected":"Red wins: blue team disconnected");Broadcast(Control(MessageKind::MatchResult,W.Take()));Ended=true;return;}
                Core->BeginLeave(P,true,Now);Roster();
            }
        }
    }
    if (Player!=HostPlayer) return;
    const bool Ready=std::all_of(Core->Members().begin(),Core->Members().end(),[](const auto& E){return !E.second.Online||E.second.Ready;});
    if (Ready) Core->SetInitialRosterReady(true);
    if (Ready||Core->Phase()!=RoomPhase::Preparing) for (const auto& O:Core->Propose(Now))
    {
        Writer W;const auto& R=Core->Objects().at(O.Object);WriteAuthority(W,R.Grant);W.Text(R.Definition);W.U32(R.Ordinal);W.Key(R.Parent);
        W.Identity(O.ProposedOwner);W.U64(O.ProposedEpoch);Broadcast(Control(MessageKind::AssignmentOffer,W.Take(),O.Object));
    }
    Core->Poll(Now);Publish(Now);
}
}
