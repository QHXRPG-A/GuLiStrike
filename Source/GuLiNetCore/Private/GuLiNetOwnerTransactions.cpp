#include "GuLiNetOwnerState.h"
#include "GuLiNetManifest.h"
#include "GuLiNetCapability.h"
#include "GuLiNetHash.h"
#include "GuLiNetTrace.h"
#include <algorithm>
#include <cmath>

namespace GuLiNet
{
OwnerState::OwnerState(Id M,Id P,Revision E):Match(M),Player(P),MatchEpoch(E) {}
Id OwnerState::AttackIdentity(const TransactionRequest& R) const
{
    if (R.Kind!=TransactionKind::Damage) return R.RootEvent;
    Writer W;W.Identity(R.RootEvent);W.Key(R.Header.Object);W.Key(R.Target);
    return StableId("GuLiTargetDamage/v1",Sha256(W.Take()));
}
ObjectKey OwnerState::AttackDomain(const TransactionRequest& R) const
{return R.Kind==TransactionKind::Damage?R.Target:R.Header.Object;}
OwnerState OwnerState::Stage(const TransactionRequest* Request,const std::vector<StateFragment>* Fragments) const
{
    TraceScope Scope("GuLiHybrid_CoreStage");
    // Simulation state is isolated; transport queues and the unbounded event
    // history are not part of a numeric transaction's staging value.
    OwnerState V(Match,Player,MatchEpoch);V.StageSource=this;V.DecisionDomain=DecisionDomain;
    V.Players=Players;
    {
        TraceScope Copy("GuLiHybrid_CoreStageObjects");
        // Decided fragments isolate their writes immediately. Numeric planning
        // reads the complete immutable directory and copies records on write.
        if (Fragments)
        {for (const auto& F:*Fragments) if (const auto I=Directory.find(F.Key.Object);I!=Directory.end()) V.Directory.try_emplace(I->first,I->second);}
    }
    {TraceScope Copy("GuLiHybrid_CoreStageRules");V.Rules=Rules;V.UpgradeCatalogs=UpgradeCatalogs;}
    {
        TraceScope Copy("GuLiHybrid_CoreStageResources");
        bool Economy=!Fragments,Resources=!Fragments,Mining=!Fragments;
        if (Fragments) for (const auto& F:*Fragments)
        {
            const auto& Name=F.Key.Capability;
            Economy|=Name=="account";
            Resources|=Name=="cargo" || Name=="processor" || Name.starts_with("ore/") || Name.starts_with("territory/");
            Mining|=Name.starts_with("leases/");
        }
        if (Economy) V.EconomyLedger=EconomyLedger;
        if (Resources) V.ResourceLedgerValue=ResourceLedgerValue;
        if (Mining) V.MiningReservationsValue=MiningReservationsValue;
    }
    V.ResourceActorProfiles=ResourceActorProfiles;V.ActiveWorks=ActiveWorks;V.ActiveWorkEvents=ActiveWorkEvents;
    V.HeldOre=HeldOre;V.HeldFactoryCapacity=HeldFactoryCapacity;V.FinalizedAccounts=FinalizedAccounts;
    V.OwnershipDefinitions=OwnershipDefinitions;
    auto Include=[&](const TransactionRequest& R)
    {
        if (const auto I=Results.find(R.Header.Event);I!=Results.end())
        {V.Results.emplace(*I);V.RequestBodies.emplace(R.Header.Event,RequestBodies.at(R.Header.Event));V.ResultDomains.emplace(R.Header.Event,ResultDomains.at(R.Header.Event));}
        if (const auto I=Attacks.find(AttackIdentity(R));I!=Attacks.end()) V.Attacks.emplace(*I);
        if (R.Kind!=TransactionKind::Damage)
            if (const auto I=LastAttack.find({R.Header.Object,R.RuleId});I!=LastAttack.end()) V.LastAttack.emplace(*I);
    };
    if (Request)
    {
        Include(*Request);
        if (!Request->Admission.empty())
        {Message M;TransactionRequest R;if (DecodeMessage(Request->Admission,M) && DecodeTransaction(M.Header,M.Body,R)) Include(R);}
        if (const auto I=Directory.find(Request->Header.Object);I!=Directory.end() && !I->second.FrozenWork.empty())
        {FrozenWorkRecipe W;if (DecodeFrozenWork(I->second,W)) Include(W.Request);}
    }
    return V;
}
bool OwnerState::ApplyValidated(const std::vector<StateFragment>& Fragments)
{
    // The caller validated this exact ordered set on Stage() on the same room
    // thread. Applying it here preserves unrelated history and in-flight work.
    for (const auto& F:Fragments) if (!ApplyFragment(F)) return false;
    return RebuildWorkIndex();
}
bool OwnerState::Configure(const RuntimeManifest& M,std::string& Error)
{
    if (!Directory.empty() || !SetRules(M.Rules) || !BeginEconomy(M.StartingRed,M.StartingBlue)) return false;
    std::map<std::string,CompiledUpgradeCatalog> Catalogs;
    if (!ReadUpgradeCatalogs(M,Catalogs,Error) || !ConfigureUpgradeCatalogs(Catalogs)) return false;
    if (!ReadOwnershipDefinitions(M,OwnershipDefinitions,Error) || !RuntimeObjects(M,Directory,Error) || !ConfigureOwnerResources(*this,M,Error)
        || !InitializeOwnedCapabilityState(Directory)) return false;
    for (const auto& [K,O]:Directory) {if (!ValidateObjectCapabilities(O)) return false;IndexRecord(O);}
    return true;
}
bool OwnerState::Install(const ObjectRecord& O)
{
    if (!O.Grant.Object.Valid() || !UpgradeStateAllowed(O) || !ValidateObjectCapabilities(O)) return false;
    const auto Existing=Directory.find(O.Grant.Object);
    if (Existing!=Directory.end()) return SetAuthority(O.Grant);
    Directory.emplace(O.Grant.Object,O);std::optional<ResourceLedger> NewResources;
    if (!PrepareResourceActor(O,true,NewResources)) {Directory.erase(O.Grant.Object);return false;}
    if (NewResources) ResourceLedgerValue=std::move(*NewResources);
    IndexRecord(O);
    return true;
}
bool OwnerState::SetAuthority(const Authority& A)
{
    const auto I=Directory.find(A.Object);if (I==Directory.end()) return false;
    if (A.OwnerEpoch<I->second.Grant.OwnerEpoch || A.ControlEpoch<I->second.Grant.ControlEpoch) return false;
    const auto LifeState=std::max(I->second.Grant.State,A.State);I->second.Grant=A;I->second.Grant.State=LifeState;IndexRecord(I->second);return true;
}
void OwnerState::IndexRecord(const ObjectRecord& O)
{
    if (StageSource) return; // A staged transaction reads the immutable live index.
    const auto K=O.Grant.Object;
    if (O.Grant.State<Life::Dead) LivingObjects.insert(K);else LivingObjects.erase(K);
    if (O.Grant.State==Life::Alive && O.Supplied && O.ShieldRadius>0) ShieldObjects.insert(K);else ShieldObjects.erase(K);
    auto& Maximum=MaximumOrdinals[OrdinalScope(K.Kind)];Maximum=std::max(Maximum,O.Ordinal);
}
void OwnerState::SetLogicalAge(ObjectKey K,Tick At)
{if (const auto I=Directory.find(K);I!=Directory.end()) I->second.LogicalAge=At;}
Id OwnerState::Owner(ObjectKey K) const
{
    const auto* O=FindRecord(K);return !O?Id{}:(O->Grant.Dormant?O->Grant.StateCustodian:O->Grant.SimOwner);
}
const ObjectRecord* OwnerState::FindRecord(ObjectKey K) const
{
    const auto I=Directory.find(K);
    return I!=Directory.end()?&I->second:(StageSource?StageSource->FindRecord(K):nullptr);
}
ObjectRecord* OwnerState::EditRecord(ObjectKey K)
{
    if (const auto I=Directory.find(K);I!=Directory.end()) return &I->second;
    const auto* Source=StageSource?StageSource->FindRecord(K):nullptr;
    return Source?&Directory.emplace(K,*Source).first->second:nullptr;
}
Id OwnerState::MutationOwner(const OwnerWork& W,const StateMutation& M) const
{
    const auto Existing=Directory.find(M.After.Key.Object);
    if (Existing!=Directory.end() && Owner(M.After.Key.Object).Valid()) return Owner(M.After.Key.Object);
    for (const auto& B:W.Result.Births) if (B.Grant.Object==M.After.Key.Object) return Owner(W.Source);
    return {};
}
bool OwnerState::Busy(ObjectKey K) const
{for (const auto& [Key,Event]:Locks) if (Key.Object==K) return true;return false;}
bool OwnerState::IsLiveMember(Id P) const
{
    const auto M=Players.find(P);return M!=Players.end() && M->second.Online && M->second.Ready && M->second.Seat!=Role::Observer;
}
bool OwnerState::Authorize(Id Sender,const Envelope& H,Domain Field,bool AllowDead) const
{
    if (H.Match!=Match || H.Epoch!=MatchEpoch || H.Sender!=Sender || !IsLiveMember(Sender)) return false;
    const auto* O=FindRecord(H.Object);if (!O) return false;const auto& A=O->Grant;
    if (A.State!=Life::Alive && !(AllowDead && A.State==Life::Dead)) return false;
    return Field==Domain::Command?A.Controller==Sender && H.ControlEpoch==A.ControlEpoch
        :Owner(H.Object)==Sender && H.OwnerEpoch==A.OwnerEpoch;
}
ObjectKey OwnerState::ExecutionDomain(const TransactionRequest& R) const
{
    if (R.Kind==TransactionKind::Damage) return R.Target;
    if (R.Kind==TransactionKind::Credit || R.Kind==TransactionKind::Reserve || R.Kind==TransactionKind::Commit || R.Kind==TransactionKind::Refund)
        return TeamAccountObject(R.Faction);
    if (R.Kind==TransactionKind::AdmitWork)
    {
        Message M;TransactionRequest Original;
        if (DecodeMessage(R.Admission,M) && DecodeTransaction(M.Header,M.Body,Original)) return ExecutionDomain(Original);
    }
    return R.Header.Object;
}
bool OwnerState::FreezeDamage(TransactionRequest& R) const
{
    if (R.Kind!=TransactionKind::Damage || Owner(R.Header.Object)!=Player) return false;
    const auto O=Directory.find(R.Header.Object);const auto V=Rules.find(R.RuleId);
    if (O==Directory.end() || V==Rules.end() || V->second.Kind!=TransactionKind::Damage || V->second.SourceDefinition!=O->second.Definition) return false;
    Rule RuleValue=V->second;
    if (!RuleCapabilityAllowed(O->second,RuleValue) || !ResolveCapabilityRule(O->second,Directory,R.RuleId,RuleValue)
        || !std::isfinite(RuleValue.Damage) || RuleValue.Damage<=0) return false;
    R.DamageAmount=RuleValue.Damage;return true;
}
std::set<StateKey> OwnerState::ChangedKeys(const TransactionRequest& Request,const CommitResult& Result) const
{
    TransactionRequest R=Request;
    if (R.Kind==TransactionKind::AdmitWork)
    {Message M;TransactionRequest Original;if (DecodeMessage(R.Admission,M) && DecodeTransaction(M.Header,M.Body,Original)) R=std::move(Original);}
    if (const auto W=Directory.find(R.Header.Object);W!=Directory.end() && !W->second.FrozenWork.empty())
    {FrozenWorkRecipe Frozen;if (DecodeFrozenWork(W->second,Frozen)) R=std::move(Frozen.Request);}
    std::set<StateKey> Out{{Result.StateDomain,"event/"+Result.Event.ToString()}};
    std::set<ObjectKey> Objects{R.Header.Object};if (R.Target.Valid()) Objects.insert(R.Target);
    if (R.Related.Valid()) Objects.insert(R.Related);
    for (const auto& O:Result.Objects) Objects.insert(O.Grant.Object);
    for (const auto& [K,H]:Result.Barriers) Objects.insert(K);
    if (Result.FinalizedWork) {Objects.insert(Result.FinalizedWork->Grant.Object);Objects.insert(Result.FinalizedWork->Parent);}
    for (const auto& K:Objects)
    {
        Out.insert({K,"object"});Out.insert({K,"tasks"});Out.insert({K,"cargo"});Out.insert({K,"processor"});
        if (K==TeamAccountObject(Team::Red) || K==TeamAccountObject(Team::Blue)) Out.insert({K,"account"});
    }
    Out.insert({AttackDomain(R),"attack/"+AttackIdentity(R).ToString()});
    if (R.Kind!=TransactionKind::Damage) Out.insert({R.Header.Object,"cooldown/"+R.RuleId});
    const auto& Nodes=ResourceLedgerValue.Snapshot().Nodes;
    if (R.ResourceNode && Nodes.contains(R.ResourceNode))
        Out.insert({MiningClusterObject(Nodes.at(R.ResourceNode).Cluster),"ore/"+std::to_string(R.ResourceNode)});
    std::set<std::uint32_t> Clusters;if (R.MiningCluster) Clusters.insert(R.MiningCluster);
    if (Result.Mining.Lease.Cluster) Clusters.insert(Result.Mining.Lease.Cluster);
    for (const auto& K:Objects) if (const auto I=MiningReservationsValue.Snapshot().Leases.find(K);I!=MiningReservationsValue.Snapshot().Leases.end()) Clusters.insert(I->second.Cluster);
    for (const auto C:Clusters) Out.insert({MiningClusterObject(C),"leases/"+std::to_string(C)});
    for (const auto& [Index,Owner]:Result.TerritoryOwners) Out.insert({TerritoryStateObject(Index),"territory/"+std::to_string(Index)});
    for (const auto& [K,Owner]:Result.InventoryOwners) {Out.insert({K,"cargo"});Out.insert({K,"processor"});}
    return Out;
}
bool OwnerState::Plan(Id Sender,const TransactionRequest& R,OwnerWork& Out)
{
    TraceScope Scope("GuLiHybrid_CorePlan");
    const auto Domain=ExecutionDomain(R);
    if (!Domain.Valid() || Owner(Domain)!=Player || !R.Header.Event.Valid()) return false;
    OwnerState Candidate=Stage(&R);Candidate.DecisionDomain=Domain;
    auto Result=Candidate.Evaluate(Sender,R,StableId("GuLiOwnerReservation/v1",R.Header.Event.ToString()));
    if (Result.Status==CommitStatus::Deferred || Result.Status==CommitStatus::Paused) return false;
    if (Result.Status==CommitStatus::Committed)
    {
        std::map<ObjectKey,ObjectRecord> Changes;
        for (const auto& O:Result.Objects) Changes.emplace(O.Grant.Object,O);
        if (Result.Kind==TransactionKind::Damage && Candidate.Directory.contains(Result.Target)) Changes[Result.Target]=Candidate.Directory.at(Result.Target);
        for (const auto& [K,H]:Result.Barriers) Changes[K]=Candidate.Directory.at(K);
        if (!ProjectCapabilityChanges(Directory,Changes))
        {
            Candidate=Stage(&R);Candidate.DecisionDomain=Domain;Result={};Result.Event=R.Header.Event;Result.Kind=R.Kind;
            Result.Faction=R.Faction;Result.Target=R.Target;Result.StateDomain=Domain;Result.StateVersion=Candidate.NextDecisionVersion();
            Result.Status=CommitStatus::InsufficientResources;Candidate.Remember(R,Result);
        }
        else
        {
            for (auto& [K,O]:Changes)
            {
                if (!Directory.contains(K))
                {
                    O.LogicalAge=O.BornAt;
                    const auto Policy=OwnershipDefinitions.find(O.Definition);if (Policy==OwnershipDefinitions.end()) return false;
                    O.Grant.Policy=Policy->second.Policy;O.Grant.ControllerSeat=Policy->second.ControllerSeat;O.Grant.RetireOnVacancy=Policy->second.RetireOnVacancy;
                    O.Grant.OwnershipGroup=Policy->second.Group.empty()?Id{}:StableId("GuLiOwnershipGroup/v1",Policy->second.Group);
                    O.Grant.GroupEpoch=0;
                    if (O.Grant.OwnershipGroup.Valid())
                        for (const auto& [Other,V]:Directory) if (V.Grant.OwnershipGroup==O.Grant.OwnershipGroup)
                        {O.Grant.GroupEpoch=V.Grant.GroupEpoch;break;}
                    O.Grant.SimOwner=Owner(R.Header.Object);O.Grant.OwnerEpoch=1;
                    if (O.Grant.Policy==OwnershipPolicy::ControllerPinned && O.Grant.SimOwner!=O.Grant.Controller) return false;
                    if (O.Grant.State==Life::Registered) O.Grant.State=Life::Alive;
                    if (O.Ordinal)
                    {
                        if (O.Grant.SimOwner!=Player || !Players.contains(Player) || !Players.at(Player).OrdinalSlot) return false;
                        const auto Prefix=Players.at(Player).OrdinalSlot<<24;auto& Next=NextOrdinal[OrdinalScope(K.Kind)];
                        if (!Next) for (const auto& [Other,Record]:Directory)
                            if (OrdinalScope(Other.Kind)==OrdinalScope(K.Kind) && (Record.Ordinal>>24)==Players.at(Player).OrdinalSlot)
                                Next=std::max(Next,Record.Ordinal&0xffffffu);
                        if (Next>=0xffffffu) return false;O.Ordinal=Prefix|++Next;
                    }
                }
                Candidate.Directory.insert_or_assign(K,O);
                const auto I=std::find_if(Result.Objects.begin(),Result.Objects.end(),[&](const auto& V){return V.Grant.Object==K;});
                if (I==Result.Objects.end()) Result.Objects.push_back(O);else *I=O;
            }
            for (auto& B:Result.Births) B=Candidate.Directory.at(B.Grant.Object);
            Candidate.Results[R.Header.Event]=Result;
        }
    }
    // Finalized reservation provenance belongs to the corresponding team account.
    for (const auto& [Key,Reservation]:EconomyLedger.Snapshot().Pending)
        if (Candidate.EconomyLedger.Snapshot().Finalized.contains(Key)) Candidate.FinalizedAccounts[Reservation.Faction].insert(Key);
    OwnerWork W;W.Event=R.Header.Event;W.Initiator=Player;W.Source=R.Header.Object;W.Request=R;W.Result=Result;
    W.Proposal=StableId("GuLiOwnerProposal/v1",Player.ToString()+W.Event.ToString()+std::to_string(NextProposal++));
    auto Keys=ChangedKeys(R,Result);const auto CandidateKeys=Candidate.ChangedKeys(R,Result);Keys.insert(CandidateKeys.begin(),CandidateKeys.end());
    for (const auto F:{Team::Red,Team::Blue})
        if (EconomyLedger.Balance(F).Version!=Candidate.EconomyLedger.Balance(F).Version) Keys.insert({TeamAccountObject(F),"account"});
    for (const auto& K:Keys)
    {
        const auto Before=CaptureFragment(K),After=Candidate.CaptureFragment(K);
        if (Before==After) continue;
        if (After.empty() || Version(K)==UINT64_MAX) return false;
        const auto O=Directory.find(K.Object);const Revision OwnerVersion=O==Directory.end()?0:O->second.Grant.OwnerEpoch;
        // The wire carries the expected version, not a second copy of the value.
        W.Changes.push_back({{K,1,Version(K),{}},{K,1,Version(K)+1,After},OwnerVersion});
    }
    for (const auto& M:W.Changes) if (!M.After.Key.Capability.starts_with("event/"))
        W.Result.RequiredStates.push_back({M.After.Key.Object,M.After.Key.Capability,M.After.Version});
    Candidate.Results[R.Header.Event]=W.Result;
    for (auto& M:W.Changes) if (M.After.Key.Capability=="event/"+R.Header.Event.ToString()) M.After.Value=Candidate.CaptureFragment(M.After.Key);
    std::stable_sort(W.Changes.begin(),W.Changes.end(),[](const auto& A,const auto& B)
    {return A.After.Key.Capability=="object" && B.After.Key.Capability!="object";});
    if (EncodeOwnerWork(W).empty()) return false;Out=std::move(W);return true;
}
bool OwnerState::Prepare(Id Planner,const OwnerWork& W)
{
    TraceScope Scope("GuLiHybrid_CorePrepare");
    if (W.Initiator!=Planner || W.Event!=W.Request.Header.Event || Owner(ExecutionDomain(W.Request))!=Planner || !IsLiveMember(Planner)) return false;
    const auto Encoded=EncodeOwnerWork(W);if (Encoded.empty()) return false;
    if (Cancelled.contains({W.Event,W.Proposal})) return false;
    if (const auto I=Prepared.find(W.Event);I!=Prepared.end() && W.Continuation
        && (I->second.first==Planner || !IsLiveMember(I->second.first)) && I->second.second!=Encoded)
    {OwnerWork Previous;if (!DecodeOwnerWork(I->second.second,Previous)) return false;Cancel(Previous.Initiator,W.Event,Previous.Proposal);}
    if (const auto I=Prepared.find(W.Event);I!=Prepared.end()) return I->second==std::pair{Planner,Encoded};
    if (Finished.contains(W.Event)) return true;
    bool Participates=false;
    for (const auto& M:W.Changes) if (MutationOwner(W,M)==Player)
    {
        Participates=true;const auto A=Directory.find(M.After.Key.Object);
        if (W.Continuation && Version(M.After.Key)>=M.After.Version) continue;
        if ((A==Directory.end()?0:A->second.Grant.OwnerEpoch)!=M.OwnerVersion || Version(M.Before.Key)!=M.Before.Version
            || (Locks.contains(M.Before.Key) && Locks.at(M.Before.Key)!=W.Event)) return false;
    }
    if (!Participates) return false;
    // Decode and validate all local fragments on a staging value before locking.
    std::vector<StateFragment> Local;
    for (const auto& M:W.Changes) if (MutationOwner(W,M)==Player && !(W.Continuation && Version(M.After.Key)>=M.After.Version)) Local.push_back(M.After);
    OwnerState Trial=Stage(nullptr,&Local);
    for (const auto& F:Local) if (!Trial.ApplyFragment(F)) return false;
    for (const auto& M:W.Changes) if (MutationOwner(W,M)==Player && !(W.Continuation && Version(M.After.Key)>=M.After.Version)) Locks[M.Before.Key]=W.Event;
    Prepared.emplace(W.Event,std::pair{Planner,Encoded});return true;
}
bool OwnerState::Commit(Id Planner,const OwnerWork& W)
{
    TraceScope Scope("GuLiHybrid_CoreCommit");
    if (Finished.contains(W.Event)) return true;
    const auto I=Prepared.find(W.Event);
    if (I==Prepared.end() || I->second.first!=Planner || I->second.second!=EncodeOwnerWork(W)) return false;
    std::vector<StateFragment> Local;
    for (const auto& M:W.Changes) if (MutationOwner(W,M)==Player && !(W.Continuation && Version(M.After.Key)>=M.After.Version))
    {
        if (!Locks.contains(M.After.Key) || Locks.at(M.After.Key)!=W.Event) return false;
        Local.push_back(M.After);
    }
    OwnerState Trial=Stage(nullptr,&Local);
    for (const auto& F:Local) if (!Trial.ApplyFragment(F)) return false;
    if (!Trial.RebuildWorkIndex()) return false;
    if (!ApplyValidated(Local)) return false;
    Cancel(Planner,W.Event,W.Proposal);Finished.insert(W.Event);return true;
}
void OwnerState::Cancel(Id Planner,Id Event,Id Proposal)
{
    if (!Proposal.Valid()) return;
    Cancelled.emplace(Event,Proposal);
    const auto I=Prepared.find(Event);if (I==Prepared.end() || I->second.first!=Planner) return;
    OwnerWork Work;if (!DecodeOwnerWork(I->second.second,Work) || Work.Proposal!=Proposal) return;
    for (auto L=Locks.begin();L!=Locks.end();) if (L->second==Event) L=Locks.erase(L);else ++L;
    Prepared.erase(I);
}
bool OwnerState::Resume(OwnerWork& W)
{
    if (Owner(ExecutionDomain(W.Request))!=Player || (W.Initiator!=Player && IsLiveMember(W.Initiator))) return false;
    // A decided operation keeps its values and event identity. Only custody
    // changes; no rule is evaluated a second time during a takeover.
    W.Initiator=Player;W.Continuation=true;W.Proposal=StableId("GuLiOwnerProposal/v1",Player.ToString()+W.Event.ToString()+std::to_string(NextProposal++));
    for (auto& M:W.Changes)
    {
        const auto O=Directory.find(M.After.Key.Object);
        M.OwnerVersion=O==Directory.end()?0:O->second.Grant.OwnerEpoch;
        if (M.After.Key.Capability=="object")
        {
            Reader R(M.After.Value);ObjectRecord V;if (!ReadObject(R,V) || !R.Done()) return false;
            if (O!=Directory.end())
            {const auto LifeState=V.Grant.State;V.Grant=O->second.Grant;V.Grant.State=std::max(LifeState,V.Grant.State);}
            else
            {
                V.Grant.SimOwner=Owner(W.Source);
                if (!V.Grant.SimOwner.Valid()) return false;
                // Result records describe the immutable decision. Current custody
                // belongs in the state fragment, never in a rewritten event result.
            }
            Writer Out;WriteObject(Out,V);M.After.Value=Out.Take();
        }
    }
    return true;
}
bool OwnerState::Import(Id Sender,const std::vector<StateFragment>& Values,bool Handoff,
    const std::map<ObjectKey,Authority>* AdmittedBirths)
{
    TraceScope Scope("GuLiHybrid_CoreImport");
    OwnerState Trial=Stage(nullptr,&Values);std::set<StateKey> Seen;std::vector<StateFragment> Accepted;
    std::vector<const StateFragment*> Ordered;for (const auto& V:Values) Ordered.push_back(&V);
    std::stable_sort(Ordered.begin(),Ordered.end(),[](const auto* A,const auto* B)
    {return A->Key.Capability=="object" && B->Key.Capability!="object";});
    for (const auto* Value:Ordered)
    {
        const auto& V=*Value;
        if (!Seen.insert(V.Key).second) return false;
        // A handoff's final state may already contain this old owner's write.
        // Ignore that duplicate before checking the current write authority.
        if (!Handoff && FindRecord(V.Key.Object) && V.Version<=Version(V.Key)) continue;
        if (Trial.Owner(V.Key.Object)!=Sender)
        {
            if (V.Key.Capability!="object" || Trial.Directory.contains(V.Key.Object)) return false;
            Reader ReaderValue(V.Value);ObjectRecord Birth;
            if (!ReadObject(ReaderValue,Birth) || !ReaderValue.Done() || Birth.Grant.Object!=V.Key.Object
                || Birth.Grant.SimOwner!=Sender || Birth.Grant.OwnerEpoch!=1 || !Birth.Grant.SourceLease.Valid()
                || !Trial.OwnershipDefinitions.contains(Birth.Definition)) return false;
            // A child admitted before its source moved keeps its own owner.
            // The room's exact birth grant permits late delivery independently
            // of the source's current simulator; it cannot grant enemy control.
            bool Admitted=false;
            if (AdmittedBirths)
                if (const auto A=AdmittedBirths->find(Birth.Grant.Object);A!=AdmittedBirths->end())
                    Admitted=A->second.SimOwner==Sender && A->second.OwnerEpoch==Birth.Grant.OwnerEpoch
                        && A->second.SourceLease==Birth.Grant.SourceLease && A->second.Policy==Birth.Grant.Policy
                        && A->second.Faction==Birth.Grant.Faction;
            if (Trial.Owner(Birth.Parent)!=Sender && !Admitted) return false;
            Trial.Directory.emplace(V.Key.Object,Birth);
        }
        if (V.Version<Version(V.Key)) {if (Handoff) return false;continue;}
        if (Locks.contains(V.Key)) return false;
        if (!Trial.ApplyFragment(V)) return false;
        Accepted.push_back(V);
    }
    if (!Trial.RebuildWorkIndex()) return false;
    return ApplyValidated(Accepted);
}
std::optional<CommitResult> OwnerState::Result(Id Event) const
{const auto I=Results.find(Event);return I==Results.end()?std::optional<CommitResult>{}:I->second;}
}
