#include "GuLiNetClient.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace GuLiNet
{
std::map<std::string,std::size_t> ClientWorld::WorkCounts() const
{
    return {{"pending",Pending.size()},{"retry",RetryWork.size()},{"active",WorkFlows.size()},
        {"waiting_result",WaitingResults.size()},{"retained",KnownWork.size()},{"deferred_state",DeferredPeerStates.size()},
        {"publication_messages",PublicationIdentities.size()},{"publication_bytes",PublicationBytes}};
}
std::vector<WorkDiagnostic> ClientWorld::WorkDiagnostics() const
{
    std::vector<WorkDiagnostic> Out;
    auto Add=[&](Id Event,const char* Stage,const TransactionRequest& R)->WorkDiagnostic&
    {WorkDiagnostic D;D.Event=Event;D.Stage=Stage;D.Source=R.Header.Object;D.Target=R.Target;Out.push_back(std::move(D));return Out.back();};
    std::size_t N=0;
    for (const auto& [E,P]:Pending) {if (N++==8) break;Add(E,"request",P.Request);}
    N=0;for (const auto& [E,R]:RetryWork) {if (N++==8) break;Add(E,"plan_retry",R.second);}
    N=0;for (const auto& [E,F]:WorkFlows)
    {
        if (N++==8) break;auto& D=Add(E,F.Decision?"commit":"prepare",F.Work.Request);
        D.Status=F.Work.Result.Status;D.Participants=F.Participants.size();D.Prepared=F.Prepared.size();D.Committed=F.Committed.size();
    }
    N=0;for (const auto& [E,R]:WaitingResults)
    {
        if (N++==8) break;WorkDiagnostic D;D.Event=E;D.Stage="result_state";D.Target=R.Target;D.Status=R.Status;
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
    if (MembershipPaused || Frozen.contains(Numbers.ExecutionDomain(R)))
    {RetryWork[R.Header.Event]={Sender,R};return true;}
    OwnerWork W;if (!Numbers.Plan(Sender,R,W)) {RetryWork[R.Header.Event]={Sender,R};return true;}
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
    KnownWork[Event]={Current.Work,false};
    for (const auto P:Current.Participants)
    {
        if (P==Player)
        {if (Numbers.Prepare(Player,Current.Work)) Current.Prepared.insert(Player);else Rejected=true;}
        else SendMessage(P,MessageKind::WorkPrepare,Event,Current.Work.Source,Encoded);
    }
    if (Rejected)
    {
        const auto Work=Current.Work;
        for (const auto P:Current.Participants)
        {if (P==Player) Numbers.Cancel(Player,Event,Work.Proposal);else SendMessage(P,MessageKind::WorkCancel,Event,Work.Source,ProposalIdentity(Work.Proposal));}
        WorkFlows.erase(Event);RetryWork[Event]={Sender,R};return true;
    }
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
            if (!Numbers.Import(Sender,Values)) return false;
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
    auto Results=std::exchange(WaitingResults,{});for (const auto& [E,R]:Results) ApplyCommit(R);
    std::vector<ObjectHandoff> WaitingTransfers;
    for (const auto K:FreezeRequested) if (Transfers.contains(K)) WaitingTransfers.push_back(Transfers.at(K));
    for (const auto& T:WaitingTransfers) FreezeHandoff(T);
}
}
