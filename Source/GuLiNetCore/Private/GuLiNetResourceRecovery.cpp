#include "GuLiNetClient.h"
#include "GuLiNetRoom.h"
#include "GuLiNetHash.h"
#include "GuLiNetTrace.h"
#include <algorithm>
#include <chrono>

namespace GuLiNet
{
namespace
{
// Periodic backup cadence only; initial copies and final leave snapshots remain immediate.
constexpr double ResourceSnapshotIntervalSeconds=5.0;
std::string KeyName(ObjectKey K)
{return std::to_string(unsigned(K.Kind))+"/"+K.StableId.ToString()+"/"+std::to_string(K.Generation);}
ResourceSnapshot CurrentSnapshot(const ObjectHandoffState& S,const ResourceGroupState& G,Revision Number,Tick Sample)
{
    ResourceSnapshot V;V.Match=S.Match;V.Group=G.Group;V.Owner=S.Source;V.OwnerVersion=G.OwnerVersion;
    V.RequestEpoch=G.RequestEpoch;V.Number=Number;V.Sample=Sample;
    V.CapturedUtcMicroseconds=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    V.Parts["i/identity"]=S.CurrentIdentity;
    for (const auto& P:S.Objects)
    {Writer W(MaxPrivateStateBytes);WriteObject(W,P.Record);W.U32(P.AdapterVersion);W.Blob(P.State);V.Parts["o/"+KeyName(P.Record.Grant.Object)]=W.Take();}
    for (const auto& P:S.Numeric)
    {Writer W(MaxPrivateStateBytes);WriteStateFragment(W,P);V.Parts["n/"+KeyName(P.Key.Object)+"/"+P.Key.Capability]=W.Take();}
    for (const auto& P:S.Systems)
    {Writer W(MaxPrivateStateBytes);W.Text(P.Name);W.Identity(P.Player);W.U32(P.AdapterVersion);W.U32(static_cast<std::uint32_t>(P.Objects.size()));for (const auto K:P.Objects) W.Key(K);W.Blob(P.State);V.Parts["s/"+P.Name]=W.Take();}
    V.Digest=ResourceSnapshotDigest(V);return V;
}
bool PrivateSnapshot(const ResourceSnapshot& V,Id Target,Revision Recovery,Revision Epoch,ObjectHandoffState& Out)
{
    ObjectHandoffState S;S.Match=V.Match;S.Source=V.Owner;S.Target=Target;S.Transfer=Recovery;
    S.CurrentOnly=true;S.ResourceGroup=V.Group;S.ResourceEpoch=Epoch;
    for (const auto& [Name,B]:V.Parts)
    {
        Reader R(B);
        if (Name.starts_with("o/"))
        {PrivateObjectState P;if (!ReadObject(R,P.Record)||!R.U32(P.AdapterVersion)||!R.Blob(P.State,MaxPrivateStateBytes)||!R.Done()
            ||Name!="o/"+KeyName(P.Record.Grant.Object)) return false;S.Objects.push_back(std::move(P));}
        else if (Name.starts_with("n/"))
        {StateFragment P;if (!ReadStateFragment(R,P)||!R.Done()||Name!="n/"+KeyName(P.Key.Object)+"/"+P.Key.Capability
            ||P.Key.Capability.starts_with("event/")) return false;S.Numeric.push_back(std::move(P));}
        else if (Name.starts_with("s/"))
        {
            PrivateSystemState P;std::uint32_t N=0;
            if (!R.Text(P.Name,128)||!R.Identity(P.Player)||!R.U32(P.AdapterVersion)||!R.U32(N)||N>4096||Name!="s/"+P.Name) return false;
            for (std::uint32_t I=0;I<N;++I) {ObjectKey K;if (!R.Key(K)) return false;P.Objects.push_back(K);}
            if (!R.Blob(P.State,MaxPrivateStateBytes)||!R.Done()) return false;S.Systems.push_back(std::move(P));
        }
        else if (Name=="i/identity") S.CurrentIdentity=B;
        else return false;
    }
    if (S.CurrentIdentity.empty()) return false;
    const auto Encoded=EncodeHandoffState(S);return !Encoded.empty()&&DecodeHandoffState(Encoded,Out);
}
}
Id ClientWorld::ObjectResourceGroup(ObjectKey K) const
{
    std::set<ObjectKey> Seen;
    while (K.Valid()&&Seen.size()<64&&Seen.insert(K).second)
    {
        const auto O=Directory.find(K);if (O==Directory.end()) break;
        if (O->second.Grant.OwnershipGroup.Valid()) return O->second.Grant.OwnershipGroup;
        if (O->second.Grant.Policy!=OwnershipPolicy::ParentWork) break;K=O->second.Parent;
    }
    return {};
}
std::vector<ObjectKey> ClientWorld::ResourceObjects(Id G,Id Owner) const
{
    std::vector<ObjectKey> Keys;
    for (const auto& [K,O]:Directory) if (ObjectResourceGroup(K)==G&&HasPrivateCustody(O.Grant,Owner)) Keys.push_back(K);
    return Keys;
}
bool ClientWorld::HasResourceRecovery() const
{return std::any_of(ResourceAuthorities.begin(),ResourceAuthorities.end(),[](const auto& P){return P.second.Recovery!=0;});}
std::map<Id,ResourceSnapshotStatus> ClientWorld::ResourceSnapshotStatuses(bool IncludeObjects) const
{
    std::map<Id,ResourceSnapshotStatus> Out;
    for (const auto& [K,G]:ResourceAuthorities)
    {
        auto& V=Out[K];if (const auto R=ResourceReplicas.find(K);R!=ResourceReplicas.end())
        {
            const auto& Evidence=R->second.Evidence;
            V.InstalledRecovery=Evidence.InstalledRecovery;V.InstalledSnapshot=Evidence.InstalledSnapshot;
            V.InstalledEpoch=Evidence.InstalledEpoch;V.InstalledSample=Evidence.InstalledSample;V.InstalledDigest=Evidence.InstalledDigest;
            if (IncludeObjects) V.InstalledObjects=Evidence.InstalledObjects;
            V.CompleteCopies=R->second.Store.Size();V.StoredBytes=R->second.Store.BytesHeld();
            V.QueuedVersions=R->second.Sending.has_value()+R->second.LatestPending.has_value();
        }
        V.Authority=G;
    }
    return Out;
}
void ClientWorld::StampPublication(Envelope& H) const
{
    if (H.ResourceGroup.Valid()||H.ResourceEpoch) return;
    const Envelope* Request=nullptr;
    if (const auto W=WorkFlows.find(H.Event);W!=WorkFlows.end()) Request=&W->second.Work.Request.Header;
    else if (const auto Known=KnownWork.find(H.Event);Known!=KnownWork.end()) Request=&Known->second.Work.Request.Header;
    if (Request) {H.ResourceGroup=Request->ResourceGroup;H.ResourceEpoch=Request->ResourceEpoch;return;}
    if (const auto* R=Numbers.FindResult(H.Event);R)
    {H.ResourceGroup=R->ResourceGroup;H.ResourceEpoch=R->ResourceEpoch;return;}
    if (const auto G=ResourceAuthorities.find(ObjectResourceGroup(H.Object));G!=ResourceAuthorities.end())
    {H.ResourceGroup=G->first;H.ResourceEpoch=G->second.RequestEpoch;}
}
bool ClientWorld::ResourceMessageCurrent(const Envelope& H) const
{
    if (!H.ResourceGroup.Valid()) return !H.ResourceEpoch&&!ResourceAuthorities.contains(ObjectResourceGroup(H.Object));
    const auto G=ResourceAuthorities.find(H.ResourceGroup);
    return G!=ResourceAuthorities.end()&&G->second.RequestEpoch==H.ResourceEpoch&&!G->second.Recovery;
}
bool ClientWorld::StampResourceRequest(TransactionRequest& R) const
{
    const auto Key=Numbers.ResourceGroupFor(R);const auto G=ResourceAuthorities.find(Key);
    if (G==ResourceAuthorities.end()) return !R.Header.ResourceGroup.Valid()&&!R.Header.ResourceEpoch;
    if (G->second.Recovery) return false;
    if (!R.Header.ResourceEpoch&&!R.Header.ResourceGroup.Valid())
    {R.Header.ResourceGroup=Key;R.Header.ResourceEpoch=G->second.RequestEpoch;}
    return R.Header.ResourceGroup==Key&&R.Header.ResourceEpoch==G->second.RequestEpoch;
}
Id ClientWorld::ContinuationEvent(const ObjectRecord& O) const
{
    FrozenWorkRecipe R;if (!DecodeFrozenWork(O,R)) return {};
    const auto G=ResourceAuthorities.find(Numbers.ResourceGroupFor(R.Request));
    if (G==ResourceAuthorities.end()||G->second.RequestEpoch==R.Request.Header.ResourceEpoch) return R.Request.Header.Event;
    return StableId("GuLiResourceContinuation/v1",O.Grant.Object.StableId.ToString()+":"+std::to_string(G->second.RequestEpoch));
}
void ClientWorld::InterruptResourceRequests(Id Group,Revision Epoch)
{
    const auto Start=std::chrono::steady_clock::now();
    const auto Affected=[&](const TransactionRequest& R){return R.Header.ResourceGroup==Group||Numbers.ResourceGroupFor(R)==Group;};
    std::map<Id,CommitResult> Notices;
    const auto Notice=[&](const TransactionRequest& R)->Id
    {
        CommitResult V;V.Event=R.Header.Event;V.Kind=R.Kind;V.Faction=R.Faction;V.Target=R.Target;V.StateDomain=R.Header.Object;
        V.ResourceGroup=Group;V.ResourceEpoch=Epoch;V.Status=CommitStatus::Interrupted;
        if (R.Kind==TransactionKind::AdmitWork)
        {Message M;TransactionRequest Original;if (DecodeMessage(R.Admission,M)&&DecodeTransaction(M.Header,M.Body,Original))
            {V.Event=Original.Header.Event;V.Kind=Original.Kind;V.Target=Original.Target;}}
        const auto E=V.Event;Notices.emplace(E,std::move(V));return E;
    };
    std::vector<std::pair<TransactionCallback,CommitResult>> Callbacks;
    for (auto I=Pending.begin();I!=Pending.end();)
        if (Affected(I->second.Request))
        {const auto E=Notice(I->second.Request);if (I->second.Callback) {auto V=Notices.at(E);V.Event=I->first;Callbacks.emplace_back(std::move(I->second.Callback),V);}I=Pending.erase(I);}else ++I;
    for (auto I=PlayerIntents.begin();I!=PlayerIntents.end();) if (Affected(I->second.Request)) {Notice(I->second.Request);I=PlayerIntents.erase(I);}else ++I;
    for (auto I=WorkFlows.begin();I!=WorkFlows.end();) if (Affected(I->second.Work.Request))
    {if (ReleaseApplicationResult) ReleaseApplicationResult(I->first);I=WorkFlows.erase(I);}else ++I;
    std::erase_if(KnownWork,[&](const auto& P){return Affected(P.second.Work.Request);});
    std::erase_if(RetryWork,[&](const auto& P){return Affected(P.second.second);});
    std::erase_if(WorkRetryRound,[&](Id E){return !RetryWork.contains(E);});
    std::erase_if(ReadyPeerWork,[&](Id E){return !RetryWork.contains(E);});
    std::erase_if(ReadyPeerWorkIdentities,[&](Id E){return !RetryWork.contains(E);});
    std::erase_if(WaitingResults,[&](const auto& P){return P.second.ResourceGroup==Group;});
    for (auto I=DeferredPeerReceipts.begin();I!=DeferredPeerReceipts.end();) if (I->second.Header.ResourceGroup==Group)
    {DeferredPeerReceiptBytes-=I->second.Bytes;I=DeferredPeerReceipts.erase(I);}else ++I;
    for (auto I=DeferredPeerStates.begin();I!=DeferredPeerStates.end();) if (I->Value.Header.ResourceGroup==Group)
    {DeferredPeerStateBytes-=I->Value.Body.size();I=DeferredPeerStates.erase(I);}else ++I;
    for (auto& [Peer,Q]:Publications) for (auto I=Q.begin();I!=Q.end();) if (I->Value.Header.ResourceGroup==Group)
    {PublicationBytes-=I->Bytes;PublicationIdentities.erase({Peer,I->Identity});I=Q.erase(I);}else ++I;
    Numbers.SealResourceEpoch(Group,Epoch);
    for (auto& [Fn,V]:Callbacks) Fn(V);
    for (const auto& [E,V]:Notices) if (OnCommit) OnCommit(V);
    HandoffSizes["resource/interrupted_requests"]+=Notices.size();
    HandoffSizes["resource/cleanup_microseconds"]+=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-Start).count();
}
void ClientWorld::SetResourceGroups(std::map<Id,ResourceGroupState> Groups)
{
    for (const auto& [Key,G]:Groups)
    {
        if (Key!=G.Group||!G.RequestEpoch) {Cancel("Invalid resource group metadata");return;}
        const auto Old=ResourceAuthorities.find(Key);
        if (Old!=ResourceAuthorities.end()&&G.RequestEpoch<Old->second.RequestEpoch) {Cancel("Resource request epoch moved backwards");return;}
        if (Old!=ResourceAuthorities.end()&&G.RequestEpoch>Old->second.RequestEpoch) InterruptResourceRequests(Key,G.RequestEpoch);
        else Numbers.SetResourceEpoch(Key,G.RequestEpoch);
        if (Old!=ResourceAuthorities.end()&&!G.Recovery
            &&(G.Backup!=Old->second.Backup||G.Owner!=Old->second.Owner||G.OwnerVersion!=Old->second.OwnerVersion))
        {
            auto& R=ResourceReplicas[Key];R.Acknowledged.reset();R.Sending.reset();R.LatestPending.reset();R.Sent=false;R.NextCapture=0;
        }
        if (G.Recovery)
        {
            SetMembershipPaused(true);
            for (const auto& [K,O]:Directory) if (ObjectResourceGroup(K)==Key) Frozen.insert(K);
            auto& Replica=ResourceReplicas[Key];
            if (Replica.Recovery!=G.Recovery)
            {
                Replica.Recovery=G.Recovery;Replica.Sending.reset();Replica.LatestPending.reset();Replica.Acknowledged.reset();
                Replica.Sent=false;Replica.InstallSent.clear();Replica.Installing.reset();Replica.InstalledAck=false;
            }
        }
        else if (Old!=ResourceAuthorities.end()&&Old->second.Recovery)
        {
            if (ResourceReplicas[Key].InstalledRecovery!=Old->second.Recovery)
            {Cancel("Resource ownership committed before complete snapshot installation");return;}
            for (const auto& [K,O]:Directory) if (ObjectResourceGroup(K)==Key) Frozen.erase(K);
            auto& R=ResourceReplicas[Key];R.NextCapture=0;R.Acknowledged.reset();R.Sending.reset();R.LatestPending.reset();
            R.NextNumber=std::max(R.NextNumber,R.Evidence.InstalledSnapshot+1);
            // All peers already installed this exact cut. The selected backup
            // can retain it under the committed grant immediately, even if the
            // new simulator disconnects before its first periodic publication.
            if (G.Backup==Player&&R.Installing)
            {
                auto S=*R.Installing;S.Source=G.Owner;S.Target=Player;S.Transfer=0;
                for (auto& O:S.Objects) O.Record.Grant=Directory.at(O.Record.Grant.Object).Grant;
                for (auto& F:S.Numeric) if (F.Key.Capability=="object")
                {Reader Read(F.Value);ObjectRecord O;if (!ReadObject(Read,O)||!Read.Done()) {Cancel("Invalid installed resource object");return;}
                    O.Grant=Directory.at(F.Key.Object).Grant;Writer Write;WriteObject(Write,O);F.Value=Write.Take();}
                for (auto& P:S.Systems) P.Player=G.Owner;
                auto V=CurrentSnapshot(S,G,R.Evidence.InstalledSnapshot,R.Evidence.InstalledSample);
                if (const auto Last=R.Store.Latest()) {V.CapturedUtcMicroseconds=Last->CapturedUtcMicroseconds;V.Digest=ResourceSnapshotDigest(V);}
                if (R.Store.Receive(DiffResourceSnapshot(V,nullptr),NetworkTime)!=ResourceSnapshotStore::Result::Installed)
                {Cancel("Unable to retain the committed resource snapshot on its backup");return;}
            }
            else R.Store.Clear();
            R.Installing.reset();R.InstallIdentity.clear();R.InstallSent.clear();
        }
    }
    ResourceAuthorities=std::move(Groups);
    for (const auto& [K,A]:Adapters) if (ObjectResourceGroup(K).Valid()) A->SetAuthority(Directory.at(K).Grant,HasSimulation(K));
}
bool ClientWorld::ReceiveResourceSnapshot(Id Sender,const Message& M)
{
    const auto G=ResourceAuthorities.find(M.Header.ResourceGroup);if (G==ResourceAuthorities.end()) return false;
    const auto& Grant=G->second;auto& Replica=ResourceReplicas[G->first];
    if (M.Header.Kind==MessageKind::ResourceSnapshotStored)
    {
        Reader R(M.Body);Revision Number;std::string Digest;std::uint8_t Good;
        if (Sender!=Grant.Backup||Grant.Owner!=Player||!R.U64(Number)||!R.Text(Digest,64)||!R.U8(Good)||Good>1||!R.Done()) return false;
        if (!Replica.Sending||Replica.Sending->Number!=Number||Replica.Sending->Digest!=Digest) return true;
        if (Good) {Replica.Acknowledged=std::move(Replica.Sending);++HandoffSizes["resource/backup_complete"];}
        else {Replica.Acknowledged.reset();Replica.Sent=false;++HandoffSizes["resource/backup_baseline_reset"];return true;}
        Replica.Sending.reset();Replica.Sent=false;return true;
    }
    const bool Installing=M.Header.Kind==MessageKind::ResourceSnapshotInstall;
    if (Installing?(!Grant.Recovery||Sender!=Grant.Publisher):
        (Grant.Recovery||Sender!=Grant.Owner||Player!=Grant.Backup||Sender==Player)) return false;
    ResourceSnapshotDelta Delta;
    if (!DecodeResourceSnapshotDelta(M.Body,Delta)||Delta.Value.Match!=Match||Delta.Value.Group!=Grant.Group
        ||Delta.Value.Owner!=(Installing?Grant.PreviousOwner:Grant.Owner)||Delta.Value.OwnerVersion!=Grant.OwnerVersion
        ||(Installing?(Grant.Abrupt?Delta.Value.RequestEpoch+1!=Grant.RequestEpoch:Delta.Value.RequestEpoch!=Grant.RequestEpoch)
            :Delta.Value.RequestEpoch!=Grant.RequestEpoch)) return false;
    const auto Result=Replica.Store.Receive(Delta,NetworkTime);
    const bool Good=Result==ResourceSnapshotStore::Result::Installed||Result==ResourceSnapshotStore::Result::Duplicate;
    if (!Installing)
    {
        Writer W;W.U64(Delta.Value.Number);W.Text(Delta.Value.Digest);W.U8(Good);
        Message Ack;Ack.Header={MessageKind::ResourceSnapshotStored,Match,Epoch(),Player};Ack.Header.ResourceGroup=Grant.Group;
        Ack.Header.ResourceEpoch=Grant.RequestEpoch;Ack.Header.Sequence=Sequence++;Ack.Body=W.Take();Transmit(Sender,std::move(Ack));
        return Good||Result==ResourceSnapshotStore::Result::MissingBaseline;
    }
    if (!Good) {Cancel("Resource recovery snapshot failed completeness or integrity validation");return false;}
    if (Replica.InstalledRecovery==Grant.Recovery) return true;
    if (!Replica.Installing)
    {
        ObjectHandoffState S;
        if (!PrivateSnapshot(*Replica.Store.Latest(),Player,Player==Grant.Target?Grant.Recovery:0,Grant.RequestEpoch,S)) return false;
        // The complete immutable cut already passed its canonical digest check.
        // Scope that identity to this recovery/epoch once; polling a retained
        // application must not repeatedly encode and hash the full numeric cut.
        Replica.InstallIdentity="resource/"+std::to_string(Grant.Recovery)+"/"+std::to_string(Grant.RequestEpoch)+"/"+Replica.Store.Latest()->Digest;
        Replica.Installing=std::move(S);Replica.InstallStarted=NetworkTime;
    }
    return true;
}
void ClientWorld::PollResourceSnapshots()
{
    TraceScope Scope("GuLiHybrid_ResourceSnapshotMaintenance");
    if (!Failure.empty()) return;
    for (const auto& [Key,G]:ResourceAuthorities)
    {
        auto& R=ResourceReplicas[Key];
        const auto SendSnapshot=[&](Id Peer,MessageKind Kind,const ResourceSnapshotDelta& Delta)
        {
            if (Peer!=Player&&HandoffAdmission&&!HandoffAdmission(Peer)) return false;
            Message M;M.Header={Kind,Match,Epoch(),Player};M.Header.ResourceGroup=Key;M.Header.ResourceEpoch=G.RequestEpoch;
            M.Header.Sequence=Sequence++;M.Body=EncodeResourceSnapshotDelta(Delta);if (M.Body.empty()) return false;
            const auto Bytes=M.Body.size();const bool Good=Peer==Player?ReceiveResourceSnapshot(Player,M):Transmit(Peer,std::move(M));
            if (Good) {HandoffSizes["resource/transmitted_bytes"]+=Bytes;++HandoffSizes["resource/transmitted_versions"];}
            return Good;
        };
        if (G.Recovery)
        {
            if (G.Publisher==Player&&!R.Sending)
            {
                if (G.Abrupt)
                {
                    const auto Last=R.Store.Latest();
                    if (!Last) {Cancel("Resource owner disconnected without a complete backup snapshot");return;}
                    R.Sending=*Last;
                    HandoffSizes["resource/backup_age_since_completion_microseconds"]=static_cast<std::uint64_t>(std::max(0.,NetworkTime-R.Store.CompletedAt())*1000000);
                    const auto Utc=static_cast<std::uint64_t>(std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
                    HandoffSizes["resource/backup_age_from_capture_microseconds"]=Utc>Last->CapturedUtcMicroseconds?Utc-Last->CapturedUtcMicroseconds:0;
                    HandoffSizes["resource/recovered_sample"]=Last->Sample;
                }
                else
                {
                    const auto Keys=ResourceObjects(Key,Player);ObjectHandoffState S;
                    if (!CaptureObjects(Keys,G.Target,0,S,true)) {if (!LastCapturePending()) Cancel("Unable to capture final resource state");continue;}
                    R.Sending=CurrentSnapshot(S,G,R.NextNumber++,Now());
                    if (R.Sending->Digest.empty()) {Cancel("Current resource snapshot capacity exceeded");return;}
                }
            }
            if (G.Publisher==Player&&R.Sending)
            {
                const auto Delta=DiffResourceSnapshot(*R.Sending,nullptr);
                for (const auto& [Peer,Member]:Members) if (Member.Online&&!R.InstallSent.contains(Peer))
                    if (SendSnapshot(Peer,MessageKind::ResourceSnapshotInstall,Delta)) R.InstallSent.insert(Peer);
            }
            if (R.Installing&&R.InstalledRecovery!=G.Recovery)
            {
                if (!ImportObjectsImpl(*R.Installing,false,&R.InstallIdentity)) {if (!LastApplicationPending()) Cancel("Complete resource snapshot could not be installed");continue;}
                R.InstalledRecovery=G.Recovery;
                const auto* Last=R.Store.Latest();R.Evidence.InstalledRecovery=G.Recovery;
                R.Evidence.InstalledSnapshot=Last->Number;R.Evidence.InstalledDigest=Last->Digest;
                R.Evidence.InstalledEpoch=Last->RequestEpoch;R.Evidence.InstalledSample=Last->Sample;R.Evidence.InstalledObjects.clear();
                for (const auto& O:R.Installing->Objects) R.Evidence.InstalledObjects.push_back(O.Record.Grant.Object);
                HandoffSizes["resource/install_microseconds"]+=static_cast<std::uint64_t>((NetworkTime-R.InstallStarted)*1000000);
            }
            if (R.InstalledRecovery==G.Recovery&&!R.InstalledAck)
            {
                const auto* Last=R.Store.Latest();if (!Last||!R.Installing) {Cancel("Missing installed resource snapshot identity");return;}
                Writer W;W.Identity(Key);W.U64(G.Recovery);W.U64(Last->Number);W.Text(Last->Digest);
                W.U32(static_cast<std::uint32_t>(R.Installing->Objects.size()));
                for (const auto& P:R.Installing->Objects) {WriteAuthority(W,P.Record.Grant);W.Text(P.Record.Definition);W.U32(P.Record.Ordinal);W.Key(P.Record.Parent);}
                R.InstalledAck=SendMessage({},MessageKind::ResourceSnapshotInstalled,{}, {},W.Take());
            }
            continue;
        }
        if (G.Owner!=Player||!G.Backup.Valid()||MembershipPaused) continue;
        if (NetworkTime>=R.NextCapture)
        {
            const auto Keys=ResourceObjects(Key,Player);ObjectHandoffState S;
            if (!Keys.empty()&&CaptureObjects(Keys,G.Backup,0,S,true))
            {
                auto Snapshot=CurrentSnapshot(S,G,R.NextNumber++,Now());
                if (Snapshot.Digest.empty()) {Cancel("Resource backup snapshot capacity exceeded");return;}
                R.LatestPending=std::move(Snapshot);R.NextCapture=NetworkTime+ResourceSnapshotIntervalSeconds;++HandoffSizes["resource/captured_versions"];
            }
        }
        if (!R.Sending&&R.LatestPending) {R.Sending=std::move(R.LatestPending);R.LatestPending.reset();R.Sent=false;}
        if (R.Sending&&!R.Sent)
        {
            const auto Delta=DiffResourceSnapshot(*R.Sending,R.Acknowledged?&*R.Acknowledged:nullptr);
            if (SendSnapshot(G.Backup,MessageKind::ResourceSnapshot,Delta)) {R.Sent=true;R.SentAt=NetworkTime;}
        }
        HandoffSizes["resource/queued_versions"]=std::size_t(R.Sending.has_value())+std::size_t(R.LatestPending.has_value());
        HandoffSizes["resource/backup_bytes"]=R.Store.BytesHeld();
    }
}

std::map<Id,ResourceGroupState> Room::ResourceGroups() const
{
    std::map<Id,ResourceGroupState> Out;
    for (const auto& [Key,G]:Groups)
    {
        if (const auto R=ResourceRecoveries.find(Key);R!=ResourceRecoveries.end()) {Out.emplace(Key,R->second.State);continue;}
        ResourceGroupState V;V.Group=Key;V.Owner=G.Owner;V.Backup=G.Backup;V.OwnerVersion=G.Version;V.RequestEpoch=G.RequestEpoch;Out.emplace(Key,V);
    }
    return Out;
}
void Room::RefreshResourceBackups()
{
    const auto Leaving=MembershipValue&&MembershipValue->Operation==MembershipOperation::Leave?MembershipValue->Player:Id{};
    for (auto& [Key,G]:Groups) if (!ResourceRecoveries.contains(Key))
    {
        if (G.Backup!=G.Owner&&G.Backup!=Leaving&&Live(G.Backup)) continue;
        G.Backup={};for (const auto& [P,M]:Players) if (P!=G.Owner&&P!=Leaving&&Live(P)) {G.Backup=P;break;}
    }
}
bool Room::InResourceRecovery(ObjectKey K) const
{return ResourceRecoveries.contains(ResourceGroupOf(K));}
Id Room::ResourceGroupOf(ObjectKey K) const
{
    std::set<ObjectKey> Seen;
    while (K.Valid()&&Seen.size()<64&&Seen.insert(K).second)
    {const auto O=Directory.find(K);if (O==Directory.end()) return {};if (O->second.Grant.OwnershipGroup.Valid()) return O->second.Grant.OwnershipGroup;
        if (O->second.Grant.Policy!=OwnershipPolicy::ParentWork) return {};K=O->second.Parent;}
    return {};
}
bool Room::ResourceSnapshotInstalled(Id Player,Id Group,Revision Recovery,Revision Number,const std::string& Digest,
    std::span<const ObjectRegistration> Objects,double Now)
{
    const auto I=ResourceRecoveries.find(Group);
    if (I==ResourceRecoveries.end()||I->second.State.Recovery!=Recovery||!Live(Player)||!Number||Digest.size()!=64||Objects.empty()
        ||Now>=I->second.State.Deadline) return false;
    auto& V=I->second;
    if (V.Number&&(V.Number!=Number||V.Digest!=Digest)) {Abort("Resource clients installed different snapshots");return false;}
    std::set<ObjectKey> Seen;
    for (const auto& O:Objects)
    {
        const auto Old=Directory.find(O.Grant.Object);
        if (!Seen.insert(O.Grant.Object).second||Old==Directory.end()||Old->second.Definition!=O.Definition
            ||ResourceGroupOf(O.Grant.Object)!=Group||O.Grant.SimOwner!=V.State.PreviousOwner
            ||Old->second.Ordinal!=O.Ordinal||Old->second.Parent!=O.Parent) return false;
    }
    if (!V.Number) {V.Number=Number;V.Digest=Digest;V.Objects.assign(Objects.begin(),Objects.end());}
    else
    {
        Writer A,B;for (const auto& O:V.Objects) {WriteAuthority(A,O.Grant);A.Text(O.Definition);A.U32(O.Ordinal);A.Key(O.Parent);}
        for (const auto& O:Objects) {WriteAuthority(B,O.Grant);B.Text(O.Definition);B.U32(O.Ordinal);B.Key(O.Parent);}
        if (A.Take()!=B.Take()) {Abort("Resource snapshot acknowledgements disagree on object identities");return false;}
    }
    V.Waiting.erase(Player);if (!V.Waiting.empty()) return true;
    auto& G=Groups.at(Group);if (G.Version==UINT64_MAX) {Abort("Resource group owner version exhausted");return false;}
    for (const auto& [K,O]:Directory) if (ResourceGroupOf(K)==Group&&O.Grant.OwnerEpoch==UINT64_MAX)
    {Abort("Resource object owner version exhausted");return false;}
    ++G.Version;G.Owner=V.State.Target;
    std::map<ObjectKey,ObjectRegistration> Cut;for (const auto& O:V.Objects) Cut.emplace(O.Grant.Object,O);
    for (auto& [K,O]:Directory) if (ResourceGroupOf(K)==Group)
    {
        const auto P=Cut.find(K);O.Grant.State=P==Cut.end()?Life::Destroyed:P->second.Grant.State;
        O.Grant.SimOwner=G.Owner;O.Grant.StateCustodian={};++O.Grant.OwnerEpoch;
        if (O.Grant.OwnershipGroup==Group) O.Grant.GroupEpoch=G.Version;
        MarkDirectoryChanged(K);Scheduler.SetObject({O.Grant,O.Grant.Policy,O.Parent},Now);
    }
    ResourceRecoveries.erase(I);RefreshResourceBackups();return true;
}
}
