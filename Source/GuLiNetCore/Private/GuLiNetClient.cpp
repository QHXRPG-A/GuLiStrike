#include "GuLiNetClient.h"
#include "GuLiNetCommandSubset.h"
#include "GuLiNetManifest.h"
#include "GuLiNetHash.h"
#include "GuLiNetTrace.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace GuLiNet
{
ClientWorld::ClientWorld(Id M,Id P,Revision E,Factory F,Send S)
    :Match(M),Player(P),Create(std::move(F)),Transmit(std::move(S)),Numbers(M,P,E)
{
    Clock.Start(E);if (!M.Valid() || !P.Valid() || !E || !Create || !Transmit) Cancel("Invalid client world");
}
ClientWorld::~ClientWorld() {for (auto& [K,A]:Adapters) A->Release();}
bool ClientWorld::Configure(const RuntimeManifest& M,std::string& Error)
{
    if (!Numbers.Configure(M,Error)) return false;
    for (const auto& [K,O]:Numbers.Objects()) StoreRecord(O);return true;
}
void ClientWorld::StoreRecord(const ObjectRecord& O)
{
    const auto K=O.Grant.Object;const auto Old=Directory.find(K);
    if (Old!=Directory.end() && Old->second==O) return;
    if (Old==Directory.end() || Old->second.Grant!=O.Grant) DirectoryHashDirty=true;
    if (Old!=Directory.end())
    {
        const auto& Previous=Old->second;
        if (Previous.Parent.Valid() && Previous.Grant.Policy==OwnershipPolicy::ParentWork && Previous.Grant.State<Life::Dead
            && (Previous.Parent!=O.Parent || O.Grant.Policy!=OwnershipPolicy::ParentWork || O.Grant.State>=Life::Dead))
        {
            const auto Children=LiveParentWork.find(Previous.Parent);
            if (Children!=LiveParentWork.end()) {Children->second.erase(K);if (Children->second.empty()) LiveParentWork.erase(Children);}
        }
        if (Previous.Grant.Faction!=O.Grant.Faction || Previous.Grant.State==Life::Destroyed || O.Grant.State==Life::Destroyed
            || Previous.Capabilities.size()!=O.Capabilities.size())
            for (const auto& [Name,Value]:Previous.Capabilities)
            {
                const auto I=CapabilityObjects.find({Previous.Grant.Faction,Name});
                if (I!=CapabilityObjects.end()) {I->second.erase(K);if (I->second.empty()) CapabilityObjects.erase(I);}
            }
        else for (const auto& [Name,Value]:Previous.Capabilities) if (!O.Capabilities.contains(Name))
        {
            const auto I=CapabilityObjects.find({Previous.Grant.Faction,Name});
            if (I!=CapabilityObjects.end()) {I->second.erase(K);if (I->second.empty()) CapabilityObjects.erase(I);}
        }
        if (Previous.Grant.Policy==OwnershipPolicy::CommanderActivity)
        {const auto I=CommanderDefinitionCounts.find(Previous.Definition);if (I!=CommanderDefinitionCounts.end() && !--I->second) CommanderDefinitionCounts.erase(I);}
    }
    Directory[K]=O;
    if (O.Parent.Valid() && O.Grant.Policy==OwnershipPolicy::ParentWork && O.Grant.State<Life::Dead) LiveParentWork[O.Parent].insert(K);
    if (O.Grant.State!=Life::Destroyed) for (const auto& [Name,Value]:O.Capabilities) CapabilityObjects[{O.Grant.Faction,Name}].insert(K);
    if (O.Grant.Policy==OwnershipPolicy::CommanderActivity) ++CommanderDefinitionCounts[O.Definition];
    if (O.Grant.State==Life::Alive) SimulationCandidates.insert(K);else SimulationCandidates.erase(K);
    if (OnRecord) OnRecord(O);
}
const ObjectRecord* ClientWorld::FindTeamCapability(Team Faction,const std::string& Capability) const
{
    const auto I=CapabilityObjects.find({Faction,Capability});if (I==CapabilityObjects.end()) return nullptr;
    for (const auto& Key:I->second) if (const auto O=Directory.find(Key);O!=Directory.end() && O->second.Grant.State!=Life::Destroyed) return &O->second;
    return nullptr;
}
const std::string& ClientWorld::DirectoryHash() const
{
    if (DirectoryHashDirty)
    {
        // This local digest includes the complete retained directory, not a
        // wire message. 100000 fixed-size authorities fit the existing 16 MiB
        // bound. Never turn a Writer overflow into a valid empty-input hash.
        Writer W(MaxPrivateStateBytes);
        for (const auto& [K,O]:Directory) WriteAuthority(W,O.Grant);
        CachedDirectoryHash=W.Good()?Sha256(W.Take()):std::string{};
        DirectoryHashDirty=false;
    }
    return CachedDirectoryHash;
}
void ClientWorld::SetMembers(std::map<Id,Member> Values) {Members=std::move(Values);Numbers.SetMembers(Members);}
bool ClientWorld::RegisterSystem(std::string Name,std::unique_ptr<ISharedStateAdapter> Adapter)
{
    if (Name.empty() || !Adapter || !Adapter->Version() || Systems.contains(Name)) return false;
    Systems.emplace(std::move(Name),std::move(Adapter));return true;
}
bool ClientWorld::PrepareAdapterRecord(const ObjectRecord& R,const PrivateObjectState* Private)
{
    const auto K=R.Grant.Object;if (!Failure.empty() || !K.Valid()) return false;
    const auto Old=Directory.find(K);
    if (Old!=Directory.end() && (R.Grant.OwnerEpoch<Old->second.Grant.OwnerEpoch || R.Grant.ControlEpoch<Old->second.Grant.ControlEpoch
        || R.Definition!=Old->second.Definition || R.Ordinal!=Old->second.Ordinal)) return false;
    if (R.Materialized && !Adapters.contains(K))
    {
        auto A=Create(R);if (!A || !A->Version()) return false;
        for (const auto& Name:A->RequiredSystems()) if (!Systems.contains(Name)) {A->Release();return false;}
        if (Private && (Private->Record.Grant.Object!=K || Private->AdapterVersion!=A->Version())) {A->Release();return false;}
        if (!(Private?A->PrepareRestore(R,Private->State):A->Prepare(R)))
        {
            if (!Private && A->IsPreparationPending()) PreparingApplications[K]=NetworkTime+.1;
            else {A->Release();return false;}
        }
        Adapters.emplace(K,std::move(A));
    }
    return true;
}
bool ClientWorld::Install(ObjectRecord R,bool Commit)
{
    if (!PrepareAdapterRecord(R)) return false;const auto K=R.Grant.Object;
    if (!Commit) return true;
    if (!Numbers.Install(R)) return false;
    StoreRecord(Numbers.Objects().at(K));
    if (const auto A=Adapters.find(K);A!=Adapters.end())
    {A->second->SetAuthority(R.Grant,HasSimulation(K));A->second->ApplyLedger(Directory.at(K),Numbers.Version({K,"object"}),false);}
    return true;
}
bool ClientWorld::InstallGrant(const Authority& Grant,std::uint32_t Ordinal)
{
    Numbers.ObserveTerminalAuthority(Grant);
    Numbers.ObserveObjectOrdinal(Grant.Object.Kind,Ordinal);
    const auto I=Directory.find(Grant.Object);if (I==Directory.end()) {PendingGrants[Grant.Object]=Grant;return true;}
    const bool Changed=I->second.Grant.OwnerEpoch!=Grant.OwnerEpoch;
    if (!Numbers.SetAuthority(Grant)) return false;
    StoreRecord(Numbers.Objects().at(Grant.Object));
    if (Changed)
    {
        MotionApplied.erase(Grant.Object);PublicStateApplied.erase(Grant.Object);
        PendingMotion.erase({Grant.Object,MessageKind::Motion});PendingMotion.erase({Grant.Object,MessageKind::ComponentState});
    }
    if (const auto A=Adapters.find(Grant.Object);A!=Adapters.end()) A->second->SetAuthority(I->second.Grant,HasSimulation(Grant.Object));
    return true;
}
bool ClientWorld::SetBootstrapMaterializationDeferred(bool Value)
{
    if (!Value && !PreparingApplications.empty()) return false;
    BootstrapMaterializationDeferred=Value;if (Value) return true;
    for (const auto& K:DeferredMaterialization)
    {
        const auto O=Numbers.Objects().find(K);if (O==Numbers.Objects().end()) continue;
        // StageBootstrapDirectory has prepared historical provenance through
        // PrepareRestore. Reapply the current ledger, then resume strict mode.
        if (!Install(O->second,true))
        {Cancel("Unable to materialize restored object: "+O->second.Definition+" ordinal="+std::to_string(O->second.Ordinal));return false;}
    }
    DeferredMaterialization.clear();return true;
}
bool ClientWorld::HasSimulation(ObjectKey K,bool Dead) const
{
    if (const auto G=ResourceAuthorities.find(ObjectResourceGroup(K));G!=ResourceAuthorities.end()&&G->second.Recovery) return false;
    const auto I=Directory.find(K);
    if (!Failure.empty() || I==Directory.end() || Frozen.contains(K) || PreparingApplications.contains(K) || CommittingOwnershipGroup) return false;const auto& A=I->second.Grant;
    return !A.Dormant && A.SimOwner==Player && A.OwnerEpoch && (A.Policy!=OwnershipPolicy::ControllerPinned || A.Controller==Player)
        && (A.State==Life::Alive || (Dead && A.State==Life::Dead));
}
bool ClientWorld::HasControl(ObjectKey K) const
{
    const auto I=Directory.find(K);return I!=Directory.end() && I->second.Grant.Controller==Player
        && I->second.Grant.ControlEpoch && I->second.Grant.State==Life::Alive && !I->second.Grant.Dormant;
}
bool ClientWorld::AwaitingApplications(const CommitResult& R) const
{
    for (const auto& O:R.Births) if (PreparingApplications.contains(O.Grant.Object)) return true;
    for (const auto& O:R.Objects) if (PreparingApplications.contains(O.Grant.Object)) return true;
    return R.Target.Valid() && PreparingApplications.contains(R.Target);
}
void ClientWorld::CompleteApplication(ObjectKey Key,bool Ready,std::uint32_t Version,const std::vector<std::string>& Required)
{
    const auto A=Adapters.find(Key);if (A==Adapters.end() || !PreparingApplications.contains(Key)) return;
    A->second->PreparationCompleted(Ready,Version,Required);
    if (!Ready)
    {
        if (!BootstrapMaterializationDeferred) Cancel("Committed object application failed: "+Key.StableId.ToString());
        PreparingApplications[Key]=NetworkTime+.1;return;
    }
    for (const auto& Name:Required) if (!Systems.contains(Name)) {Cancel("Prepared object requires missing shared state: "+Name);return;}
    PreparingApplications.erase(Key);
    if (const auto O=Directory.find(Key);O!=Directory.end())
    {A->second->SetAuthority(O->second.Grant,HasSimulation(Key));A->second->ApplyLedger(O->second,Numbers.Version({Key,"object"}),false);}
    if (const auto I=PendingHandoffReadiness.find(Key);I!=PendingHandoffReadiness.end())
    {const auto Transfer=I->second;PendingHandoffReadiness.erase(I);if (Transfers.contains(Key)&&Transfers.at(Key).IdValue==Transfer.IdValue) PrepareHandoff(Transfer);}
}
void ClientWorld::CompleteMotionApplication(ObjectKey Key,MessageKind Kind,Tick At,Revision OwnerVersion,bool Good)
{
    const auto O=Directory.find(Key);
    if (O==Directory.end() || O->second.Grant.OwnerEpoch!=OwnerVersion) return;
    if (Kind==MessageKind::ObjectEvent)
    {if (Good) {auto& Age=O->second.LogicalAge;Age=std::max(Age,At);Numbers.SetLogicalAge(Key,Age);}return;}
    auto& S=MotionStats[Key];
    const auto PendingApply=PendingMotion.find({Key,Kind});if (PendingApply!=PendingMotion.end() && PendingApply->second<=At) PendingMotion.erase(PendingApply);
    if (!Good) {++S.InvalidState;return;}
    auto& Applied=Kind==MessageKind::Motion?MotionApplied:PublicStateApplied;
    if (const auto Last=Applied.find(Key);Last!=Applied.end() && Last->second>=At) return;
    Applied[Key]=At;auto& Age=O->second.LogicalAge;Age=std::max(Age,At);Numbers.SetLogicalAge(Key,Age);
    ++S.Applied;S.LastApplied=At;
}
Tick ClientWorld::ObjectNow(ObjectKey K) const
{const auto O=Directory.find(K);return O==Directory.end()?0:O->second.LogicalAge;}
bool ClientWorld::ValidatePeer(Id Sender,const Envelope& H,Domain Field,bool Dead) const
{
    if (H.Match!=Match || H.Epoch!=Epoch() || H.Sender!=Sender) return false;
    const auto M=Members.find(Sender);const auto I=Directory.find(H.Object);
    if (M==Members.end() || !M->second.Online || I==Directory.end()) return false;
    const auto& A=I->second.Grant;
    // Room lifecycle metadata can overtake the owner's ordered P2P events.
    // Final events still belong to this exact owner version and life identity;
    // retirement must not discard the notification that closes the effect.
    const bool FinalEvent=H.Kind==MessageKind::ObjectEvent && A.State==Life::Destroyed;
    if (A.State!=Life::Alive && !(Dead && (A.State==Life::Dead || FinalEvent))) return false;
    return Field==Domain::Command?A.Controller==Sender && A.ControlEpoch==H.ControlEpoch
        :(A.Dormant?A.StateCustodian:A.SimOwner)==Sender && A.OwnerEpoch==H.OwnerEpoch;
}
bool ClientWorld::SendMessage(Id Peer,MessageKind Kind,Id Event,ObjectKey K,Bytes Body)
{
    Envelope H;H.Kind=Kind;H.Match=Match;H.Epoch=Epoch();H.Sender=Player;H.Event=Event;H.Object=K;H.At=ObjectNow(K);H.Sequence=Sequence++;
    if (const auto O=Directory.find(K);O!=Directory.end()) {H.OwnerEpoch=O->second.Grant.OwnerEpoch;H.ControlEpoch=O->second.Grant.ControlEpoch;}
    StampPublication(H);return Transmit(Peer,{H,std::move(Body)});
}
bool ClientWorld::Broadcast(MessageKind Kind,Id Event,ObjectKey K,const Bytes& Body)
{
    for (const auto& [Peer,M]:Members) if (Peer!=Player && M.Online && M.Ready)
    {
        Envelope H;H.Kind=Kind;H.Match=Match;H.Epoch=Epoch();H.Sender=Player;H.Event=Event;H.Object=K;H.At=ObjectNow(K);H.Sequence=Sequence++;
        if (const auto O=Directory.find(K);O!=Directory.end()) {H.OwnerEpoch=O->second.Grant.OwnerEpoch;H.ControlEpoch=O->second.Grant.ControlEpoch;}
        StampPublication(H);Message V{H,Body};auto& Queue=Publications[Peer];
        if (Queue.empty() && Transmit(Peer,V)) continue;
        // A numeric commit has already happened. Transport backpressure cannot
        // discard its state or receipt. Keep publications in causal order until
        // the peer channel accepts them; ordinary simulation continues.
        auto IdentityHeader=H;IdentityHeader.Sequence=0;IdentityHeader.At=0;
        Bytes Wire;if (!EncodeMessage({IdentityHeader,Body},Wire)) return false;
        const auto Identity=Sha256(Wire);
        if (!PublicationIdentities.emplace(Peer,Identity).second) continue;
        const auto Size=Wire.size();PublicationBytes+=Size;Queue.push_back({std::move(V),Identity,Size});
    }
    return true;
}
void ClientWorld::FlushPublications()
{
    TraceScope Scope("GuLiHybrid_CoreFlushPublications");
    for (auto I=Publications.begin();I!=Publications.end();)
    {
        const auto M=Members.find(I->first);const bool Online=M!=Members.end() && M->second.Online;
        auto& Queue=I->second;
        while (!Queue.empty())
        {
            const auto& P=Queue.front();if (Online && !Transmit(I->first,P.Value)) break;
            PublicationBytes-=P.Bytes;PublicationIdentities.erase({I->first,P.Identity});Queue.pop_front();
        }
        if (Queue.empty()) I=Publications.erase(I);else ++I;
    }
}
bool ClientWorld::PublishObjectEvent(ObjectKey K,Id Event,Bytes Body)
{
    return Event.Valid() && !Body.empty() && CanPublishFacts() && HasSimulation(K)
        && Broadcast(MessageKind::ObjectEvent,Event,K,Body);
}
bool ClientWorld::PublishDirectoryLife(ObjectKey K,Revision Version)
{
    const auto O=Directory.find(K);if (O==Directory.end() || O->second.Grant.SimOwner!=Player || O->second.Grant.OwnerEpoch!=Version) return false;
    Writer W;W.U8(std::uint8_t(O->second.Grant.State));return SendMessage({},MessageKind::ObjectEvent,{},K,W.Take());
}
bool ClientWorld::ApplyCommand(Id Sender,const Envelope& H,const FrozenCommand& C)
{
    if (CommandTiming) CommandTiming("core-apply",C.Event,C.Members.size());
    ApplicationPending=false;
    if (H.Match!=Match || H.Epoch!=Epoch() || H.Sender!=Sender || C.Event!=H.Event || !C.Event.Valid()) return false;
    if (PrepareCommands)
    {
        const auto Rejected=[&]{if (CancelCommands) CancelCommands(C);return false;};
        std::vector<CommandMember> Eligible;
        for (const auto& M:C.Members)
        {
            const auto O=Directory.find(M.Object);if (O==Directory.end()) return Rejected();const auto& A=O->second.Grant;
            if (A.Controller==Sender && A.ControlEpoch==M.ControlEpoch && A.SimOwner!=Player)
            {
                const auto T=CompletedTransfers.find(M.Object);
                if (T!=CompletedTransfers.end() && T->second.Source==Player && T->second.PreviousOwnerVersion==M.OwnerEpoch
                    && T->second.NextOwnerVersion==A.OwnerEpoch && T->second.Target==A.SimOwner) continue;
            }
            if (A.Controller!=Sender||A.ControlEpoch!=M.ControlEpoch||A.OwnerEpoch!=M.OwnerEpoch||A.SimOwner!=Player) return Rejected();
            if (!Frozen.contains(M.Object) && !PreparingApplications.contains(M.Object) && !Commands.contains({C.Event,M.Object})
                && A.State==Life::Alive && !A.Dormant && Adapters.contains(M.Object)) Eligible.push_back(M);
        }
        if (CommandTiming) CommandTiming("core-eligible",C.Event,Eligible.size());
        if (!Eligible.empty())
        {
            const auto Status=PrepareCommands(C,Eligible);
            ApplicationPending=Status==ApplicationStatus::Pending;
            if (Status!=ApplicationStatus::Ready) return false;
        }
    }
    CommandReceipt Receipt;Receipt.Event=C.Event;Receipt.At=Now();
    for (const auto& M:C.Members)
    {
        const auto O=Directory.find(M.Object);if (O==Directory.end()) return false;const auto& A=O->second.Grant;
        if (A.Controller==Sender && A.ControlEpoch==M.ControlEpoch && A.SimOwner!=Player)
        {
            const auto T=CompletedTransfers.find(M.Object);
            if (T!=CompletedTransfers.end() && T->second.Source==Player && T->second.PreviousOwnerVersion==M.OwnerEpoch
                && T->second.NextOwnerVersion==A.OwnerEpoch && T->second.Target==A.SimOwner)
            {
                auto Member=M;Member.OwnerEpoch=A.OwnerEpoch;auto One=CommandWithMembers(C,{Member});
                Bytes Original;if (!EncodeMessage({H,EncodeCommand(One)},Original)) return false;
                Writer W;W.U64(T->second.IdValue);W.Blob(Original);
                if (!SendMessage(T->second.Target,MessageKind::HandoffCommands,C.Event,M.Object,W.Take())) return false;
                continue;
            }
        }
        if (A.Controller!=Sender || A.ControlEpoch!=M.ControlEpoch || A.OwnerEpoch!=M.OwnerEpoch || A.SimOwner!=Player) return false;
        if (Frozen.contains(M.Object))
        {
            auto One=CommandWithMembers(C,{M});Message Queued{H,EncodeCommand(One)};
            QueueHandoffCommand(M.Object,std::move(Queued));continue;
        }
        const auto Key=std::pair{C.Event,M.Object};auto Executed=Commands.find(Key);
        if (Executed==Commands.end())
        {
            const auto Adapter=Adapters.find(M.Object);ActiveObject=M.Object;
            const bool Accepted=A.State==Life::Alive && !A.Dormant && !PreparingApplications.contains(M.Object) && Adapter!=Adapters.end() && Adapter->second->Command(C,M);
            ActiveObject={};Executed=Commands.emplace(Key,CommandOutcome{ObjectNow(M.Object),Accepted}).first;
            if (Accepted)
            {
                ++CommandExecutions;++CommandExecutionsByKind[M.Object.Kind];
                auto& One=LatestCommands[M.Object];One=CommandWithMembers(C,{M});
                Broadcast(MessageKind::CommandState,C.Event,M.Object,EncodeCommand(One));
            }
        }
        Receipt.Members.push_back({M.Object,M.ControlEpoch,M.OwnerEpoch,Executed->second.Accepted});
    }
    if (!Receipt.Members.empty())
    {
        if (Sender==Player) CompleteCommandReceipt(Receipt);
        else SendMessage(Sender,MessageKind::CommandReceipt,C.Event,{},EncodeCommandReceipt(Receipt));
    }
    return true;
}
void ClientWorld::DrainCommands()
{
    TraceScope Scope("GuLiHybrid_CoreDrainCommands");
    if (MembershipPaused) return;
    auto Queue=std::exchange(IncomingCommands,{});
    std::set<ObjectKey> Waiting;
    for (auto& M:Queue)
    {
        FrozenCommand C;if (!DecodeCommand(M.Body,C)) continue;
        const bool Blocked=std::any_of(C.Members.begin(),C.Members.end(),[&](const auto& V){return Waiting.contains(V.Object);});
        if (CommandTiming) CommandTiming(Blocked?"core-fifo-blocked":"core-drain",C.Event,C.Members.size());
        if (Blocked || (!ApplyCommand(M.Header.Sender,M.Header,C) && ApplicationPending))
        {
            for (const auto& V:C.Members) Waiting.insert(V.Object);
            IncomingCommands.push_back(std::move(M));
        }
    }
}
bool ClientWorld::Route(const FrozenCommand& Input)
{
    if (!CanPublishFacts() || !Input.Event.Valid() || Input.Members.empty()) return false;
    auto Canonical=Input;Canonical.At=0;
    std::set<ObjectKey> Unique;
    for (auto& M:Canonical.Members)
    {
        const auto O=Directory.find(M.Object);
        if (O==Directory.end() || !HasControl(M.Object) || O->second.Grant.ControlEpoch!=M.ControlEpoch
            || !Unique.insert(M.Object).second) return false;
        M.OwnerEpoch=0;
    }
    const auto Identity=EncodeCommand(Canonical);
    if (const auto I=CommandOutbox.find(Input.Event);I!=CommandOutbox.end()) return I->second.Identity==Identity;
    constexpr std::size_t MaximumBytes=16*1024*1024;
    const auto Cost=Identity.size()*2;
    if (Identity.empty() || CommandOutbox.size()>=4096 || Cost>MaximumBytes || CommandOutboxBytes>MaximumBytes-Cost) return false;
    CommandOutbox.emplace(Input.Event,OutgoingCommand{Input,Identity,Cost,0});CommandOutboxBytes+=Cost;
    return true;
}
void ClientWorld::QueueHandoffCommand(ObjectKey Object,Message Value)
{
    auto& Queue=HandoffCommands[Object];
    const auto Existing=std::find_if(Queue.begin(),Queue.end(),[&](const Message& M)
        {return M.Header.Event==Value.Header.Event && M.Header.Sender==Value.Header.Sender;});
    if (Existing==Queue.end()) Queue.push_back(std::move(Value));
}
void ClientWorld::CompleteCommandReceipt(const CommandReceipt& Receipt)
{
    const auto I=CommandOutbox.find(Receipt.Event);
    if (I!=CommandOutbox.end())
    {
        auto& Left=I->second.Value.Members;
        std::erase_if(Left,[&](const CommandMember& M)
            {return std::any_of(Receipt.Members.begin(),Receipt.Members.end(),[&](const CommandReceiptMember& R){return R.Object==M.Object;});});
        if (Left.empty()) {CommandOutboxBytes-=I->second.ByteCost;CommandOutbox.erase(I);}
    }
    if (OnCommandReceipt) OnCommandReceipt(Receipt);
}
void ClientWorld::FlushCommandOutbox()
{
    TraceScope Scope("GuLiHybrid_CoreCommandOutbox");
    std::vector<Id> Events;
    for (const auto& [E,C]:CommandOutbox) if (C.RetryAt<=NetworkTime) Events.push_back(E);
    for (const auto E:Events)
    {
        const auto I=CommandOutbox.find(E);if (I==CommandOutbox.end()) continue;
        auto& PendingCommand=I->second;PendingCommand.RetryAt=NetworkTime+0.5;
        std::map<Id,FrozenCommand> Routed;CommandReceipt Rejected;Rejected.Event=E;Rejected.At=Now();
        for (auto M:PendingCommand.Value.Members)
        {
            const auto O=Directory.find(M.Object);
            if (O==Directory.end() || !HasControl(M.Object) || O->second.Grant.ControlEpoch!=M.ControlEpoch)
            {Rejected.Members.push_back({M.Object,M.ControlEpoch,M.OwnerEpoch,false});continue;}
            M.OwnerEpoch=O->second.Grant.OwnerEpoch;
            const auto Transfer=Transfers.find(M.Object);
            if (Transfer!=Transfers.end() && Transfer->second.Target==Player && M.OwnerEpoch==Transfer->second.PreviousOwnerVersion)
            {
                auto One=CommandWithMembers(PendingCommand.Value,{M});
                Envelope H;H.Kind=MessageKind::Command;H.Match=Match;H.Epoch=Epoch();H.Sender=Player;H.Event=E;H.At=One.At;
                QueueHandoffCommand(M.Object,{H,EncodeCommand(One)});continue;
            }
            auto [Batch,New]=Routed.try_emplace(O->second.Grant.SimOwner);
            if (New) Batch->second=CommandWithMembers(PendingCommand.Value,{});
            Batch->second.Members.push_back(M);
        }
        for (const auto& [Peer,C]:Routed)
        {
            if (!Peer.Valid()) continue;
            if (Peer==Player)
            {
                if (IncomingCommands.size()>=16384) continue;
                Envelope H;H.Kind=MessageKind::Command;H.Match=Match;H.Epoch=Epoch();H.Sender=Player;H.Event=E;H.Sequence=Sequence++;H.At=C.At;
                IncomingCommands.push_back({H,EncodeCommand(C)});
                if (CommandTiming) CommandTiming("core-local-dispatch",E,IncomingCommands.size());
            }
            else SendMessage(Peer,MessageKind::Command,E,{},EncodeCommand(C));
        }
        // Only a business receipt completes delivery. Socket backpressure and ownership changes are retried independently.
        if (!Rejected.Members.empty()) CompleteCommandReceipt(Rejected);
    }
}

void ClientWorld::RecordReceiveRejection(const Message& V)
{
    // Keep bounded examples per message kind so startup motion cannot hide
    // later state/transaction failures behind the first sixteen samples.
    ReceiveRejection R;R.Header=V.Header;R.ReceivedAt=Now();R.BodyBytes=V.Body.size();
    if (const auto O=Directory.find(V.Header.Object);O!=Directory.end())
    {R.ObjectKnown=true;R.Materialized=O->second.Materialized;R.State=O->second.Grant.State;}
    if (RecentRejections.size()>=64) RecentRejections.erase(RecentRejections.begin());
    RecentRejections.push_back(R);
    if (ReceiveRejections.size()>=96 || std::count_if(ReceiveRejections.begin(),ReceiveRejections.end(),
        [&](const auto& Previous){return Previous.Header.Kind==V.Header.Kind;})>=8) return;
    ReceiveRejections.push_back(std::move(R));
}
bool ClientWorld::ReceivePeer(Id Sender,const Message& V)
{
    if (!Failure.empty() || Sender!=V.Header.Sender || V.Header.Match!=Match || V.Header.Epoch!=Epoch()
        || !Members.contains(Sender) || !Members.at(Sender).Online) return false;
    if (V.Header.Kind==MessageKind::ResourceSnapshot||V.Header.Kind==MessageKind::ResourceSnapshotStored||V.Header.Kind==MessageKind::ResourceSnapshotInstall)
        return ReceiveResourceSnapshot(Sender,V);
    if (!ResourceMessageCurrent(V.Header)) {++HandoffSizes["resource/stale_messages_rejected"];return false;}
    if (V.Header.Kind==MessageKind::CommandState)
    {
        FrozenCommand C;if (!ValidatePeer(Sender,V.Header,Domain::Simulation,true) || !DecodeCommand(V.Body,C)
            || C.Members.size()!=1 || C.Members.front().Object!=V.Header.Object) return false;
        auto I=LatestCommands.find(V.Header.Object);
        if (I==LatestCommands.end() || I->second.Members.front().ControlEpoch<C.Members.front().ControlEpoch || I->second.At<=C.At)
            LatestCommands[V.Header.Object]=std::move(C);
        return true;
    }
    if (V.Header.Kind==MessageKind::TransactionIntent)
    {
        Reader ReaderValue(V.Body);std::uint8_t Admit;Bytes Body;TransactionRequest R;
        if (!ReaderValue.U8(Admit) || Admit>1 || !ReaderValue.Blob(Body) || !ReaderValue.Done()
            || !DecodeTransaction(V.Header,Body,R) || !ValidatePeer(Sender,V.Header,Domain::Command)
            || !HasSimulation(R.Header.Object) || R.Kind==TransactionKind::Damage) return false;
        if (Frozen.contains(R.Header.Object)) return true;
        if (Admit) SubmitAdmitted(std::move(R));else Submit(std::move(R),{});return true;
    }
    if (V.Header.Kind==MessageKind::Transaction || V.Header.Kind==MessageKind::HitFact)
    {
        TransactionRequest R;if (!DecodeTransaction(V.Header,V.Body,R)) return false;
        return StartWork(Sender,R,DeferPeerTransactions);
    }
    if ((V.Header.Kind>=MessageKind::WorkPrepare && V.Header.Kind<=MessageKind::WorkStateRequest) || V.Header.Kind==MessageKind::WorkState) return ReceiveWork(Sender,V);
    if (V.Header.Kind==MessageKind::TransactionResult)
    {
        CommitResult R;if (!DecodeCommit(V.Body,R) || R.Event!=V.Header.Event || V.Header.Object!=R.StateDomain
            || !R.StateDomain.Valid() || !V.Header.OwnerEpoch) return false;
        if (!Directory.contains(R.StateDomain))
        {
            // Result, domain body and room authority travel independently.
            // Retain an undecided delivery until the original authority can
            // be checked; it cannot authorize its own source via its payload.
            const auto Key=std::pair{Sender,R.Event};
            if (const auto Previous=DeferredPeerReceipts.find(Key);Previous!=DeferredPeerReceipts.end())
                return Previous->second.Header.Object==V.Header.Object && Previous->second.Header.OwnerEpoch==V.Header.OwnerEpoch
                    && Previous->second.Header.Epoch==V.Header.Epoch && EncodeCommit(Previous->second.Result)==V.Body;
            if (DeferredPeerReceipts.size()>=256 || V.Body.size()>8*1024*1024-DeferredPeerReceiptBytes)
            {RecordReceiveRejection(V);Cancel("Pending receipt authority capacity exceeded");return false;}
            DeferredPeerReceiptBytes+=V.Body.size();
            DeferredPeerReceipts.emplace(Key,DeferredPeerReceipt{V.Header,std::move(R),V.Body.size()});return true;
        }
        if (Numbers.Owner(R.StateDomain)!=Sender || Directory.at(R.StateDomain).Grant.OwnerEpoch!=V.Header.OwnerEpoch)
        {RecordReceiveRejection(V);return false;}
        return ApplyCommit(R);
    }
    if (V.Header.Kind==MessageKind::StateFragments)
    {
        if (ImportPeerState(Sender,V)) return true;
        // P2P state and room grants use independent connections. Retain a
        // bounded delivery while a birth grant or handoff final state arrives;
        // this never pauses simulation or waits for other clients' clocks.
        if (DeferredPeerStates.size()>=256 || DeferredPeerStateBytes+V.Body.size()>8*1024*1024)
        {++FailedStateImports;RecordReceiveRejection(V);return false;}
        DeferredPeerStates.push_back({Sender,V,NetworkTime+5});DeferredPeerStateBytes+=V.Body.size();return true;
    }
    if (V.Header.Kind==MessageKind::Command)
    {FrozenCommand C;if (!DecodeCommand(V.Body,C) || C.Event!=V.Header.Event || IncomingCommands.size()>=16384) return false;IncomingCommands.push_back(V);return true;}
    if (V.Header.Kind==MessageKind::CommandReceipt)
    {
        CommandReceipt R;if (!DecodeCommandReceipt(V.Body,R) || R.Event!=V.Header.Event) return false;
        for (const auto& M:R.Members)
        {
            const auto O=Directory.find(M.Object);if (O==Directory.end() || O->second.Grant.SimOwner!=Sender
                || O->second.Grant.OwnerEpoch!=M.OwnerEpoch || O->second.Grant.ControlEpoch!=M.ControlEpoch
                || O->second.Grant.Controller!=Player) return false;
        }
        CompleteCommandReceipt(R);return true;
    }
    if (V.Header.Kind==MessageKind::HandoffCommands)
    {
        Reader R(V.Body);Revision Transfer;Bytes Wire;Message Original;FrozenCommand C;
        const ObjectHandoff* T=nullptr;
        if (const auto I=Transfers.find(V.Header.Object);I!=Transfers.end()) T=&I->second;
        else if (const auto Complete=CompletedTransfers.find(V.Header.Object);Complete!=CompletedTransfers.end()) T=&Complete->second;
        if (!T || !R.U64(Transfer) || Transfer!=T->IdValue || Sender!=T->Source || T->Target!=Player
            || !R.Blob(Wire) || !R.Done() || !DecodeMessage(Wire,Original) || !DecodeCommand(Original.Body,C)) return false;
        for (const auto& M:C.Members) if (M.Object!=V.Header.Object) return false;
        if (Transfers.contains(V.Header.Object)) QueueHandoffCommand(V.Header.Object,std::move(Original));
        else IncomingCommands.push_back(std::move(Original));return true;
    }
    if (V.Header.Kind==MessageKind::HandoffState)
    {
        ObjectHandoffState S;
        if (!DecodeHandoffState(V.Body,S)) {RecordReceiveRejection(V);return HandoffIssue("receive/decode");}
        return InstallHandoff(Sender,S);
    }
    const auto A=Adapters.find(V.Header.Object);if (A==Adapters.end()) {++UnknownMotionLives;RecordReceiveRejection(V);return false;}
    if (V.Header.Kind==MessageKind::MotionAcknowledgment)
    {
        const auto O=Directory.find(V.Header.Object);
        return O!=Directory.end() && HasSimulation(V.Header.Object) && V.Header.OwnerEpoch==O->second.Grant.OwnerEpoch
            && A->second->MotionFeedback(Sender,V.Body);
    }
    if (V.Header.Kind==MessageKind::Motion || V.Header.Kind==MessageKind::ComponentState)
    {
        auto& S=MotionStats[V.Header.Object];++S.Received;S.Sender=Sender;S.LastReceived=V.Header.At;S.ReceivedOwnerEpoch=V.Header.OwnerEpoch;
        const auto& O=Directory.at(V.Header.Object);S.ExpectedOwnerEpoch=O.Grant.OwnerEpoch;S.ExpectedSender=O.Grant.SimOwner;
        if (!ValidatePeer(Sender,V.Header,Domain::Simulation,true)) {++S.AuthorityRejected;RecordReceiveRejection(V);return false;}
        auto& Applied=V.Header.Kind==MessageKind::Motion?MotionApplied:PublicStateApplied;
        if (const auto Last=Applied.find(V.Header.Object);Last!=Applied.end() && V.Header.At<=Last->second) {++S.Stale;return true;}
        if (const auto Last=PendingMotion.find({V.Header.Object,V.Header.Kind});Last!=PendingMotion.end() && V.Header.At<=Last->second) {++S.Stale;return true;}
        const bool Good=V.Header.Kind==MessageKind::Motion?A->second->ApplyMotion(V.Body,V.Header.At):A->second->ApplyPublicState(V.Body,V.Header.At);
        if (!Good) {++S.InvalidState;return false;}
        if (A->second->DefersMotionResult()) {PendingMotion[{V.Header.Object,V.Header.Kind}]=V.Header.At;return true;}
        Applied[V.Header.Object]=V.Header.At;
        auto& Age=Directory.at(V.Header.Object).LogicalAge;Age=std::max(Age,V.Header.At);Numbers.SetLogicalAge(V.Header.Object,Age);
        ++S.Applied;S.LastApplied=V.Header.At;return true;
    }
    if (V.Header.Kind==MessageKind::ObjectEvent)
    {
        if (!ValidatePeer(Sender,V.Header,Domain::Simulation,true) || !A->second->ApplyObjectEvent(V.Body,V.Header.At)) return false;
        if (A->second->DefersMotionResult()) return true;
        auto& Age=Directory.at(V.Header.Object).LogicalAge;Age=std::max(Age,V.Header.At);Numbers.SetLogicalAge(V.Header.Object,Age);return true;
    }
    return false;
}
bool ClientWorld::ImportPeerState(Id Sender,const Message& V)
{
    Reader R(V.Body);std::uint32_t Count;if (!R.U32(Count) || Count>4096) return false;
    std::vector<StateFragment> Parts;
    for (std::uint32_t I=0;I<Count;++I)
    {StateFragment P;if (!ReadStateFragment(R,P)) return false;Parts.push_back(std::move(P));}
    if (!R.Done()) return false;
    std::string Reason;
    if (!Numbers.Import(Sender,Parts,false,&PendingGrants,&Reason))
    {LastStateImportFailure=std::move(Reason);return false;}
    RefreshRecords(Parts);return true;
}
void ClientWorld::RetryPeerStates()
{
    TraceScope Scope("GuLiHybrid_CoreRetryPeerStates");
    for (auto I=DeferredPeerStates.begin();I!=DeferredPeerStates.end();)
    {
        const bool Applied=ImportPeerState(I->Sender,I->Value);
        if (Applied || NetworkTime>=I->Expires)
        {
            if (!Applied) {++FailedStateImports;RecordReceiveRejection(I->Value);}
            DeferredPeerStateBytes-=I->Value.Body.size();I=DeferredPeerStates.erase(I);
        }
        else ++I;
    }
}
void ClientWorld::RetryPeerReceipts()
{
    if (DeferredPeerReceipts.empty()) return;
    std::vector<std::pair<Id,Id>> Ready;
    for (const auto& [Key,Receipt]:DeferredPeerReceipts)
        if (Receipt.Header.Epoch!=Epoch() || !Members.contains(Key.first) || !Members.at(Key.first).Online
            || Directory.contains(Receipt.Result.StateDomain)) Ready.push_back(Key);
    for (const auto& Key:Ready)
    {
        const auto I=DeferredPeerReceipts.find(Key);if (I==DeferredPeerReceipts.end()) continue;
        auto Receipt=std::move(I->second);DeferredPeerReceiptBytes-=Receipt.Bytes;DeferredPeerReceipts.erase(I);
        const auto Domain=Directory.find(Receipt.Result.StateDomain);
        if (Receipt.Header.Epoch!=Epoch() || !Members.contains(Key.first) || !Members.at(Key.first).Online
            || Domain==Directory.end() || Numbers.Owner(Receipt.Result.StateDomain)!=Key.first
            || Domain->second.Grant.OwnerEpoch!=Receipt.Header.OwnerEpoch)
        {RecordReceiveRejection({Receipt.Header,EncodeCommit(Receipt.Result)});continue;}
        // ApplyCommit still waits for every required state/application and
        // owns the event's exactly-once callback. No elapsed-time fallback.
        ApplyCommit(Receipt.Result);
    }
}
Id ClientWorld::Submit(TransactionRequest R,TransactionCallback Callback,bool PreserveOriginalTime)
{
    if (!StampResourceRequest(R)) return {};
    if (!Failure.empty() || !R.Header.Event.Valid() || Pending.contains(R.Header.Event) || !HasSimulation(R.Header.Object,true)) return {};
    const auto& O=Directory.at(R.Header.Object);R.Header.Kind=MessageKind::Transaction;R.Header.Match=Match;R.Header.Epoch=Epoch();R.Header.Sender=Player;
    if (PreserveOriginalTime && (R.Header.At>O.LogicalAge || R.StartedAt>R.Header.At)) return {};
    R.Header.OwnerEpoch=O.Grant.OwnerEpoch;R.Header.ControlEpoch=O.Grant.ControlEpoch;
    if (!PreserveOriginalTime) R.Header.At=O.LogicalAge;R.Header.Sequence=Sequence++;
    if (!R.RootEvent.Valid()) R.RootEvent=R.Header.Event;
    if (!R.StartedAt) R.StartedAt=R.Header.At;
    if (R.Kind==TransactionKind::Damage && !Numbers.FreezeDamage(R)) return {};
    const Id Event=R.Header.Event;Pending.emplace(Event,PendingTransaction{std::move(R),std::move(Callback),0});return Event;
}
Id ClientWorld::SubmitControlled(TransactionRequest R,bool Admit,bool PreserveOriginalTime)
{
    if (!StampResourceRequest(R)) return {};
    if (!CanPublishFacts() || !R.Header.Event.Valid() || !HasControl(R.Header.Object) || R.Kind==TransactionKind::Damage) return {};
    if (HasSimulation(R.Header.Object)) return Admit?SubmitAdmitted(std::move(R),PreserveOriginalTime):Submit(std::move(R),{},PreserveOriginalTime);
    const Id Event=R.Header.Event;if (ObservedCommits.contains(Event)) return Event;
    PlayerIntents.try_emplace(Event,PlayerIntent{std::move(R),Admit,0});return Event;
}
Id ClientWorld::SubmitAdmitted(TransactionRequest R,bool PreserveOriginalTime)
{
    if (!StampResourceRequest(R)) return {};
    if (!CanAdmitWork(R.Kind) || !HasSimulation(R.Header.Object,true) || !R.Header.Event.Valid()) return {};
    const auto& O=Directory.at(R.Header.Object);R.Header.Match=Match;R.Header.Epoch=Epoch();R.Header.Sender=Player;R.Header.Kind=MessageKind::Transaction;
    R.Header.OwnerEpoch=O.Grant.OwnerEpoch;R.Header.ControlEpoch=O.Grant.ControlEpoch;
    if (PreserveOriginalTime && (R.Header.At>O.LogicalAge || R.StartedAt>R.Header.At)) return {};
    if (!PreserveOriginalTime) R.Header.At=O.LogicalAge;R.Header.Sequence=Sequence++;
    if (!R.StartedAt) R.StartedAt=R.Header.At;if (!R.RootEvent.Valid()) R.RootEvent=R.Header.Event;
    Bytes Admission;if (!EncodeMessage({R.Header,EncodeTransaction(R)},Admission)) return {};
    TransactionRequest Work;Work.Header.Object=R.Header.Object;Work.Header.At=R.Header.At;Work.Header.Event=StableId("GuLiTransactionAdmission/v1",R.Header.Event.ToString());
    Work.Header.ResourceGroup=R.Header.ResourceGroup;Work.Header.ResourceEpoch=R.Header.ResourceEpoch;
    Work.Kind=TransactionKind::AdmitWork;Work.Target={ObjectKind::BusinessWork,StableId("GuLiTransactionWork/v1",R.Header.Event.ToString()),1};
    Work.RootEvent=R.Header.Event;Work.StartedAt=R.StartedAt;Work.RuleId=R.RuleId;Work.Faction=R.Faction;Work.Admission=std::move(Admission);
    return Submit(std::move(Work),{},PreserveOriginalTime).Valid()?R.Header.Event:Id{};
}
void ClientWorld::RefreshRecords(std::span<const StateFragment> Fragments,bool Effects)
{
    TraceScope Scope("GuLiHybrid_CoreRefreshRecords");
    if (OnFragments) OnFragments(Fragments);
    // A committed batch can contain both a newborn and its admitted parent.
    // Publish the complete directory before any adapter resolves those links;
    // ObjectKey order places buildings before BusinessWork parents.
    std::set<ObjectKey> NewRecords;
    for (const auto& F:Fragments) if (F.Key.Capability=="object")
    {
        const auto O=Numbers.Objects().find(F.Key.Object);if (O==Numbers.Objects().end()) continue;
        if (!Directory.contains(F.Key.Object)) NewRecords.insert(F.Key.Object);
        StoreRecord(O->second);
    }
    for (const auto& F:Fragments) if (F.Key.Capability=="object")
    {
        const auto O=Numbers.Objects().find(F.Key.Object);if (O==Numbers.Objects().end()) continue;
        if (Numbers.Owner(F.Key.Object)==Player && (!PublishedLife.contains(F.Key.Object) || PublishedLife.at(F.Key.Object)!=O->second.Grant.State))
        {
            Writer W;W.U8(static_cast<std::uint8_t>(O->second.Grant.State));
            if (SendMessage({},MessageKind::ObjectEvent,{},F.Key.Object,W.Take())) PublishedLife[F.Key.Object]=O->second.Grant.State;
        }
        if (const auto G=PendingGrants.find(F.Key.Object);G!=PendingGrants.end())
        {const auto Grant=G->second;PendingGrants.erase(G);InstallGrant(Grant);}
        if (O->second.Materialized && !Adapters.contains(F.Key.Object))
            if (!Install(O->second,true))
            {
                if (BootstrapMaterializationDeferred) DeferredMaterialization.insert(F.Key.Object);
                else {Cancel("Unable to materialize committed object: "+O->second.Definition+" ordinal="+std::to_string(O->second.Ordinal));return;}
            }
        if (const auto A=Adapters.find(F.Key.Object);A!=Adapters.end())
        // Import can ignore an older fragment. The adapter receives the current
        // numeric record, so its revision must describe that same record.
        {A->second->SetAuthority(O->second.Grant,HasSimulation(F.Key.Object));A->second->ApplyLedger(O->second,Numbers.Version(F.Key),Effects);}
        if (NewRecords.contains(F.Key.Object) && Numbers.Owner(F.Key.Object)==Player)
        {Writer W;WriteAuthority(W,O->second.Grant);W.Text(O->second.Definition);W.U32(O->second.Ordinal);W.Key(O->second.Parent);SendMessage({},MessageKind::RegisterObject,{},F.Key.Object,W.Take());}
    }
}
bool ClientWorld::CommitDependenciesReady(const CommitResult& R) const
{
    if (AwaitingApplications(R)) return false;
    for (const auto& S:R.RequiredStates) if (Numbers.Version({S.Object,S.Capability})<S.Version)
        return false;
    return true;
}
bool ClientWorld::ApplyCommit(const CommitResult& R)
{
    TraceScope Scope("GuLiHybrid_CoreApplyCommit");
    if (R.ResourceGroup.Valid())
    {const auto G=ResourceAuthorities.find(R.ResourceGroup);if (G==ResourceAuthorities.end()||G->second.RequestEpoch!=R.ResourceEpoch||G->second.Recovery) return false;}
    if (!CommitDependenciesReady(R)) {WaitingResults[R.Event]=R;return true;}
    auto ResolvePending=[&]
    {
        if (const auto P=Pending.find(R.Event);P!=Pending.end())
        {auto Callback=std::move(P->second.Callback);Pending.erase(P);if (Callback) Callback(R);}
    };
    // A malformed or stale delivery can be rejected before any ledger
    // decision exists. Resolve that request without consuming the event's
    // one gameplay completion; its real committed publication may arrive later.
    if (!R.StateVersion && R.Status!=CommitStatus::Committed && R.Status!=CommitStatus::Duplicate)
    {
        ++UnsettledReceiptTotal;
        if (UnsettledReceiptEvidence.size()>=128) UnsettledReceiptEvidence.pop_front();
        UnsettledReceiptEvidence.push_back(R);
        if (R.Status!=CommitStatus::Deferred && R.Status!=CommitStatus::Paused)
        {PlayerIntents.erase(R.Event);ResolvePending();}
        return true;
    }
    PlayerIntents.erase(R.Event);
    KnownWork.erase(R.Event);
    const bool First=ObservedCommits.insert(R.Event).second;
    // A rebound requester can register after this replica already observed the
    // decision. Finish that delivery without applying gameplay effects twice.
    if (!First) {++DuplicateResultCount;ResolvePending();return true;}
    std::set<ObjectKey> Changed;
    for (const auto& O:R.Objects) Changed.insert(O.Grant.Object);
    if (R.Target.Valid()) Changed.insert(R.Target);
    for (const auto& [K,H]:R.Barriers) Changed.insert(K);
    if (R.FinalizedWork) Changed.insert(R.FinalizedWork->Grant.Object);
    for (const auto& K:Changed) if (const auto O=Numbers.Objects().find(K);O!=Numbers.Objects().end()) StoreRecord(O->second);
    for (const auto& K:Changed)
    {
        const auto O=Numbers.Objects().find(K);if (O==Numbers.Objects().end()) continue;
        if (const auto A=Adapters.find(K);A!=Adapters.end())
        {A->second->SetAuthority(O->second.Grant,HasSimulation(K));A->second->ApplyLedger(O->second,Numbers.Version({K,"object"}),true);}
    }
    ResolvePending();
    if (R.Kind==TransactionKind::AdmitWork && R.Status!=CommitStatus::Committed && R.Status!=CommitStatus::Duplicate) Pending.erase(R.Event);
    if (OnCommit) OnCommit(R);return true;
}
void ClientWorld::SetMembershipPaused(bool Value)
{
    if (MembershipPaused==Value) return;MembershipPaused=Value;Clock.SetPaused(Value);
    if (Value) for (auto& [K,A]:Adapters) A->ResetControlInput();
}
std::uint32_t ClientWorld::Advance(double Delta)
{
    if (!Failure.empty()) return 0;
    const auto Steps=Clock.Advance(Delta);
    for (std::uint32_t I=0;I<Steps;++I)
    {
        DrainCommands();std::vector<ObjectKey> Owned;
        for (const auto& K:SimulationCandidates)
        {
            const auto& O=Directory.at(K);
            if (HasSimulation(K) || (O.Grant.Dormant && HasPrivateCustody(O.Grant,Player) && !Frozen.contains(K))) Owned.push_back(K);
        }
        for (const auto& K:Owned)
        {
            auto& O=Directory.at(K);++O.LogicalAge;Numbers.SetLogicalAge(K,O.LogicalAge);ActiveObject=K;
            if (const auto A=Adapters.find(K);A!=Adapters.end() && !O.Grant.Dormant) A->second->Advance(O.LogicalAge,1.0/TicksPerSecond);
            ActiveObject={};
        }
        for (const auto& [Name,A]:Systems) A->Advance(Now()-Steps+I+1,1.0/TicksPerSecond);
        if (LogicalStepBoundary) LogicalStepBoundary();
    }
    return Steps;
}
void ClientWorld::Cancel(std::string Reason) {if (Failure.empty()) Failure=std::move(Reason);}
std::size_t ClientWorld::ExecutedCommandCount(ObjectKind Kind) const
{const auto I=CommandExecutionsByKind.find(Kind);return I==CommandExecutionsByKind.end()?0:I->second;}
std::vector<TransactionRequest> ClientWorld::PendingTransactions() const
{std::vector<TransactionRequest> Out;for (const auto& [E,V]:Pending) Out.push_back(V.Request);for (const auto& [E,V]:PlayerIntents) Out.push_back(V.Request);return Out;}
std::optional<CommandReceipt> ClientWorld::CommandResult(Id Event) const
{
    CommandReceipt R;R.Event=Event;
    for (const auto& [K,V]:Commands) if (K.first==Event && Directory.contains(K.second))
    {
        const auto& A=Directory.at(K.second).Grant;R.At=std::max(R.At,V.At);
        R.Members.push_back({K.second,A.ControlEpoch,A.OwnerEpoch,V.Accepted});
    }
    return R.Members.empty()?std::optional<CommandReceipt>{}:R;
}
}
