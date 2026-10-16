#include "GuLiNetClient.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace GuLiNet
{
std::map<std::string,std::size_t> ClientWorld::WorkCounts() const
{
    return {{"pending",Pending.size()},{"controlled_intents",PlayerIntents.size()},{"retry",RetryWork.size()},{"active",WorkFlows.size()},
        {"waiting_result",WaitingResults.size()},{"retained",KnownWork.size()},{"deferred_state",DeferredPeerStates.size()},
        {"publication_messages",PublicationIdentities.size()},{"publication_bytes",PublicationBytes}};
}
std::vector<WorkDiagnostic> ClientWorld::WorkDiagnostics() const
{
    std::vector<WorkDiagnostic> Out;
    auto Add=[&](Id Event,const char* Stage,const TransactionRequest& R)->WorkDiagnostic&
    {
        WorkDiagnostic D;D.Event=Event;D.Stage=Stage;D.Source=R.Header.Object;D.Target=R.Target;
        D.RequestSender=R.Header.Sender;D.RequestOwnerVersion=R.Header.OwnerEpoch;D.RequestAt=R.Header.At;D.StartedAt=R.StartedAt;D.Rule=R.RuleId;
        if (const auto O=Numbers.Objects().find(R.Header.Object);O!=Numbers.Objects().end())
        {
            D.NumericSourceKnown=true;D.NumericSourceLife=O->second.Grant.State;D.NumericSourceAge=O->second.LogicalAge;
            D.NumericSourceOwner=O->second.Grant.Dormant?O->second.Grant.StateCustodian:O->second.Grant.SimOwner;
            D.NumericSourceOwnerVersion=O->second.Grant.OwnerEpoch;
        }
        Out.push_back(std::move(D));return Out.back();
    };
    std::size_t N=0;
    for (const auto& [E,P]:PlayerIntents) {if (N++==8) break;Add(E,"controlled_intent",P.Request);}
    N=0;
    for (const auto& [E,P]:Pending) {if (N++==8) break;Add(E,"request",P.Request);}
    N=0;for (const auto& [E,R]:RetryWork)
    {
        if (N++==8) break;auto& D=Add(E,"plan_retry",R.second);
        if (const auto Reason=PlanWaitReasons.find(E);Reason!=PlanWaitReasons.end()) D.WaitReason=Reason->second;
    }
    N=0;for (const auto& [E,F]:WorkFlows)
    {
        if (N++==8) break;auto& D=Add(E,F.Decision?"commit":"prepare",F.Work.Request);
        D.Status=F.Work.Result.Status;D.Participants=F.Participants.size();D.Prepared=F.Prepared.size();D.Committed=F.Committed.size();
    }
    N=0;for (const auto& [E,R]:WaitingResults)
    {
        if (N++==8) break;WorkDiagnostic D;D.Event=E;D.Stage="result_state";D.Target=R.Target;D.Status=R.Status;D.WaitReason=LastStateImportFailure;
        for (const auto& S:R.RequiredStates) if (const auto Version=Numbers.Version({S.Object,S.Capability});Version<S.Version)
        {D.Missing={S.Object,S.Capability};D.RequiredVersion=S.Version;D.CurrentVersion=Version;break;}
        Out.push_back(std::move(D));
    }
    return Out;
}
namespace
{
Bytes Fragments(const std::vector<StateFragment>& Values)
{Writer W;W.U32(static_cast<std::uint32_t>(Values.size()));for (const auto& V:Values) WriteStateFragment(W,V);return W.Take();}
Bytes ProposalIdentity(Id Proposal) {Writer W;W.Identity(Proposal);return W.Take();}
Bytes Evidence(const OwnerWork& Work,bool Decided)
{Writer W;W.U8(Decided);W.Blob(EncodeOwnerWork(Work));return W.Take();}
Bytes Acknowledge(Id Event,Id Proposal,bool Accepted,const std::vector<StateFragment>& Values={})
{Writer W;W.Identity(Event);W.Identity(Proposal);W.U8(Accepted?1:0);W.Blob(Fragments(Values));return W.Take();}
bool ReadAcknowledgment(std::span<const std::uint8_t> Data,Id& Event,Id& Proposal,bool& Accepted,std::vector<StateFragment>& Out)
{
    Reader R(Data);std::uint8_t Status;Bytes Body;
    if (!R.Identity(Event) || !R.Identity(Proposal) || !Proposal.Valid() || !R.U8(Status) || Status>1 || !R.Blob(Body) || !R.Done()) return false;
    Reader Parts(Body);std::uint32_t Count;if (!Parts.U32(Count) || Count>4096) return false;
    for (std::uint32_t I=0;I<Count;++I) {StateFragment V;if (!ReadStateFragment(Parts,V)) return false;Out.push_back(std::move(V));}
    if (!Parts.Done()) return false;Accepted=Status!=0;return true;
}
}
bool ClientWorld::StartWork(Id Sender,const TransactionRequest& R)
{
    if (Numbers.Owner(Numbers.ExecutionDomain(R))!=Player) return false;
    if (WorkFlows.contains(R.Header.Event)) return true;
    // Observers consume reliable publications independently. Their receive
    // speed must not become a planning gate for this owner's resource chain.
    const auto Domain=Numbers.ExecutionDomain(R);
    if (MembershipPaused || Frozen.contains(Domain) || Numbers.Busy(Domain))
    {
        PlanWaitReasons[R.Header.Event]=MembershipPaused?"membership_pause":Frozen.contains(Domain)?"execution_domain_frozen":"execution_domain_busy";
        RetryWork[R.Header.Event]={Sender,R};return true;
    }
    OwnerWork W;std::string WaitReason;
    if (!Numbers.Plan(Sender,R,W,&WaitReason))
    {PlanWaitReasons[R.Header.Event]=std::move(WaitReason);RetryWork[R.Header.Event]={Sender,R};return true;}
    PlanWaitReasons.erase(R.Header.Event);
    if (W.Changes.empty())
    {
        if (W.Result.Status==CommitStatus::Deferred || W.Result.Status==CommitStatus::Paused) {RetryWork[R.Header.Event]={Sender,R};return true;}
        if (Sender==Player) ApplyCommit(W.Result);
        else SendMessage(Sender,MessageKind::TransactionResult,W.Event,W.Result.StateDomain,EncodeCommit(W.Result));
        return true;
    }
    WorkFlow Flow;Flow.Work=std::move(W);Flow.Requester=Sender;Flow.RetryAt=NetworkTime+.5;
    for (const auto& M:Flow.Work.Changes)
    {
        const auto Owner=Numbers.MutationOwner(Flow.Work,M);
        if (!Owner.Valid() || !Members.contains(Owner) || !Members.at(Owner).Online) {RetryWork[R.Header.Event]={Sender,R};return true;}
        Flow.Participants.insert(Owner);
    }
    const auto Event=Flow.Work.Event;const auto Encoded=EncodeOwnerWork(Flow.Work);WorkFlows.emplace(Event,std::move(Flow));
    auto& Current=WorkFlows.at(Event);bool Rejected=false;
    // Reserve this planner's fragments before involving another owner. A
    // rejected local prepare must not flood remote owners with prepare/cancel
    // pairs for every hit waiting behind the same locked target.
    if (Current.Participants.contains(Player))
    {if (Numbers.Prepare(Player,Current.Work)) Current.Prepared.insert(Player);else Rejected=true;}
    if (Rejected)
    {
        Numbers.Cancel(Player,Event,Current.Work.Proposal);
        WorkFlows.erase(Event);PlanWaitReasons[Event]="local_prepare_rejected";RetryWork[Event]={Sender,R};return true;
    }
    KnownWork[Event]={Current.Work,false};
    for (const auto P:Current.Participants) if (P!=Player)
        SendMessage(P,MessageKind::WorkPrepare,Event,Current.Work.Source,Encoded);
    AdvanceWork(Event);return true;
}
void ClientWorld::AdvanceWork(Id Event)
{
    auto I=WorkFlows.find(Event);if (I==WorkFlows.end()) return;auto& F=I->second;
    if (!F.Decision && F.Prepared==F.Participants)
    {
        F.Decision=true;KnownWork[Event]={F.Work,true};
        // A single owner's prepare/commit is one synchronous local operation.
        // Only distributed work needs a recoverable decision between commits.
        if (F.Participants.size()>1) Broadcast(MessageKind::WorkState,Event,F.Work.Source,Evidence(F.Work,true));
        const auto Bytes=EncodeOwnerWork(F.Work);
        for (const auto P:F.Participants)
        {
            if (P==Player)
            {
                if (!Numbers.Commit(Player,F.Work)) return;
                auto Published=Numbers.Published(F.Work);
                // Materialization can synchronously emit object events. Put the
                // admitted state in the ordered stream before invoking adapters.
                Broadcast(MessageKind::StateFragments,Event,F.Work.Source,Fragments(Published));
                RefreshRecords(Published);F.Committed.insert(Player);
            }
            else SendMessage(P,MessageKind::WorkCommit,Event,F.Work.Source,Bytes);
        }
    }
    if (F.Decision && F.Committed==F.Participants)
    {
        const auto Result=F.Work.Result;WorkFlows.erase(I);RetryWork.erase(Event);KnownWork.erase(Event);
        ApplyCommit(Result);Broadcast(MessageKind::TransactionResult,Event,Result.StateDomain,EncodeCommit(Result));
    }
}
bool ClientWorld::ReceiveWork(Id Sender,const Message& V)
{
    if (V.Header.Kind==MessageKind::WorkStateRequest)
    {
        Reader R(V.Body);std::uint32_t Count;
        if (!R.U32(Count) || !Count || Count>64) return false;
        std::set<StateKey> Seen;std::set<ObjectKey> Selected;
        for (std::uint32_t I=0;I<Count;++I)
        {
            StateKey Key;Revision Version;
            if (!R.Key(Key.Object) || !Key.Object.Valid() || !R.Text(Key.Capability,256) || Key.Capability.empty()
                || !R.U64(Version) || !Version || !Seen.insert(Key).second) return false;
            // A repair is a read of this peer's committed numeric state. It
            // cannot authorize a foreign object or synthesize a missing write.
            if (Numbers.Owner(Key.Object)==Player && Numbers.Version(Key)>=Version) Selected.insert(Key.Object);
        }
        if (!R.Done()) return false;
        if (Selected.empty()) return true;
        const std::vector<ObjectKey> Keys(Selected.begin(),Selected.end());
        const auto Values=Numbers.Export(Keys);
        if (Values.size()>4096) return false;
        return SendMessage(Sender,MessageKind::StateFragments,V.Header.Event,Keys.front(),Fragments(Values));
    }
    if (V.Header.Kind==MessageKind::WorkState)
    {
        Reader R(V.Body);std::uint8_t Decided;Bytes Body;OwnerWork W;
        if (!R.U8(Decided) || Decided>1 || !R.Blob(Body) || !R.Done() || !DecodeOwnerWork(Body,W) || W.Event!=V.Header.Event) return false;
        bool Participant=W.Initiator==Sender;
        for (const auto& M:W.Changes) Participant|=Numbers.MutationOwner(W,M)==Sender;
        if (!Participant || ObservedCommits.contains(W.Event)) return Participant;
        const auto Previous=KnownWork.find(W.Event);
        if (Previous!=KnownWork.end() && Previous->second.Decided && Previous->second.Work.Proposal!=W.Proposal && !W.Continuation) return false;
        KnownWork[W.Event]={std::move(W),Decided!=0};return true;
    }
    if (V.Header.Kind==MessageKind::WorkPrepare || V.Header.Kind==MessageKind::WorkCommit)
    {
        OwnerWork W;if (!DecodeOwnerWork(V.Body,W) || W.Event!=V.Header.Event || W.Initiator!=Sender) return false;
        if (W.Continuation && Numbers.Owner(Numbers.ExecutionDomain(W.Request))!=Sender) return false;
        if (V.Header.Kind==MessageKind::WorkPrepare)
        {
            bool FrozenObject=false;std::vector<ObjectKey> Owned;
            for (const auto& M:W.Changes) if (Numbers.MutationOwner(W,M)==Player)
            {FrozenObject|=Frozen.contains(M.After.Key.Object);Owned.push_back(M.After.Key.Object);}
            const bool Good=!FrozenObject && Numbers.Prepare(Sender,W);
            std::vector<StateFragment> Changed;
            if (!Good)
            {
                auto Current=Numbers.Export(Owned);
                for (auto& P:Current) for (const auto& M:W.Changes) if (M.After.Key==P.Key) {Changed.push_back(std::move(P));break;}
            }
            if (Good) KnownWork[W.Event]={W,W.Continuation};
            SendMessage(Sender,MessageKind::WorkPrepared,W.Event,W.Source,Acknowledge(W.Event,W.Proposal,Good,Changed));return true;
        }
        const bool Good=Numbers.Commit(Sender,W);const auto Published=Good?Numbers.Published(W):std::vector<StateFragment>{};
        if (Good)
        {
            KnownWork[W.Event]={W,true};Broadcast(MessageKind::WorkState,W.Event,W.Source,Evidence(W,true));
            Broadcast(MessageKind::StateFragments,W.Event,W.Source,Fragments(Published));RefreshRecords(Published);
        }
        SendMessage(Sender,MessageKind::WorkReceipt,W.Event,W.Source,Acknowledge(W.Event,W.Proposal,Good,Published));return true;
    }
    if (V.Header.Kind==MessageKind::WorkCancel)
    {
        Reader R(V.Body);Id Proposal;if (!R.Identity(Proposal) || !R.Done()) return false;
        Numbers.Cancel(Sender,V.Header.Event,Proposal);
        const auto K=KnownWork.find(V.Header.Event);
        if (K!=KnownWork.end() && K->second.Work.Proposal==Proposal && !K->second.Decided) KnownWork.erase(K);
        return true;
    }
    if (V.Header.Kind==MessageKind::WorkPrepared || V.Header.Kind==MessageKind::WorkReceipt)
    {
        Id Event,Proposal;bool Accepted;std::vector<StateFragment> Values;
        if (!ReadAcknowledgment(V.Body,Event,Proposal,Accepted,Values) || Event!=V.Header.Event) return false;
        auto I=WorkFlows.find(Event);if (I==WorkFlows.end()) return true;
        if (I->second.Work.Proposal!=Proposal) return true;
        if (!I->second.Participants.contains(Sender)) return false;
        if (V.Header.Kind==MessageKind::WorkPrepared)
        {
            if (I->second.Decision) return true;
            if (!Accepted)
            {
                const auto W=I->second.Work;const auto Requester=I->second.Requester;
                for (const auto P:I->second.Participants)
                {if (P==Player) Numbers.Cancel(Player,Event,W.Proposal);else SendMessage(P,MessageKind::WorkCancel,Event,W.Source,ProposalIdentity(W.Proposal));}
                WorkFlows.erase(I);
                if (!Values.empty() && Numbers.Import(Sender,Values)) RefreshRecords(Values);
                RetryWork[Event]={Requester,W.Request};return true;
            }
            I->second.Prepared.insert(Sender);
        }
        else
        {
            if (!I->second.Decision || !Accepted) return false;
            // This planner already validated the exact decided birth. A shared
            // group's owner need not own its parent. Admit only these proposal
            // grants; unrelated unknown objects still require room admission.
            std::map<ObjectKey,Authority> BirthGrants;
            for (const auto& B:I->second.Work.Result.Births)
                for (const auto& M:I->second.Work.Changes) if (M.After.Key.Object==B.Grant.Object && M.After.Key.Capability=="object")
                {
                    Reader R(M.After.Value);ObjectRecord O;
                    if (!ReadObject(R,O) || !R.Done() || O.Grant.Object!=B.Grant.Object) return false;
                    BirthGrants.emplace(B.Grant.Object,O.Grant);
                }
            if (!Numbers.Import(Sender,Values,false,&BirthGrants)) return false;
            RefreshRecords(Values);I->second.Committed.insert(Sender);
        }
        AdvanceWork(Event);return true;
    }
    return false;
}
void ClientWorld::PollNetwork(double Now)
{
    if (!std::isfinite(Now) || Now<NetworkTime || !Failure.empty()) return;NetworkTime=Now;
    FlushPublications();
    RetryPeerStates();
    FlushCommandOutbox();
    for (auto& [E,I]:PlayerIntents) if (I.RetryAt<=Now && HasControl(I.Request.Header.Object))
    {
        I.RetryAt=Now+.5;const auto K=I.Request.Header.Object;
        if (HasSimulation(K)) {if (I.Admit) SubmitAdmitted(I.Request);else Submit(I.Request,{});}
        else
        {Writer W;W.U8(I.Admit?1:0);W.Blob(EncodeTransaction(I.Request));SendMessage(Numbers.Owner(K),MessageKind::TransactionIntent,E,K,W.Take());}
    }
    for (auto I=EarlyHandoffStates.begin();I!=EarlyHandoffStates.end();) if (Now-I->second.first>10) I=EarlyHandoffStates.erase(I);else ++I;
    // Network retries and acknowledgments continue during membership pauses.
    // They do not depend on any simulator's logical age.
    // Both the exporting and importing side retain pending work while frozen.
    // Neither may send it past the captured boundary before ownership commits.
    std::vector<Id> Events;for (const auto& [E,P]:Pending)
        if (P.RetryAt<=Now && !Frozen.contains(P.Request.Header.Object)) Events.push_back(E);
    for (const auto E:Events)
    {
        const auto I=Pending.find(E);if (I==Pending.end()) continue;auto& P=I->second;P.RetryAt=Now+.5;
        const auto Domain=Numbers.ExecutionDomain(P.Request);const auto Peer=Numbers.Owner(Domain);
        if (!Peer.Valid()) continue;
        if (Peer==Player) StartWork(Player,P.Request);
        else Transmit(Peer,{P.Request.Header,EncodeTransaction(P.Request)});
    }
    if (Now>=NextWorkRetry)
    {
        NextWorkRetry=Now+.1;
        auto Retry=std::exchange(RetryWork,{});
        for (const auto& [E,R]:Retry) if (!WorkFlows.contains(E)) StartWork(R.first,R.second);
        for (auto& [E,K]:KnownWork)
        {
            if (Members.contains(K.Work.Initiator) && Members.at(K.Work.Initiator).Online) continue;
            if (!K.Decided) {Numbers.Cancel(K.Work.Initiator,E,K.Work.Proposal);continue;}
            if (Numbers.Owner(Numbers.ExecutionDomain(K.Work.Request))!=Player || WorkFlows.contains(E)) continue;
            auto W=K.Work;if (!Numbers.Resume(W)) continue;
            WorkFlow F;F.Work=W;F.Requester=Player;F.RetryAt=Now;
            for (const auto& M:W.Changes) {const Id Owner=Numbers.MutationOwner(W,M);if (Owner.Valid()) F.Participants.insert(Owner);}
            if (F.Participants.empty()) continue;
            if (F.Participants.contains(Player) && Numbers.Prepare(Player,W)) F.Prepared.insert(Player);
            WorkFlows.emplace(E,std::move(F));K.Work=std::move(W);
        }
    }
    Events.clear();for (const auto& [E,F]:WorkFlows) if (F.RetryAt<=Now) Events.push_back(E);
    for (const auto E:Events)
    {
        auto I=WorkFlows.find(E);if (I==WorkFlows.end()) continue;auto& F=I->second;F.RetryAt=Now+.5;
        std::set<Id> CurrentOwners;bool OwnersReady=true;
        for (const auto& M:F.Work.Changes)
        {const Id Owner=Numbers.MutationOwner(F.Work,M);OwnersReady&=Owner.Valid() && Members.contains(Owner) && Members.at(Owner).Online;CurrentOwners.insert(Owner);}
        if (!OwnersReady) continue;
        if (CurrentOwners!=F.Participants)
        {
            const auto Old=F.Work;
            for (const auto P:F.Participants)
            {if (P==Player) Numbers.Cancel(Player,E,Old.Proposal);else SendMessage(P,MessageKind::WorkCancel,E,Old.Source,ProposalIdentity(Old.Proposal));}
            if (!F.Decision)
            {const auto Requester=F.Requester;WorkFlows.erase(I);RetryWork[E]={Requester,Old.Request};continue;}
            if (!Numbers.Resume(F.Work)) continue;
            F.Participants=std::move(CurrentOwners);F.Prepared.clear();F.Committed.clear();F.Decision=false;KnownWork[E]={F.Work,true};
        }
        const auto Body=EncodeOwnerWork(F.Work);
        for (const auto Peer:F.Participants)
        {
            if (Peer==Player)
            {
                if (!F.Decision && !F.Prepared.contains(Peer) && Numbers.Prepare(Player,F.Work)) F.Prepared.insert(Peer);
                if (F.Decision && !F.Committed.contains(Peer) && Numbers.Commit(Player,F.Work))
                {const auto Published=Numbers.Published(F.Work);Broadcast(MessageKind::StateFragments,E,F.Work.Source,Fragments(Published));RefreshRecords(Published);F.Committed.insert(Peer);}
            }
            else if (!F.Decision && !F.Prepared.contains(Peer)) SendMessage(Peer,MessageKind::WorkPrepare,E,F.Work.Source,Body);
            else if (F.Decision && !F.Committed.contains(Peer)) SendMessage(Peer,MessageKind::WorkCommit,E,F.Work.Source,Body);
        }
        AdvanceWork(E);
    }
    for (auto I=PlanWaitReasons.begin();I!=PlanWaitReasons.end();)
        if (!RetryWork.contains(I->first)) I=PlanWaitReasons.erase(I);else ++I;
    auto Results=std::exchange(WaitingResults,{});for (const auto& [E,R]:Results) ApplyCommit(R);
    if (Now>=NextStateRepair)
    {
        NextStateRepair=Now+1.;
        std::map<Id,std::map<StateKey,Revision>> Missing;
        // A birth body can expire from the bounded defer queue before its
        // control-plane grant arrives. Repair that authenticated live grant
        // even when no local requester is waiting for the original result.
        for (const auto& [K,Grant]:PendingGrants)
        {
            if (Grant.State!=Life::Alive || Directory.contains(K)) continue;
            const Id Owner=Grant.Dormant?Grant.StateCustodian:Grant.SimOwner;
            if (!Owner.Valid() || Owner==Player || !Members.contains(Owner) || !Members.at(Owner).Online) continue;
            auto& Requests=Missing[Owner];
            if (Requests.size()<64) Requests.emplace(StateKey{K,"object"},1);
        }
        const auto Collect=[&](const CommitResult& R)
        {
            for (const auto& Required:R.RequiredStates)
            {
                const StateKey Key{Required.Object,Required.Capability};
                if (Numbers.Version(Key)>=Required.Version) continue;
                Id Owner=Numbers.Owner(Key.Object);
                if (!Owner.Valid())
                    if (const auto Grant=PendingGrants.find(Key.Object);Grant!=PendingGrants.end())
                        Owner=Grant->second.Dormant?Grant->second.StateCustodian:Grant->second.SimOwner;
                const auto Use=[&](const ObjectRecord& O)
                {if (!Owner.Valid() && O.Grant.Object==Key.Object) Owner=O.Grant.Dormant?O.Grant.StateCustodian:O.Grant.SimOwner;};
                for (const auto& O:R.Objects) Use(O);
                for (const auto& O:R.Births) Use(O);
                if (R.FinalizedWork) Use(*R.FinalizedWork);
                if (!Owner.Valid() || Owner==Player || !Members.contains(Owner) || !Members.at(Owner).Online) continue;
                auto& Requests=Missing[Owner];
                if (Requests.size()<64 || Requests.contains(Key)) Requests[Key]=std::max(Requests[Key],Required.Version);
            }
        };
        for (const auto& [Event,R]:WaitingResults) Collect(R);
        for (const auto& [Event,K]:KnownWork) if (K.Decided) Collect(K.Work.Result);
        for (const auto& [Peer,Requests]:Missing)
        {
            Writer W;W.U32(static_cast<std::uint32_t>(Requests.size()));
            for (const auto& [Key,Version]:Requests) {W.Key(Key.Object);W.Text(Key.Capability);W.U64(Version);}
            SendMessage(Peer,MessageKind::WorkStateRequest,{},Requests.begin()->first.Object,W.Take());
        }
    }
    std::vector<ObjectHandoff> WaitingTransfers;
    for (const auto K:FreezeRequested) if (Transfers.contains(K)) WaitingTransfers.push_back(Transfers.at(K));
    for (const auto& T:WaitingTransfers) FreezeHandoff(T);
}
}
