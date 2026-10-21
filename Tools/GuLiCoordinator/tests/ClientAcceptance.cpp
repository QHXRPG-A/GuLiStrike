#include "GuLiNetClient.h"
#include "GuLiNetAsyncClient.h"
#include "GuLiNetRoom.h"
#include "GuLiNetManifest.h"
#include "GuLiNetTransport.h"
#include "GuLiNetHash.h"
#include "GuLiNetCapability.h"
#include "GuLiNetControlPoint.h"
#include "GuLiNetResourceMaintenance.h"
#include "GuLiNetLinearFlightRecipe.h"
#include "GuLiNetRuntimeEffectRecipe.h"
#include "GuLiNetReplacement.h"
#include <algorithm>
#include <array>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <future>
#include <thread>
#include <atomic>

using namespace GuLiNet;
namespace
{
bool ReuseResourceValidationSnapshot=false;
Id ID(unsigned N) {return {{0x47554c49,0,0,N}};}
void Check(bool Value,const char* Why) {if (!Value) throw std::runtime_error(Why);}
struct Adapter final:IObjectAdapter
{
    bool Local=false;std::uint64_t Position=0,Executions=0,LastTick=0;
    unsigned MainPeriod=1,WorkerPeriod=1;bool WakeOnCommand=false,MainAwake=false;
    mutable unsigned Captures=0;
    std::vector<std::string> SharedSystems;
    Revision AppliedVersion=0;float AppliedHealth=0;
    std::uint32_t Version() const override {return 1;}
    std::vector<std::string> RequiredSystems() const override {return SharedSystems;}
    bool Prepare(const ObjectRecord&) override {return true;}
    void SetAuthority(const Authority&,bool Simulate) override {Local=Simulate;}
    std::uint32_t AdvancePeriod() const override {return WorkerPeriod;}
    bool NeedsAdvanceOnMain(Tick At) const override {return MainAwake || (MainPeriod && At%MainPeriod==0);}
    void Advance(Tick At,double) override {if (Local) {++Position;LastTick=At;MainAwake=false;}}
    bool Command(const FrozenCommand&,const CommandMember&) override {++Executions;MainAwake|=WakeOnCommand;return true;}
    bool ApplyMotion(std::span<const std::uint8_t> Bytes,Tick) override
    {Reader R(Bytes);return R.U64(Position) && R.Done();}
    bool ApplyObjectEvent(std::span<const std::uint8_t> Data,Tick At) override
    {
        if (Data.size()==8) return ApplyMotion(Data,At);
        Reader R(Data);Bytes Payload;
        return R.U64(Position) && R.Blob(Payload) && R.Done();
    }
    void ApplyLedger(const ObjectRecord& O,Revision V,bool) override
    {Check(V>=AppliedVersion,"adapter numeric revision cannot regress");AppliedVersion=V;AppliedHealth=O.Vitals.Value;}
    void BeginResourceRestore(Revision E) override
    {Check(E>0&&!Local,"snapshot version boundary requires fenced simulation");AppliedVersion=0;}
    bool Capture(Bytes& Out) const override {++Captures;Writer W;W.U64(Position);W.U64(Executions);W.U64(LastTick);Out=W.Take();return true;}
    bool Validate(std::span<const std::uint8_t> B) const override {Reader R(B);std::uint64_t V;return R.U64(V)&&R.U64(V)&&R.U64(V)&&R.Done();}
    bool Restore(std::span<const std::uint8_t> B,RestoreStage Stage) override
    {if (Stage!=RestoreStage::Create) return Validate(B);Reader R(B);return R.U64(Position)&&R.U64(Executions)&&R.U64(LastTick)&&R.Done();}
    bool RestoreQuery(std::span<const std::uint8_t> B,RestoreStage S) override {return Restore(B,S);}
    void Release() override {}
};
RuntimeManifest Manifest()
{
    RuntimeManifest M;M.MapPackage="/Game/Maps/Acceptance";M.StartingRed=M.StartingBlue={100,100};
    for (unsigned N=0;N<6;++N)
    {
        ObjectRecord O;O.Grant.Object={ObjectKind::Soldier,ID(20+N),1};O.Grant.Faction=N<3?Team::Red:Team::Blue;
        O.Grant.ControlEpoch=1;O.Grant.EstimatedCost=1;O.Ordinal=N+1;O.Definition="data/unit";O.Vitals={100,100,0,0,1,false};
        M.Objects.emplace(O.Grant.Object,O);
    }
    Rule Hit;Hit.SourceDefinition="data/unit";Hit.Damage=25;Hit.MaxTargets=4;M.Rules["hit"]=Hit;
    Rule Reserve;Reserve.SourceDefinition="data/unit";Reserve.Kind=TransactionKind::Reserve;Reserve.Amount={20,10};M.Rules["reserve"]=Reserve;
    M.Numbers["ownership/definition/data/unit/ownership_policy"]=unsigned(OwnershipPolicy::CommanderActivity);
    M.Numbers["ownership/definition/data/unit/controller_seat"]=unsigned(Role::Commander);
    M.Numbers["ownership/definition/data/unit/retire_on_vacancy"]=0;
    return M;
}
struct Fixture
{
    struct Delivery {Id To,From;Message Value;};
    RuntimeManifest Data=Manifest();std::map<Id,std::unique_ptr<ClientWorld>> Clients;
    std::map<Id,Member> Members;std::map<std::pair<Id,ObjectKey>,Adapter*> Adapters;
    std::deque<Delivery> Queue;std::vector<Delivery> Metadata;double Now=0;bool RejectHandoffQueue=false,RejectResourceSnapshots=false;
    std::set<Id> RejectPublications;
    explicit Fixture(RuntimeManifest Initial=Manifest()):Data(std::move(Initial))
    {
        std::string Error;Check(ValidateManifest(Data,Error),Error.c_str());
        for (unsigned N=0;N<6;++N)
        {Member M;M.Player=ID(N+1);M.Faction=N<3?Team::Red:Team::Blue;M.Seat=Role(N%3);M.Ready=true;M.OrdinalSlot=N+1;Members.emplace(M.Player,M);}
        for (const auto& [P,M]:Members)
        {
            auto World=std::make_unique<ClientWorld>(ID(100),P,1,[this,P](const ObjectRecord& O)
            {auto A=std::make_unique<Adapter>();Adapters[{P,O.Grant.Object}]=A.get();return A;},
            [this,P](Id To,Message V){if (RejectHandoffQueue && V.Header.Kind==MessageKind::HandoffState) return false;
                if (RejectResourceSnapshots && V.Header.Kind==MessageKind::ResourceSnapshot) return false;
                if (RejectPublications.contains(To) && (V.Header.Kind==MessageKind::StateFragments || V.Header.Kind==MessageKind::ObjectEvent)) return false;
                if (To.Valid()) Queue.push_back({To,P,std::move(V)});else Metadata.push_back({{},P,std::move(V)});return true;});
            Check(World->Configure(Data,Error),Error.c_str());World->SetMembers(Members);
            World->SetResourceValidationSnapshotReuse(ReuseResourceValidationSnapshot);
            std::vector<ObjectRecord> Records;for (const auto& [K,O]:World->Objects()) Records.push_back(O);
            for (auto& O:Records)
            {
                const auto Index=O.Ordinal?O.Ordinal:(O.Grant.Faction==Team::Red?1u:4u);
                O.Grant.SimOwner=ID(Index);O.Grant.Controller=ID(O.Grant.Faction==Team::Red?1:4);O.Grant.OwnerEpoch=O.Grant.ControlEpoch=1;O.Grant.State=Life::Alive;
                Check(World->Install(O,true),"install owner grant");
            }
            World->SetMembershipPaused(false);Clients.emplace(P,std::move(World));
        }
    }
    ObjectKey Key(unsigned N) const {return {ObjectKind::Soldier,ID(20+N),1};}
    ClientWorld& Client(unsigned P) {return *Clients.at(ID(P));}
    void Flush()
    {
        unsigned Count=0;
        while (!Queue.empty())
        {
            Check(++Count<100000,"message loop");auto D=std::move(Queue.front());Queue.pop_front();
            if (Members.at(D.To).Online) Check(Clients.at(D.To)->ReceivePeer(D.From,D.Value),"peer message rejected");
        }
    }
    void Step(unsigned N=10)
    {for (unsigned I=0;I<N;++I) {Now+=.1;for (auto& [P,C]:Clients) if (Members.at(P).Online) {C->Advance(1./60);C->PollNetwork(Now);}Flush();}}
};
void IndependentProgressAndValues()
{
    Fixture F;auto& A=F.Client(1);auto& Slow=F.Client(2);
    unsigned Applied=0,FirstReceipt=0,ReplayReceipt=0,ReorderedEffects=0;
    for (auto& [Player,Client]:F.Clients) Client->SetCommitCallback([&](const CommitResult& V)
    {if (V.Event==ID(700)) ++Applied;if (V.Event==ID(702)) ++ReorderedEffects;});
    for (unsigned I=0;I<120;++I) A.Advance(1./60);
    Check(A.Now()>=119 && Slow.Now()==0,"a slow peer cannot hold the local clock");
    A.SetMembershipPaused(true);const auto Paused=A.Now();A.Advance(10);Check(A.Now()==Paused,"membership pauses simulation");A.SetMembershipPaused(false);
    TransactionRequest R;R.Header.Object=F.Key(0);R.Header.Event=ID(700);R.Kind=TransactionKind::Damage;R.Target=F.Key(3);R.RuleId="hit";R.Faction=Team::Red;
    R.StartedAt=A.ObjectNow(R.Header.Object);
    // Deliveries can finish and be rebound without another simulation step.
    // Preserve the original object age as well as the work identity on retry.
    auto NetworkOnly=[&]
    {for (unsigned I=0;I<10;++I) {F.Now+=.1;for (auto& [P,C]:F.Clients) C->PollNetwork(F.Now);F.Flush();}};
    Check(A.Submit(R,[&](const CommitResult& V){Check(V.Status==CommitStatus::Committed,"first receipt is committed");++FirstReceipt;}).Valid(),"attacker submits damage fact");NetworkOnly();
    for (const auto& [P,C]:F.Clients) Check(C->Objects().at(F.Key(3)).Vitals.Value==75,"target owner publishes one health change");
    Check(FirstReceipt==1 && Applied==6 && A.PendingTransactions().empty(),"first commit resolves input and publishes one effect per receiver");
    for (const auto& [P,C]:F.Clients)
    {
        // A room directory refresh carries ownership, not a new numeric revision.
        const auto Before=F.Adapters.at({P,F.Key(3)})->AppliedVersion;
        Check(Before>0 && C->Install(C->Objects().at(F.Key(3)),true),"refresh a damaged actor's existing directory record");
        const auto* AdapterValue=F.Adapters.at({P,F.Key(3)});
        Check(AdapterValue->AppliedVersion==Before && AdapterValue->AppliedHealth==75,
            "metadata refresh preserves the applied health and its current object version");
    }
    Check(A.Submit(R,[&](const CommitResult& V){Check(V.Status==CommitStatus::Duplicate,"rebound receipt retains duplicate delivery status");++ReplayReceipt;}).Valid(),"business duplicate request can be retried");NetworkOnly();
    for (const auto& [P,C]:F.Clients) Check(C->Objects().at(F.Key(3)).Vitals.Value==75,"duplicate damage does not deduct twice");
    Check(ReplayReceipt==1 && Applied==6 && A.PendingTransactions().empty(),"rebound duplicate resolves its pending callback without repeating gameplay effects");
    auto ChangedRequest=R;ChangedRequest.Target=F.Key(4);unsigned RejectedReceipt=0;
    Check(A.Submit(ChangedRequest,[&](const CommitResult& V){Check(V.Status==CommitStatus::BadRequest,"reusing an event with another target is rejected");++RejectedReceipt;}).Valid(),"submit a conflicting duplicate for rejection");NetworkOnly();
    Check(RejectedReceipt==1 && Applied==6 && A.PendingTransactions().empty()
        && F.Client(5).Objects().at(F.Key(4)).Vitals.Value==100,"rejected rebound also resolves without new side effects");
    CommitResult EarlyRejection;EarlyRejection.Event=ID(702);EarlyRejection.Kind=TransactionKind::Damage;
    EarlyRejection.Status=CommitStatus::BadRequest;EarlyRejection.StateDomain=F.Key(4);EarlyRejection.Target=F.Key(4);
    Check(Slow.ApplyCommit(EarlyRejection) && Slow.UnsettledReceiptCount()==1 && ReorderedEffects==0,
        "an unversioned delivery rejection is retained separately without consuming a gameplay completion");
    auto Later=R;Later.Header.Event=ID(702);Later.Target=F.Key(4);unsigned LaterReceipt=0;
    Check(A.Submit(Later,[&](const CommitResult& V){Check(V.Status==CommitStatus::Committed,"later real decision commits");++LaterReceipt;}).Valid(),
        "submit the real event after a receiver saw only its malformed delivery rejection");NetworkOnly();
    Check(LaterReceipt==1 && ReorderedEffects==6 && Slow.PendingTransactions().empty(),
        "the real committed event still applies once on every receiver after an early rejection");
    for (const auto& [P,C]:F.Clients) Check(C->Objects().at(F.Key(4)).Vitals.Value==75,
        "an early rejection cannot suppress the later authoritative health change");
    TransactionRequest E;E.Header.Object=F.Key(1);E.Header.Event=ID(701);E.Kind=TransactionKind::Reserve;E.RuleId="reserve";E.Faction=Team::Red;
    E.ReservationKey={DeriveEconomyAccount(E.Header.Object),1};Check(F.Client(2).Submit(E,{}).Valid(),"remote owner submits team resource request");F.Step();
    for (const auto& [P,C]:F.Clients) Check(C->NumericState().TeamEconomy().Balance(Team::Red)==Resources{80,90,2},"team owner result and incremented account version reach all peers");
    const std::array<ObjectKey,1> SourceKeys{F.Key(1)},AccountKeys{TeamAccountObject(Team::Red)};
    const auto SourceHistory=F.Client(2).NumericState().Export(SourceKeys);
    const auto AccountHistory=F.Client(1).NumericState().Export(AccountKeys);
    const auto Has=[](const auto& Parts,const std::string& Name)
    {return std::any_of(Parts.begin(),Parts.end(),[&](const auto& Part){return Part.Key.Capability==Name && !Part.Value.empty();});};
    Check(Has(SourceHistory,"attack/"+ID(701).ToString()) && Has(SourceHistory,"cooldown/reserve")
        && !Has(SourceHistory,"event/"+ID(701).ToString()) && Has(AccountHistory,"event/"+ID(701).ToString()),
        "single-object export retains actual cross-owner attack/cooldown history and keeps its result on the account domain");
}
void ScopedHandoff()
{
    Fixture F;F.Step();const auto K=F.Key(1);auto& Old=F.Client(2);auto& New=F.Client(1);
    auto* Unrelated=F.Adapters.at({ID(1),F.Key(0)});const auto Before=Unrelated->Position;
    ObjectHandoff T;T.IdValue=1;T.Object=K;T.Source=ID(2);T.Target=ID(1);T.PreviousOwnerVersion=1;T.NextOwnerVersion=2;T.TargetPrepared=true;
    for (auto& [P,C]:F.Clients) Check(C->PrepareHandoff(T),"prepare scoped object handoff");
    Check(Old.FreezeHandoff(T),"old owner freezes only transferred object");F.Flush();
    const auto Age=Old.ObjectNow(K);F.Step(3);Check(Old.ObjectNow(K)==Age,"source stopped the transferred object");
    Check(Unrelated->Position>Before,"unrelated object keeps advancing during handoff");
    auto Grant=Old.Objects().at(K).Grant;Grant.SimOwner=ID(1);Grant.OwnerEpoch=2;
    for (auto& [P,C]:F.Clients) Check(C->CommitHandoff(T,Grant),"ownership commit");
    Check(New.ObjectNow(K)==Age && New.HasSimulation(K) && !Old.HasSimulation(K),"logical age moves with unique ownership");
    F.Step();Check(New.ObjectNow(K)>Age,"new owner independently continues the object");
    Message Late;Late.Header.Kind=MessageKind::Motion;Late.Header.Match=ID(100);Late.Header.Epoch=1;Late.Header.Sender=ID(2);
    Late.Header.Object=K;Late.Header.OwnerEpoch=1;Late.Header.At=999;Writer W;W.U64(999);Late.Body=W.Take();
    Check(!New.ReceivePeer(ID(2),Late),"old owner cannot overwrite the new simulation");

    // Publication batching does not combine independent private cuts or change
    // their transfer identities, sources, or target install acknowledgements.
    std::vector<std::pair<ObjectHandoff,Authority>> Independent;
    for (unsigned N:{2u,4u})
    {
        const auto Key=F.Key(N);const auto Source=F.Client(1).Objects().at(Key).Grant.SimOwner;
        ObjectHandoff V;V.IdValue=10+N;V.Object=Key;V.Source=Source;V.Target=ID(1);
        V.PreviousOwnerVersion=1;V.NextOwnerVersion=2;V.TargetPrepared=true;
        for (auto& [P,C]:F.Clients) Check(C->PrepareHandoff(V),"prepare independent private transfer");
        Check(F.Clients.at(Source)->FreezeHandoff(V),"freeze each independent private cut");
        auto A=F.Clients.at(Source)->Objects().at(Key).Grant;A.SimOwner=ID(1);A.OwnerEpoch=2;
        Independent.emplace_back(V,A);
    }
    F.Flush();
    for (auto& [P,C]:F.Clients) Check(C->CommitHandoffs(Independent),"publish independently installed grants in one existing commit envelope");
    F.Flush();
    for (const auto& [V,A]:Independent)
        Check(F.Client(1).HasSimulation(V.Object) && !F.Clients.at(V.Source)->HasSimulation(V.Object),
            "batched publication retains unique authority for each independent transfer");

    // A shared group receives one private cut despite independently ordered
    // member prepares. Cancellation and commit detach that cut only once.
    Fixture G;std::vector<ObjectKey> GroupKeys{G.Key(1)};
    for (unsigned N=0;N<2;++N)
    {
        auto O=G.Client(2).Objects().at(G.Key(1));O.Grant.Object.StableId=ID(5500+N);O.Ordinal=100+N;
        for (auto& [P,C]:G.Clients) Check(C->Install(O,true),"declare additional private group member");
        GroupKeys.push_back(O.Grant.Object);
    }
    struct SharedCounter final:ISharedStateAdapter
    {
        std::vector<ObjectKey> Keys;unsigned Detaches=0;
        std::uint32_t Version() const override {return 1;}
        bool UsesObject(ObjectKey K) const override {return std::find(Keys.begin(),Keys.end(),K)!=Keys.end();}
        bool Capture(const std::vector<ObjectKey>& Objects,Bytes& Out) const override
        {Writer W;W.U32(static_cast<std::uint32_t>(Objects.size()));Out=W.Take();return true;}
        bool Validate(const std::vector<PrivateSystemState>& Parts,const std::vector<ObjectKey>& Objects) const override
        {std::uint32_t N;return Parts.size()==1 && Parts.front().Objects==Objects && [&]{Reader R(Parts.front().State);return R.U32(N) && N==Objects.size() && R.Done();}();}
        bool Restore(const std::vector<PrivateSystemState>& Parts,const std::vector<ObjectKey>& Objects,RestoreStage Stage) override
        {if (!Validate(Parts,Objects)) return false;if (Stage==RestoreStage::Detach) ++Detaches;return true;}
    };
    auto SourceSystem=std::make_unique<SharedCounter>();SourceSystem->Keys=GroupKeys;auto* SourceCount=SourceSystem.get();
    auto TargetSystem=std::make_unique<SharedCounter>();TargetSystem->Keys=GroupKeys;auto* TargetCount=TargetSystem.get();
    Check(G.Client(2).RegisterSystem("scoped-group",std::move(SourceSystem)) && G.Client(1).RegisterSystem("scoped-group",std::move(TargetSystem)),"register group private capability");
    for (unsigned Round=0;Round<2;++Round)
    {
        std::vector<ObjectHandoff> Batch;
        for (std::size_t I=0;I<GroupKeys.size();++I)
        {
            ObjectHandoff V;V.IdValue=900+Round*10+I;V.Object=GroupKeys[I];V.Source=ID(2);V.Target=ID(1);V.Group=ID(5600+Round);
            V.PreviousOwnerVersion=1;V.NextOwnerVersion=2;Batch.push_back(V);
            for (auto& [P,C]:G.Clients) if (P!=ID(1) || I==0) Check(C->PrepareHandoff(V),"prepare independently ordered group member");
        }
        bool CaptureReady=false;unsigned CaptureAttempts=0;
        G.Client(2).SetApplicationBatches({},[&](std::span<const ObjectKey> Keys,bool)
        {
            ++CaptureAttempts;Check(Keys.size()==GroupKeys.size(),"pending group retains complete ordered cut");
            return CaptureReady?ClientWorld::CaptureApplicationStatus::Ready:ClientWorld::CaptureApplicationStatus::Pending;
        },{});
        for (auto V:Batch) {V.TargetPrepared=true;Check(G.Client(2).FreezeHandoff(V),"freeze group at admitted boundary");}
        CaptureAttempts=0;G.Client(2).PollNetwork(G.Now);
        Check(CaptureAttempts==1,"one pending capture retry per group per owner turn");
        Check(G.Client(2).WorkCounts().at("freeze_requested")==GroupKeys.size(),"pending capture keeps every member runnable");
        CaptureReady=true;G.Client(2).PollNetwork(G.Now);
        Check(CaptureAttempts==2 && G.Client(2).WorkCounts().at("freeze_requested")==0,"ready cut completes without duplicate member capture");
        G.Client(2).SetApplicationBatches({},{},{});
        Check(std::count_if(G.Queue.begin(),G.Queue.end(),[](const auto& D){return D.Value.Header.Kind==MessageKind::HandoffState;})==1,"one shared cut serves all member identities");
        ObjectHandoffState OriginalCut;
        const auto Packet=std::find_if(G.Queue.begin(),G.Queue.end(),[](const auto& D){return D.Value.Header.Kind==MessageKind::HandoffState;});
        Check(DecodeHandoffState(Packet->Value.Body,OriginalCut),"retain the exact private group cut for duplicate validation");
        G.Flush();Check(TargetCount->Detaches==Round*2,"early cut waits for every member prepare");
        bool ImportReady=false;unsigned ImportAttempts=0;std::map<ObjectKey,std::uint64_t> ApplicationProgress;
        G.Client(1).SetApplicationProgressRevision([&](ObjectKey K){return ApplicationProgress[K];});
        G.Client(1).SetApplicationBatches([&](auto,auto)
        {++ImportAttempts;return ImportReady?ClientWorld::ApplicationStatus::Ready:ClientWorld::ApplicationStatus::Pending;},{},{});
        for (std::size_t I=1;I<Batch.size();++I) Check(G.Client(1).PrepareHandoff(Batch[I]),"non-leader prepare releases the early cut");
        const auto AttemptsBefore=ImportAttempts;
        const auto EncodingsBefore=G.Client(1).HandoffMeasurements().at("receive/canonical_encodings");
        for (unsigned I=0;I<10;++I) G.Client(1).PollNetwork(G.Now);
        Check(G.Client(1).HandoffMeasurements().at("receive/canonical_encodings")==EncodingsBefore,
            "pending engine replies retain the exact immutable cut without recoding its history");
        auto ChangedPending=OriginalCut;ChangedPending.Objects.front().State.front()^=1;
        Check(!G.Client(1).InstallHandoff(ID(2),ChangedPending),"a pending transfer rejects changed private state before application");
        Check(ImportAttempts==AttemptsBefore,
            "reduced work waits for a real application reply instead of rechecking the retained cut");
        const auto AttemptsAfterPoll=ImportAttempts;
        ++ApplicationProgress[G.Key(0)];G.Client(1).PollNetwork(G.Now);
        Check(ImportAttempts==AttemptsAfterPoll,"an unrelated application reply cannot wake this private cut");
        ++ApplicationProgress[GroupKeys.front()];G.Client(1).PollNetwork(G.Now);
        Check(ImportAttempts>AttemptsBefore,"application invalidation wakes the retained cut immediately");
        ImportReady=true;++ApplicationProgress[GroupKeys.front()];G.Client(1).PollNetwork(G.Now);
        G.Client(1).SetApplicationBatches({},{},{});G.Client(1).SetApplicationProgressRevision({});
        G.Flush();Check(TargetCount->Detaches==Round*2+1,"target restores the shared cut once");
        Check(G.Client(1).InstallHandoff(ID(2),OriginalCut) && TargetCount->Detaches==Round*2+1,
            "identical group replay acknowledges without applying private state twice");
        auto ChangedCut=OriginalCut;ChangedCut.Objects.back().State.push_back(0x7f);
        Check(!G.Client(1).InstallHandoff(ID(2),ChangedCut) && TargetCount->Detaches==Round*2+1,
            "a changed later group member is rejected through repeated installation");
        const auto Age=G.Client(2).ObjectNow(GroupKeys.front());G.Step(2);
        for (const auto& Key:GroupKeys) Check(!G.Client(1).HasSimulation(Key) && G.Client(2).ObjectNow(Key)==Age,"all cut members remain frozen before commit");
        if (!Round)
        {
            for (auto& [P,C]:G.Clients) for (const auto& V:Batch) C->CancelHandoff(V);
            Check(TargetCount->Detaches==2 && SourceCount->Detaches==0,"cancel detaches imported shared work only once");
            G.Step();for (const auto& Key:GroupKeys) Check(G.Client(2).HasSimulation(Key),"cancel restores original group authority");
        }
        else
        {
            std::vector<std::pair<ObjectHandoff,Authority>> Commit;
            for (const auto& V:Batch) {auto A=G.Client(2).Objects().at(V.Object).Grant;A.SimOwner=ID(1);A.OwnerEpoch=2;Commit.emplace_back(V,A);}
            for (auto& [P,C]:G.Clients) Check(C->CommitHandoffs(Commit),"commit every acknowledged group member together");
            Check(SourceCount->Detaches==1,"source shared work detaches once at commit");
            G.Flush();for (const auto& Key:GroupKeys) Check(G.Client(1).HasSimulation(Key) && !G.Client(2).HasSimulation(Key),"retry grants unique group authority");
        }
    }
    {
        Fixture Birth;const ObjectKey Retired{ObjectKind::BusinessWork,ID(5800),1};
        auto Child=Birth.Client(1).Objects().at(Birth.Key(0));Child.Grant.Object.StableId=ID(5801);
        Child.Parent=Retired;Child.Ordinal=700;Child.Grant.SourceLease=ID(5802);
        Check(Birth.Client(1).Install(Child,true),"source retains a real child with historical placement provenance");
        ObjectHandoffState Cut;const std::array<ObjectKey,1> Keys{Child.Grant.Object};
        Check(Birth.Client(1).CaptureObjects(Keys,ID(2),0,Cut),"capture child while retired work has no private cut");
        auto Join=[&](std::optional<Life> ParentLife)
        {
            auto C=std::make_unique<ClientWorld>(ID(100),ID(2),1,[](const ObjectRecord&){return std::make_unique<Adapter>();},[](Id,Message){return true;});
            std::string Error;Check(C->Configure(Birth.Data,Error),"joining child directory configuration");C->SetMembers(Birth.Members);
            if (ParentLife)
            {Authority A;A.Object=Retired;A.State=*ParentLife;A.OwnerEpoch=1;Check(C->InstallGrant(A),"coordinator parent lifecycle arrives before numeric state");}
            Check(C->StageBootstrapDirectory({&Cut,1}),"stage actual child before linking its private state");return C;
        };
        auto Unknown=Join({});Check(!Unknown->ImportBootstrap({&Cut,1}),"unknown missing parent still rejects");
        auto Alive=Join(Life::Alive);Check(!Alive->ImportBootstrap({&Cut,1}),"missing active placement work still rejects");
        auto Destroyed=Join(Life::Destroyed);
        Check(Destroyed->ImportBootstrap({&Cut,1}) && Destroyed->Objects().at(Child.Grant.Object).Parent==Retired
            && !Destroyed->Objects().contains(Retired) && !Destroyed->HasSimulation(Child.Grant.Object),
            "known retired placement provenance imports without inventing a parent or simulation authority");
    }
}
void FrozenFlightAdmissionAndRequestTime()
{
    namespace Recipe=GuLiNet::LinearFlightRecipe;
    ObjectRecord Source;Source.Grant.Object={ObjectKind::Soldier,ID(5700),1};Source.Definition="data/unit";
    ObjectRecord Child;Child.Grant.Object={ObjectKind::Flight,ID(5701),1};Child.Parent=Source.Grant.Object;
    Child.Grant.SourceLease=ID(5702);Child.Definition="effect/linear/test";
    TransactionRequest R;R.Kind=TransactionKind::Spawn;R.Header.Object=Source.Grant.Object;R.Header.Event=Child.Grant.SourceLease;R.Target=Child.Grant.Object;
    const std::map<ObjectKey,ObjectRecord> Objects{{Source.Grant.Object,Source}};
    auto Missing=Child;Check(!DeriveSpawnCapabilities(Source,Objects,R,Missing),"a linear birth cannot commit without its original recipe");
    Recipe::Recipe P{Child.Grant.Object,Source.Grant.Object,Child.Grant.SourceLease,{1,2,3,4}};
    R.CapabilityPayload=Recipe::Encode(P);Check(DeriveSpawnCapabilities(Source,Objects,R,Child),"child-family admission does not require the producer to carry that capability");
    Recipe::Recipe Read;Check(Recipe::Decode(Child.Capabilities.at(Recipe::Name).Data,Read) && Read.State==P.State,"core preserves the adapter's exact opaque recipe bytes");
    auto Wrong=Child;Wrong.Parent.StableId=ID(5703);Wrong.Capabilities.clear();Check(!DeriveSpawnCapabilities(Source,Objects,R,Wrong),"recipe cannot move to a different parent");
    Wrong=Child;Wrong.Grant.SourceLease=ID(5704);Wrong.Capabilities.clear();Check(!DeriveSpawnCapabilities(Source,Objects,R,Wrong),"recipe cannot change its admission event");
    auto Changed=Child;Changed.Capabilities.at(Recipe::Name).Data.back()^=1;
    const auto* Contract=FindCapability(Recipe::Name);Check(Contract && !Contract->ValidateTransition(Child,Changed),"committed launch recipe is immutable");
    TransactionRequest Decoded;Check(DecodeTransaction(R.Header,EncodeTransaction(R),Decoded) && Decoded.CapabilityPayload==R.CapabilityPayload,"spawn payload survives the real transaction decoder");
    auto Unclaimed=Child;Unclaimed.Definition="data/unit";Unclaimed.Grant.Object.Kind=ObjectKind::Soldier;Unclaimed.Capabilities.clear();
    Check(!DeriveSpawnCapabilities(Source,Objects,R,Unclaimed),"unclaimed spawn payload cannot be silently ignored");
    RuntimeManifest M;Rule Spawn;Spawn.Kind=TransactionKind::Spawn;Spawn.TargetDefinition=Child.Definition;M.Rules["linear"]=Spawn;
    std::string Error;Check(InitializeManifestCapabilities(M,Error) && M.Numbers.at("capability/linear_flight_recipe/version")==1,"recipe compatibility version enters the manifest hash input");

    auto BirthManifest=Manifest();Spawn.SourceDefinition="data/unit";Spawn.SpawnKind=ObjectKind::Flight;
    Spawn.SpawnCost=.001;Spawn.MaxEffectLifetime=600;BirthManifest.Rules["linear"]=Spawn;
    const std::string Policy="ownership/definition/"+Child.Definition+"/";
    BirthManifest.Numbers[Policy+"ownership_policy"]=unsigned(OwnershipPolicy::RetainedSource);
    BirthManifest.Numbers[Policy+"controller_seat"]=unsigned(Role::Observer);BirthManifest.Numbers[Policy+"retire_on_vacancy"]=0;
    Fixture Birth(BirthManifest);TransactionRequest Admission;Admission.Header.Object=Birth.Key(0);Admission.Header.Event=ID(5715);
    Admission.Kind=TransactionKind::Spawn;Admission.RuleId="linear";Admission.Faction=Team::Red;Admission.Target={ObjectKind::Flight,ID(5716),1};
    P={Admission.Target,Admission.Header.Object,Admission.Header.Event,{1,2,3,4}};Admission.CapabilityPayload=Recipe::Encode(P);
    Check(Birth.Client(1).Submit(Admission,{}).Valid(),"production client accepts a real birth request with an original recipe");Birth.Step();
    for (const auto& [Player,Client]:Birth.Clients)
        Check(Client->Objects().contains(Admission.Target) && Client->Objects().at(Admission.Target).Capabilities.contains(Recipe::Name),"birth and immutable recipe commit and publish together on every client");
    Authority Retired;Retired.Object={ObjectKind::Flight,ID(5725),1};Retired.State=Life::Destroyed;Retired.OwnerEpoch=2;
    Check(Birth.Client(1).InstallGrant(Retired,(1u<<24)|37u) && !Birth.Client(1).Objects().contains(Retired.Object)
        && Birth.Client(1).NumericState().LastRetiredLife(ObjectKind::Flight,ID(5725))==1,
        "terminal metadata records life history without inventing numeric state");
    Admission.Header.Event=ID(5726);Admission.Target={ObjectKind::Flight,ID(5725),2};
    P={Admission.Target,Admission.Header.Object,Admission.Header.Event,{1,2,3,4}};Admission.CapabilityPayload=Recipe::Encode(P);
    Check(Birth.Client(1).Submit(Admission,{}).Valid(),"new life can be submitted from retained terminal metadata");Birth.Step();
    for (const auto& [Player,Client]:Birth.Clients)
        Check(Client->Objects().contains(Admission.Target) && Client->Objects().at(Admission.Target).Grant.Object.Generation==2,
            "new positive generation really commits and publishes after missing old private state");
    Check((Birth.Client(1).Objects().at(Admission.Target).Ordinal&0xffffffu)>37,
        "rejoining planner does not reuse a historical ordinal absent from its numeric directory");

    namespace RuntimeRecipe=GuLiNet::RuntimeEffectRecipe;
    auto EffectManifest=Manifest();Spawn.TargetDefinition="effect/runtime/test";Spawn.SpawnKind=ObjectKind::Field;
    EffectManifest.Rules["runtime"]=Spawn;const std::string EffectPolicy="ownership/definition/effect/runtime/test/";
    EffectManifest.Numbers[EffectPolicy+"ownership_policy"]=unsigned(OwnershipPolicy::RetainedSource);
    EffectManifest.Numbers[EffectPolicy+"controller_seat"]=unsigned(Role::Observer);EffectManifest.Numbers[EffectPolicy+"retire_on_vacancy"]=0;
    Fixture Effects(EffectManifest);Admission.Header.Object=Effects.Key(0);Admission.Header.Event=ID(5717);
    Admission.RuleId="runtime";Admission.Target={ObjectKind::Field,ID(5718),1};
    RuntimeRecipe::Recipe EffectRecipe{Admission.Target,Admission.Header.Object,Admission.Header.Event,{5,6,7,8}};
    Admission.CapabilityPayload=RuntimeRecipe::Encode(EffectRecipe);
    Check(Effects.Client(1).Submit(Admission,{}).Valid(),"runtime effect uses the same production birth request path");Effects.Step();
    for (const auto& [Player,Client]:Effects.Clients)
        Check(Client->Objects().contains(Admission.Target) && Client->Objects().at(Admission.Target).Capabilities.contains(RuntimeRecipe::Name),"runtime effect and original recipe publish atomically to every client");
    auto BadEffect=Effects.Client(1).Objects().at(Admission.Target);BadEffect.Parent.StableId=ID(5719);
    Check(!FindCapability(RuntimeRecipe::Name)->ValidateState(BadEffect,BadEffect.Capabilities.at(RuntimeRecipe::Name)),"runtime recipe cannot be rebound to another source");

    Fixture F;F.Step(20);const auto K=F.Key(1);TransactionRequest Work;Work.Header.Object=K;Work.Header.Event=ID(5710);
    Work.Header.At=Work.StartedAt=5;Work.Kind=TransactionKind::Reserve;Work.RuleId="reserve";Work.Faction=Team::Red;
    Work.ReservationKey={DeriveEconomyAccount(K),1};
    Check(F.Client(2).Submit(Work,{},true).Valid(),"resume an original request without replacing its logical time");
    Check(F.Client(2).PendingTransactions().front().Header.At==5,"queued resume retains the original request fingerprint time");
    F.Step();const auto Before=F.Client(2).DuplicateResults();const auto Balance=F.Client(1).NumericState().TeamEconomy().Balance(Team::Red);
    Check(F.Client(2).Submit(Work,{},true).Valid(),"resubmit the same original request at a later current clock");F.Step();
    Check(F.Client(2).DuplicateResults()==Before+1 && F.Client(1).NumericState().TeamEconomy().Balance(Team::Red)==Balance,"original time replay deduplicates without another debit");
    Work.Header.Event=ID(5711);Work.Header.At=F.Client(2).ObjectNow(K)+1;
    Check(!F.Client(2).Submit(Work,{},true).Valid(),"resume cannot claim a future source-object age");
    // A queued admitted request must retain the same time on both wire layers.
    Work.Header.Event=ID(5720);Work.Header.At=Work.StartedAt=5;Work.Kind=TransactionKind::Process;Work.Target=K;
    Check(F.Client(2).SubmitAdmitted(Work,true).Valid(),"queue an admitted resource fact after its original source age");
    const auto AdmissionRequest=F.Client(2).PendingTransactions().back();Message OriginalMessage;TransactionRequest OriginalRequest;
    Check(DecodeMessage(AdmissionRequest.Admission,OriginalMessage) && DecodeTransaction(OriginalMessage.Header,OriginalMessage.Body,OriginalRequest),"decode the queued admission's original request");
    Check(AdmissionRequest.Header.At==OriginalMessage.Header.At && OriginalMessage.Header.At==5 && OriginalRequest.StartedAt==5,"queued admission wrapper and frozen fact retain one original clock boundary");
}
void CommittedPublicationBackpressure()
{
    Fixture F;F.Step();F.RejectPublications.insert(ID(6));unsigned Receipts=0;
    F.Client(6).SetCommitCallback([&](const CommitResult& R){if (R.Event==ID(710)) ++Receipts;});
    TransactionRequest R;R.Header.Object=F.Key(0);R.Header.Event=ID(710);R.Kind=TransactionKind::Damage;
    R.Target=F.Key(3);R.RuleId="hit";R.Faction=Team::Red;
    Check(F.Client(1).Submit(R,{}).Valid(),"submit with one observer's publication queue full");F.Step();
    Check(F.Client(4).Objects().at(F.Key(3)).Vitals.Value==75 && F.Client(6).Objects().at(F.Key(3)).Vitals.Value==100,
        "backpressure does not undo or stop the owner commit");
    Check(F.Client(4).WorkCounts().at("publication_messages")>0 && Receipts==0,"committed state and its receipt remain pending publication");
    const auto Age=F.Client(6).ObjectNow(F.Key(5));F.Step();
    Check(F.Client(6).ObjectNow(F.Key(5))>Age && !F.Client(6).IsPaused(),"network pressure never pauses independent simulation");
    F.RejectPublications.clear();F.Step();
    Check(F.Client(6).Objects().at(F.Key(3)).Vitals.Value==75 && Receipts==1,"queue capacity returning delivers missing state and one result");
    for (const auto& [P,C]:F.Clients) Check(C->WorkCounts().at("publication_messages")==0,"publication backlog drains");
}
void PendingWorkStaysAtFrozenBoundary()
{
    Fixture F;F.Step();const auto K=F.Key(1);auto& Old=F.Client(2);auto& Next=F.Client(1);
    TransactionRequest R;R.Header.Object=K;R.Header.Event=ID(711);R.Kind=TransactionKind::Reserve;
    R.RuleId="reserve";R.Faction=Team::Red;R.ReservationKey={DeriveEconomyAccount(K),1};
    Check(Old.Submit(R,{}).Valid(),"queue pending work immediately before handoff");
    ObjectHandoff T;T.IdValue=71;T.Object=K;T.Source=ID(2);T.Target=ID(1);T.PreviousOwnerVersion=1;T.NextOwnerVersion=2;T.TargetPrepared=true;
    for (auto& [P,C]:F.Clients) Check(C->PrepareHandoff(T),"prepare pending-work handoff");
    Check(Old.FreezeHandoff(T),"capture pending work at the object boundary");F.Flush();F.Step();
    Check(Old.PendingTransactions().size()==1 && Next.PendingTransactions().size()==1,
        "both custodians retain the same pending request before commit");
    Check(Next.NumericState().TeamEconomy().Balance(Team::Red)==Resources{100,100,1},
        "neither frozen custodian executes the transferred request early");
    auto Grant=Old.Objects().at(K).Grant;Grant.SimOwner=ID(1);Grant.OwnerEpoch=2;
    for (auto& [P,C]:F.Clients) Check(C->CommitHandoff(T,Grant),"commit the pending-work owner");
    F.Step();
    Check(Old.PendingTransactions().empty() && Next.PendingTransactions().empty()
        && Next.NumericState().TeamEconomy().Balance(Team::Red)==Resources{80,90,2},
        "new owner executes transferred work once after commit");
}
void SlowObserverCannotBlockOwnerEconomy()
{
    Fixture F;F.Step();F.RejectPublications.insert(ID(6));auto& Owner=F.Client(1);
    Writer W;W.U64(77);W.Blob(Bytes(512*1024,42));const auto Payload=W.Take();
    for (unsigned N=0;N<33;++N)
    {Check(Owner.PublishObjectEvent(F.Key(0),ID(5000+N),Payload),"retain a reliable capability event for a slow observer");F.Flush();}
    Check(Owner.WorkCounts().at("publication_bytes")>16*1024*1024,"reproduce the former source-wide backlog threshold");
    TransactionRequest R;R.Header.Object=F.Key(0);R.Header.Event=ID(5100);R.Kind=TransactionKind::Reserve;
    R.RuleId="reserve";R.Faction=Team::Red;R.ReservationKey={DeriveEconomyAccount(R.Header.Object),1};
    unsigned Applied=0;Check(Owner.Submit(R,[&](const auto& V){Check(V.Status==CommitStatus::Committed,"local economy commits");++Applied;}).Valid(),"request resources while an observer is slow");
    F.Step();Check(Applied==1 && Owner.PendingTransactions().empty()
        && Owner.NumericState().TeamEconomy().Balance(Team::Red)==Resources{80,90,2},
        "an observer's publication backlog cannot gate the resource owner's local work");
    F.RejectPublications.clear();F.Step();
    Check(Owner.WorkCounts().at("publication_messages")==0 && Applied==1
        && F.Client(6).NumericState().TeamEconomy().Balance(Team::Red)==Resources{80,90,2},
        "the slow observer catches up without repeating the resource operation");
}
void TargetOwnerSettlesDamageIndependently()
{
    Fixture F;F.Step();auto& Source=F.Client(1);auto& Target=F.Client(4);
    TransactionRequest R;R.Header.Object=F.Key(0);R.Header.Event=ID(712);R.RootEvent=ID(712);
    R.Kind=TransactionKind::Damage;R.Target=F.Key(3);R.RuleId="hit";R.Faction=Team::Red;
    Check(Source.Submit(R,{}).Valid(),"submit an attacker-owned hit fact");Source.PollNetwork(F.Now+1);
    const auto I=std::find_if(F.Queue.begin(),F.Queue.end(),[&](const auto& D){return D.To==ID(4) && D.Value.Header.Event==R.Header.Event;});
    Check(I!=F.Queue.end(),"hit fact is routed to the victim owner");auto Delivery=std::move(*I);F.Queue.erase(I);
    Check(Target.ReceivePeer(Delivery.From,Delivery.Value),"target receives the authorized hit fact");
    Check(Target.Objects().at(R.Target).Vitals.Value==75 && Target.WorkCounts().at("active")==0,
        "ordinary damage settles locally without an attacker prepare/commit round trip");
    Check(std::none_of(F.Queue.begin(),F.Queue.end(),[](const auto& D){return D.Value.Header.Kind==MessageKind::WorkPrepare;}),
        "ordinary hit does not open a distributed source transaction");
    F.Now+=1;F.Flush();F.Step();
    auto Repeated=R;Repeated.Header.Event=ID(713);Check(Source.Submit(Repeated,{}).Valid(),"replay the same hit with another delivery identity");F.Step();
    Check(Target.Objects().at(R.Target).Vitals.Value==75,"same source/root/target cannot deduct twice");
    auto Other=R;Other.Header.Event=ID(714);Other.Target=F.Key(4);Check(Source.Submit(Other,{}).Valid(),"one effect can hit another owner-held target");F.Step();
    Check(F.Client(5).Objects().at(Other.Target).Vitals.Value==75,"target-local deduplication does not swallow another victim's hit");
}
void HandoffBackpressureKeepsSourceRunning()
{
    Fixture F;F.Step();const auto K=F.Key(1);auto& Old=F.Client(2);auto& New=F.Client(1);
    ObjectHandoff T;T.IdValue=1;T.Object=K;T.Source=ID(2);T.Target=ID(1);T.PreviousOwnerVersion=1;T.NextOwnerVersion=2;T.TargetPrepared=true;
    for (auto& [P,C]:F.Clients) Check(C->PrepareHandoff(T),"prepare backpressured handoff");
    bool Ready=false;Old.SetHandoffAdmission([&](Id Peer){Check(Peer==T.Target,"admission checks destination");return Ready;});
    auto Age=Old.ObjectNow(K);Check(Old.FreezeHandoff(T),"defer capture before queue admission");F.Step(3);
    Check(Old.HasSimulation(K) && Old.ObjectNow(K)>Age && !New.HasSimulation(K),"queue pressure only delays transfer, not existing simulation");
    Ready=true;F.RejectHandoffQueue=true;Age=Old.ObjectNow(K);Check(Old.FreezeHandoff(T),"queue may reject after preflight");F.Step(3);
    Check(Old.HasSimulation(K) && Old.ObjectNow(K)>Age,"failed queue admission must not freeze an unsent object");
    F.RejectHandoffQueue=false;Check(Old.FreezeHandoff(T),"retry admission at a new object boundary");F.Flush();
    Age=Old.ObjectNow(K);Check(!Old.HasSimulation(K),"successful queue admission freezes final state");
    auto Grant=Old.Objects().at(K).Grant;Grant.SimOwner=ID(1);Grant.OwnerEpoch=2;
    for (auto& [P,C]:F.Clients) Check(C->CommitHandoff(T,Grant),"commit admitted state");
    Check(New.HasSimulation(K) && New.ObjectNow(K)==Age && !Old.HasSimulation(K),"one new owner continues the current, not the rejected snapshot");
}
void BoundedHandoffLaneAdmission()
{
    QueueLimits Limits;Limits.BytesPerSecond[3]=4000;
    PeerChannel Tx(ID(80),Limits),Rx(ID(80),Limits);std::vector<Bytes> Acks;std::vector<Id> Received;
    Message M;M.Header.Kind=MessageKind::HandoffState;M.Header.Match=ID(100);M.Header.Epoch=1;
    M.Header.Sender=ID(1);M.Header.Object={ObjectKind::Soldier,ID(20),1};M.Header.Event=ID(1500);M.Body=Bytes(1800,1);
    Check(Tx.CanQueueHandoff() && Tx.Queue(M,0),"one snapshot larger than the admission window enters an empty lane");
    Check(!Tx.HasTransmittedReliable(Lane::Handoff,1),"queued birth/state is not yet on the wire");
    auto Next=M;Next.Header.Event=ID(1501);
    Check(!Tx.CanQueueHandoff() && !Tx.Queue(Next,0) && Tx.SentThrough(Lane::Handoff)==1,
        "backpressure does not allocate a reliable sequence or stack another state");
    for (unsigned I=0;I<=20;++I)
    {
        const double Now=I*.1;
        Tx.Pump(Now,[&](std::span<const std::uint8_t> B){Check(B.size()<=1000,"handoff keeps datagram bound");
            return Rx.Receive(B,Now,[&](auto A){Acks.emplace_back(A.begin(),A.end());return true;},[&](Message V){Received.push_back(V.Header.Event);});});
        if (!I) Check(!Tx.HasTransmittedReliable(Lane::Handoff,1),"partial first transmission cannot release metadata");
        if (!Acks.empty()) Check(Tx.HasTransmittedReliable(Lane::Handoff,1),"complete first send releases metadata before any ACK");
        for (const auto& A:Acks) Check(Tx.Receive(A,Now,[](auto){return true;},[](auto){}),"deliver admission-window ACK");
        Acks.clear();
    }
    Check(Received==std::vector<Id>{ID(1500)} && Tx.CanQueueHandoff() && Tx.Metrics().QueuedBytes==0,"acknowledgement releases lane admission");
    Check(Tx.HasTransmittedReliable(Lane::Handoff,1) && !Tx.HasTransmittedReliable(Lane::Handoff,2),
        "acknowledged reliable sends stay complete; future sequences do not");
    Check(Tx.Queue(Next,2.1) && Tx.SentThrough(Lane::Handoff)==2,"next snapshot enters without a sequence hole");
}
void CommanderActivityPolicy()
{
    OwnershipScheduler S;for (unsigned I=1;I<=3;++I) Check(S.SetMember({ID(I),Team::Red,Role(I-1),true}),"scheduler member");
    std::array<ObjectKey,6> Keys;
    for (unsigned I=0;I<6;++I)
    {
        Authority A;Keys[I]=A.Object={ObjectKind::Building,ID(900+I),1};A.Controller=ID(1);A.SimOwner=ID(I<4?1:I-2);
        A.Faction=Team::Red;A.State=Life::Alive;A.ControlEpoch=A.OwnerEpoch=1;A.EstimatedCost=1;A.Policy=OwnershipPolicy::CommanderActivity;
        Check(S.SetObject({A,A.Policy},0),"data-defined selectable ownership");
    }
    Check(S.Select(ID(1),1,Keys,0),"select all");
    for (const auto& T:S.TakeActions())
    {Check(S.TargetPrepared(T.Transfer.Target,T.Transfer.Object,T.Transfer.IdValue),"target ready");
     Check(S.SourceFrozen(T.Transfer.Source,T.Transfer.Object,T.Transfer.IdValue,77),"state frozen");
     Check(S.TargetInstalled(T.Transfer.Target,T.Transfer.Object,T.Transfer.IdValue,77),"state installed");}
    S.TakeActions();for (unsigned I=0;I<3;++I) {S.ReportLoad(ID(1),6);S.ReportLoad(ID(2),1);S.ReportLoad(ID(3),1);}
    S.Poll(100);Check(S.Transfers().empty(),"continuous selection never offloads");
    Check(S.Select(ID(1),2,{},100),"deselect");S.Poll(109);Check(S.Transfers().empty(),"nine idle seconds do not offload");
    S.SetMembershipPaused(true,109);S.Poll(120);S.SetMembershipPaused(false,120);S.Poll(120.5);
    Check(S.Transfers().empty(),"membership pause does not accrue idle time");S.Poll(122);
    Check(!S.Transfers().empty() && S.Transfers().size()<=32,"eligible idle objects offload in one bounded batch");
    const auto T=S.Transfers().begin()->second;std::array<ObjectKey,1> Selected{T.Object};
    Check(S.Select(ID(1),3,Selected,122.1),"selection interrupts background handoff");
    Check(!S.Transfers().contains(T.Object),"reselected commander object stays local");
}
void OrderedReliableTransport()
{
    PeerChannel Tx(ID(77)),Rx(ID(77));std::vector<Bytes> Packets,Acks;std::vector<Id> Received;
    for (unsigned I=0;I<3;++I)
    {
        Message M;M.Header.Kind=MessageKind::Command;M.Header.Match=ID(100);M.Header.Epoch=1;M.Header.Sender=ID(1);M.Header.Event=ID(1000+I);M.Body=Bytes(2100,static_cast<std::uint8_t>(I));
        Check(Tx.Queue(M,0),"queue reliable command");
    }
    for (unsigned I=0;I<10;++I) Tx.Pump(I*.1,[&](std::span<const std::uint8_t> B){Check(B.size()<=1000,"datagram including headers is bounded");Packets.emplace_back(B.begin(),B.end());return true;});
    std::reverse(Packets.begin(),Packets.end());
    for (const auto& P:Packets)
    {
        Message Partial;Check(!InspectDatagramMessage(P,Partial),"a fragment cannot prove a transmitted application header");
        auto Ack=[&](std::span<const std::uint8_t> B){Acks.emplace_back(B.begin(),B.end());return true;};
        auto Apply=[&](Message M){Received.push_back(M.Header.Event);};
        Check(Rx.Receive(P,1,Ack,Apply),"reordered fragment received");Check(Rx.Receive(P,1,Ack,Apply),"duplicate fragment received");
    }
    Check(Received==std::vector<Id>{ID(1000),ID(1001),ID(1002)},"reliable order and duplicate suppression");
    for (const auto& P:Acks) {Message Ack;Check(!InspectDatagramMessage(P,Ack),"ACK is not application transmission evidence");}
    PeerChannel Single(ID(81));Message Small;Small.Header.Kind=MessageKind::Motion;Small.Header.Match=ID(100);
    Small.Header.Epoch=1;Small.Header.Sender=ID(1);Small.Header.Object={ObjectKind::Soldier,ID(30),2};
    Small.Header.OwnerEpoch=7;Small.Header.At=11;Small.Body=Bytes(20,3);bool Inspected=false;
    Check(Single.Queue(Small,0),"queue complete motion for actual-send evidence");
    Single.Pump(0,[&](auto Packet)
    {
        Message Actual;Check(InspectDatagramMessage(Packet,Actual),"complete motion exposes its actual wire header");
        Check(Actual.Header.Match==Small.Header.Match && Actual.Header.Object==Small.Header.Object
            && Actual.Header.OwnerEpoch==7 && Actual.Header.At==11 && Actual.Body==Small.Body,"wire evidence retains identity, life and ownership");
        Inspected=true;return true;
    });
    Check(Inspected,"application evidence requires an actual send callback");
}
void LatestStateDuringFragmentedSend()
{
    // Three 1800-byte bodies include their message/fragment headers. Allow
    // all three wire messages to finish inside the unchanged 0.5 s state TTL.
    QueueLimits Limits;Limits.BytesPerSecond[1]=12000;
    PeerChannel Tx(ID(79),Limits),Rx(ID(79),Limits);std::vector<Message> Received;
    auto State=[](unsigned N,MessageKind Kind=MessageKind::Motion)
    {
        Message V;V.Header.Kind=Kind;V.Header.Match=ID(100);V.Header.Epoch=1;V.Header.Sender=ID(1);
        V.Header.Object={ObjectKind::Soldier,ID(30),1};V.Header.Event=ID(1400+N);V.Body=Bytes(1800,std::uint8_t(N));return V;
    };
    auto Send=[&](std::span<const std::uint8_t> B)
    {return Rx.Receive(B,.1,[](auto){return true;},[&](Message V){Received.push_back(std::move(V));});};
    Check(Tx.Queue(State(1),0),"queue fragmented motion");Tx.Pump(0,Send);
    Check(Received.empty(),"first sample is only partially transmitted");
    Check(Tx.Queue(State(2),.01) && Tx.Queue(State(3),.02),"replace only the waiting successor");
    Check(Tx.Queue(State(4,MessageKind::ComponentState),.02),"another state kind on this object remains independent");
    for (unsigned I=1;I<=5;++I) Tx.Pump(.1*I,Send);
    Check(Received.size()==3 && Received[0].Body==Bytes(1800,1) && Received[1].Body==Bytes(1800,3)
        && Received[2].Header.Kind==MessageKind::ComponentState && Received[2].Body==Bytes(1800,4),
        "started sample finishes, newest waiting sample wins, other kinds are not overwritten");
    Check(Tx.Metrics().QueuedBytes==0 && Tx.Metrics().ReplacedStates==1,"coalesced queue bytes converge exactly");
    Check(Tx.Queue(State(5),1),"requeue after completion");Tx.Pump(2,Send);
    Check(Tx.Metrics().QueuedBytes==0,"expired waiting sample is removed from accounting");
    Check(Tx.Queue(State(6),2),"expired index cannot remove a later sample");Tx.Pump(2.1,Send);
    Check(Received.back().Body==Bytes(1800,6) && Tx.Metrics().QueuedBytes==0,"latest state still delivers after expiry");
}
void RoomMetadata()
{
    Room R(ID(100),"config","map");std::string Error;
    Check(R.ConfigureDefinitions({{"data/unit",{OwnershipPolicy::CommanderActivity,Role::Commander,false}}}),"room definitions");
    for (unsigned N=1;N<=3;++N)
    {Member M;M.Player=ID(N);M.Faction=Team::Red;M.Seat=Role(N-1);Check(R.Join(M,"config","map",0,Error)&&R.Ready(M.Player),"three unique seats");}
    ObjectRegistration O;O.Grant.Object={ObjectKind::Soldier,ID(20),1};O.Grant.Faction=Team::Red;O.Definition="data/unit";
    Check(R.Register(O,0),"metadata registration");for (const auto& A:R.Propose(0)) Check(R.CommitAssignment(A.ProposedOwner,A.Object,A.ProposedEpoch,0),"initial assignment");
    R.SetInitialRosterReady(true);R.Poll(0);Check(R.Phase()==RoomPhase::Running,"start without any checkpoint");
    const auto Owner=R.Objects().at(O.Grant.Object).Grant.SimOwner;
    const auto Bytes=R.ExportMetadata();Room Restored(ID(100),"config","map");
    Check(Restored.ConfigureDefinitions({{"data/unit",{OwnershipPolicy::CommanderActivity,Role::Commander,false}}})&&Restored.ImportMetadata(Bytes,5),"service restart restores only metadata");
    Check(Restored.Objects().at(O.Grant.Object).Grant.SimOwner==Owner && Restored.Phase()==RoomPhase::Running,"restart preserves running ownership");
    auto Reconnect=Restored.Members().at(ID(1));Check(Restored.Join(Reconnect,"config","map",6,Error),"control reconnect");
    Check(!Restored.Membership() && Restored.Phase()==RoomPhase::Running,"control reconnect does not pause the match");
    {
        Room Balanced(ID(103),"config","map");
        Check(Balanced.ConfigureDefinitions({{"data/unit",{OwnershipPolicy::CommanderActivity,Role::Commander,false}}}),"leave reservation definitions");
        for (unsigned N=1;N<=3;++N)
        {Member M;M.Player=ID(N);M.Faction=Team::Red;M.Seat=Role(N-1);
            Check(Balanced.Join(M,"config","map",0,Error) && Balanced.Ready(M.Player),"leave reservation roster");}
        for (unsigned N=0;N<6;++N)
        {ObjectRegistration V;V.Grant.Object={ObjectKind::Soldier,ID(5000+N),1};V.Grant.Faction=Team::Red;V.Definition="data/unit";
            Check(Balanced.Register(V,0,ID(1)),"one source owns the departure wave");}
        Balanced.SetInitialRosterReady(true);Balanced.Poll(0);
        Check(Balanced.BeginLeave(ID(1),false,1),"start balanced departure before any installation");
        std::map<Id,unsigned> Destinations;
        for (const auto& [K,T]:Balanced.Ownership().Transfers()) ++Destinations[T.Target];
        Check(Destinations[ID(2)]==3 && Destinations[ID(3)]==3,
            "queued custody reservations balance equal-cost work across available receivers before grants commit");
    }
    {
        OwnershipSettings Config;Config.MaximumLeaveBatch=2;
        Room Queued(ID(102),"config","map",{},Config);
        Check(Queued.ConfigureDefinitions({{"data/unit",{OwnershipPolicy::CommanderActivity,Role::Commander,false}}}),"queued departure definitions");
        for (unsigned N=1;N<=2;++N)
        {Member M;M.Player=ID(N);M.Faction=Team::Red;M.Seat=Role(N-1);
            Check(Queued.Join(M,"config","map",0,Error) && Queued.Ready(M.Player),"queued departure roster");}
        std::vector<ObjectKey> Owned;
        for (unsigned N=0;N<7;++N)
        {ObjectRegistration V;V.Grant.Object={ObjectKind::Soldier,ID(1000+N),1};V.Grant.Faction=Team::Red;V.Definition="data/unit";
            Check(Queued.Register(V,0),"queued departure object");}
        for (const auto& A:Queued.Propose(0)) Check(Queued.CommitAssignment(A.ProposedOwner,A.Object,A.ProposedEpoch,0),"queued initial grant");
        Queued.SetInitialRosterReady(true);Queued.Poll(0);
        for (const auto& [K,V]:Queued.Objects()) if (V.Grant.SimOwner==ID(1)) Owned.push_back(K);
        Check(Owned.size()>2 && Queued.BeginLeave(ID(1),false,1),"queued source owns more objects than one window");
        const auto Version=Queued.Membership()->Version;const auto Deadline=Queued.Membership()->Deadline;
        Check(Queued.Ownership().Transfers().size()==2,"only one bounded transfer window starts timers");
        Queued.MembershipReady(ID(1),Version,1);Queued.MembershipReady(ID(2),Version,1);
        unsigned Waves=0;
        while (Queued.Membership() && Waves<8)
        {
            const double At=1.+(++Waves)*.1;Queued.Propose(At);const auto Pending=Queued.Ownership().Transfers();
            Check(!Pending.empty() && Pending.size()<=2 && Queued.Membership()->Deadline==Deadline,"queued departure retains membership deadline and transfer window");
            for (const auto& [K,T]:Pending)
            {Check(T.Deadline>At && T.Deadline<=At+5,"active transfer keeps original five-second deadline");
                Check(Queued.Ownership().TargetPrepared(T.Target,K,T.IdValue) && Queued.Ownership().SourceFrozen(T.Source,K,T.IdValue,123)
                    && Queued.Ownership().TargetInstalled(T.Target,K,T.IdValue,123),"queued state installs");}
            Queued.Poll(At);Check(Queued.Membership().has_value(),"unpublished/deferred custody cannot finish departure");
            Queued.TakeOwnershipActions(At);Queued.Poll(At);
        }
        Check(!Queued.Membership() && Waves>1 && Queued.Phase()==RoomPhase::Running,"queued departure drains all windows before completion");
        for (const auto& K:Owned) Check(Queued.Objects().at(K).Grant.SimOwner==ID(2),"all queued private custody committed");
    }
    {
        const auto Key=O.Grant.Object;const auto& Grant=Restored.Objects().at(Key).Grant;
        Check(Restored.UpdateLifecycle(Owner,Key,Grant.OwnerEpoch,Life::Destroyed,6),"retired metadata retains its historical owner");
        Check(Restored.BeginLeave(Owner,false,7),"retired owner can leave normally");
        Check(Restored.Ownership().Transfers().empty(),"destroyed history never requests a nonexistent private-state transfer");
        const auto Version=Restored.Membership()->Version;
        for (unsigned N=1;N<=3;++N) Restored.MembershipReady(ID(N),Version,7);
        Check(Restored.Phase()==RoomPhase::Running && !Restored.Members().at(Owner).Online,"retired history does not delay departure completion");
    }
    {
        Room Departures(ID(101),"config","map");
        Check(Departures.ConfigureDefinitions({{"data/unit",{OwnershipPolicy::CommanderActivity,Role::Commander,false}}}),"departure definitions");
        for (unsigned N=1;N<=4;++N)
        {Member M;M.Player=ID(N);M.Faction=N<=3?Team::Red:Team::Blue;M.Seat=N<=3?Role(N-1):Role::Commander;
            Check(Departures.Join(M,"config","map",0,Error) && Departures.Ready(M.Player),"departure roster");}
        for (unsigned N=0;N<3;++N)
        {ObjectRegistration V;V.Grant.Object={ObjectKind::Soldier,ID(30+N),1};V.Grant.Faction=Team::Red;V.Definition="data/unit";
            Check(Departures.Register(V,0),"departure unit");}
        for (const auto& A:Departures.Propose(0)) Check(Departures.CommitAssignment(A.ProposedOwner,A.Object,A.ProposedEpoch,0),"departure initial grant");
        Departures.SetInitialRosterReady(true);Departures.Poll(0);
        Check(Departures.BeginLeave(ID(1),true,1),"first abrupt departure");
        const auto Version=Departures.Membership()->Version;const double Deadline=Departures.Membership()->Deadline;
        Check(!Departures.Propose(1).empty(),"another departing peer can already have an outstanding assignment");
        Check(Departures.BeginLeave(ID(2),true,1.1) && Departures.BeginLeave(ID(3),true,1.2),"concurrent departures join the active roster change");
        Check(Departures.Membership()->Version==Version && Departures.Membership()->Deadline==Deadline,"concurrent loss does not restart the membership deadline");
        Check(Departures.Propose(1.3).empty(),"offline teammates are no longer assignment candidates");
        Check(Departures.MembershipReady(ID(4),Version,1.3) && Departures.Phase()==RoomPhase::Running,"remaining team can finish the roster change");
        Departures.Poll(2);Departures.Poll(32);
        Check(Departures.Phase()==RoomPhase::Aborted && Departures.Failure().find("Empty team")!=std::string::npos,"empty team reaches its existing terminal interval");
    }
}
void IndivisibleGroupsAndFixedRoles()
{
    Room R(ID(5000),"config","map");std::string Error;
    {
        Room Carrier(ID(5003),"config","map");
        Check(Carrier.ConfigureDefinitions({{"ship",{OwnershipPolicy::ControllerPinned,Role::Air,false}},
            {"wingman",{OwnershipPolicy::ControllerPinned,Role::Air,true}},
            {"flight",{OwnershipPolicy::RetainedSource,Role::Observer,false}}}),"pinned carrier definition");
        for (unsigned N:{1u,3u,4u})
        {
            Member M;M.Player=ID(N);M.Faction=N<4?Team::Red:Team::Blue;M.Seat=N==3?Role::Air:Role::Commander;
            Check(Carrier.Join(M,"config","map",0,Error) && Carrier.Ready(M.Player),"pinned carrier roster");
        }
        ObjectRegistration O;O.Grant.Object={ObjectKind::Ship,ID(5400),1};O.Grant.Faction=Team::Red;O.Definition="ship";
        const auto Key=O.Grant.Object;Check(Carrier.Register(O,0),"pinned carrier declaration");
        for (const auto& A:Carrier.Propose(0)) Check(A.ProposedOwner==ID(3)
            && Carrier.CommitAssignment(A.ProposedOwner,A.Object,A.ProposedEpoch,0),"original Air carrier authority");
        ObjectRegistration Member;Member.Grant.Object={ObjectKind::Wingman,ID(5410),1};Member.Grant.Faction=Team::Red;
        Member.Grant.Controller=Member.Grant.SimOwner=ID(3);Member.Grant.OwnerEpoch=1;Member.Grant.State=Life::Alive;
        Member.Grant.Policy=OwnershipPolicy::ControllerPinned;Member.Grant.SourceLease=ID(5411);Member.Definition="wingman";Member.Parent=Key;
        Check(Carrier.Register(Member,0,ID(3)),"registered member before vacancy");
        auto Unannounced=Member;Unannounced.Grant.Object.StableId=ID(5412);Unannounced.Grant.SourceLease=ID(5413);
        Check(!Carrier.CanRecoverBirth(Unannounced,ID(1)),"live producer cannot be impersonated by its parent custodian");
        Carrier.SetInitialRosterReady(true);Carrier.Poll(0);
        const auto Original=Carrier.Members().at(ID(3));
        Check(Carrier.BeginLeave(ID(3),true,.1),"abrupt Air vacancy");
        const auto LeaveVersion=Carrier.Membership()->Version;
        for (auto P:{ID(1),ID(4)}) Carrier.MembershipReady(P,LeaveVersion,.1);
        Check(!Carrier.Membership() && Carrier.Objects().at(Key).Grant.Dormant
            && Carrier.Objects().at(Key).Grant.StateCustodian==ID(1),"vacant carrier retained by a live custodian");
        Check(Carrier.CanRecoverBirth(Unannounced,ID(1)) && !Carrier.CanRecoverBirth(Unannounced,ID(4))
            && Carrier.Register(Unannounced,.15,ID(1)),"only actual parent custodian recovers a committed orphan birth");
        const auto RecoveredMember=Carrier.Objects().at(Unannounced.Grant.Object).Grant;
        Check(RecoveredMember.State==Life::Destroyed && RecoveredMember.OwnerEpoch==2,
            "vacancy policy retires recovered old-tenure member under a newer authority");
        ObjectRegistration Flight;Flight.Grant=Member.Grant;Flight.Grant.Object={ObjectKind::Flight,ID(5414),1};
        Flight.Grant.Policy=OwnershipPolicy::RetainedSource;Flight.Grant.SourceLease=ID(5415);Flight.Definition="flight";Flight.Parent=Member.Grant.Object;
        Check(Carrier.CanRecoverBirth(Flight,ID(1)) && Carrier.Register(Flight,.16,ID(1)),
            "recovery follows a real retired-parent chain to the current carrier custodian");
        const auto RecoveredFlight=Carrier.Objects().at(Flight.Grant.Object).Grant;
        Check(RecoveredFlight.OwnerEpoch==2 && RecoveredFlight.SimOwner==ID(1) && RecoveredFlight.Controller==ID(3)
            && RecoveredFlight.SourceLease==Flight.Grant.SourceLease && RecoveredFlight.State==Life::Alive,
            "retained work preserves original provenance and rejects the old owner version");
        Check(Carrier.Join(Original,"config","map",.2,Error) && Carrier.Ready(ID(3)),"original Air ready again");
        const auto JoinVersion=Carrier.Membership()->Version;
        // No scheduling poll between the final bootstrap ACK and completion.
        for (auto P:{ID(1),ID(4),ID(3)}) Carrier.MembershipReady(P,JoinVersion,.2);
        Check(Carrier.Membership().has_value() && Carrier.Ownership().Transfers().size()==1,
            "final ready receipt cannot resume before pinned private custody returns");
        const auto Transfer=Carrier.Ownership().Transfers().at(Key);
        Check(Transfer.Source==ID(1) && Transfer.Target==ID(3)
            && Carrier.Ownership().TargetPrepared(ID(3),Key,Transfer.IdValue)
            && Carrier.Ownership().SourceFrozen(ID(1),Key,Transfer.IdValue,5400)
            && Carrier.Ownership().TargetInstalled(ID(3),Key,Transfer.IdValue,5400),"pinned private state returns through the normal handoff");
        Carrier.TakeOwnershipActions(.3);Carrier.Poll(.3);
        const auto& Grant=Carrier.Objects().at(Key).Grant;
        Check(!Carrier.Membership() && Carrier.Phase()==RoomPhase::Running && !Grant.Dormant
            && Grant.Controller==ID(3) && Grant.SimOwner==ID(3) && !Grant.StateCustodian.Valid(),
            "resumed fixed Air seat owns its active original carrier");
    }
    const std::map<std::string,OwnershipDefinition> Definitions{
        {"resource",{OwnershipPolicy::SharedGroup,Role::Commander,false,"match/resources"}},
        {"unit",{OwnershipPolicy::CommanderActivity,Role::Commander,false}}};
    {
        Room Fresh(ID(5002),"config","map");Check(Fresh.ConfigureDefinitions(Definitions),"unassigned startup group definitions");
        Member Ground;Ground.Player=ID(2);Ground.Faction=Team::Red;Ground.Seat=Role::Ground;
        Check(Fresh.Join(Ground,"config","map",0,Error) && Fresh.Ready(Ground.Player),"startup helper ready before allocation");
        ObjectRegistration O;O.Grant.Object={ObjectKind::Building,ID(5300),1};O.Grant.Faction=Team::Red;O.Definition="resource";
        Check(Fresh.Register(O,0),"startup unassigned resource");Fresh.Poll(0);
        Member Commander;Commander.Player=ID(1);Commander.Faction=Team::Red;Commander.Seat=Role::Commander;
        Check(Fresh.Join(Commander,"config","map",.1,Error) && Fresh.Ready(Commander.Player),"Commander ready before first offers");
        const auto Offers=Fresh.Propose(.1);Check(Offers.size()==1 && Offers.front().ProposedOwner==Commander.Player,"first group allocation prefers ready Commander");
        Check(Fresh.CommitAssignment(Offers.front().ProposedOwner,Offers.front().Object,Offers.front().ProposedEpoch,.1),"first group grant committed");
        const auto G=Fresh.OwnershipGroups().at(StableId("GuLiOwnershipGroup/v1","match/resources"));
        Check(G.Owner==Commander.Player && Fresh.Objects().at(O.Grant.Object).Grant.SimOwner==G.Owner
            && Fresh.Objects().at(O.Grant.Object).Grant.GroupEpoch==G.Version && Fresh.Ownership().Transfers().empty(),"unowned startup has coherent group metadata without a needless state transfer");
    }
    {
        // A helper can become ready before the rendered Commander. Its actual
        // group custody must remain authoritative until a complete transfer.
        Room Early(ID(5001),"config","map");Check(Early.ConfigureDefinitions(Definitions),"early group definitions");
        Member Ground;Ground.Player=ID(2);Ground.Faction=Team::Red;Ground.Seat=Role::Ground;
        Check(Early.Join(Ground,"config","map",0,Error) && Early.Ready(Ground.Player),"helper ready first");
        std::vector<ObjectKey> Initial;
        for (unsigned N=0;N<2;++N)
        {ObjectRegistration O;O.Grant.Object={ObjectKind::Building,ID(5200+N),1};O.Grant.Faction=Team::Red;O.Definition="resource";
            Check(Early.Register(O,0),"early resource declaration");Initial.push_back(O.Grant.Object);}
        const auto Offers=Early.Propose(0);Check(Offers.size()==2,"early initial offers");
        Check(Early.CommitAssignment(Offers[0].ProposedOwner,Offers[0].Object,Offers[0].ProposedEpoch,0),"helper initial custody");
        Member Commander;Commander.Player=ID(1);Commander.Faction=Team::Red;Commander.Seat=Role::Commander;
        Check(Early.Join(Commander,"config","map",.1,Error) && Early.Ready(Commander.Player),"Commander ready later");
        Early.Propose(.1);const auto Pending=Early.Ownership().Transfers();
        Check(Pending.size()==1,"already owned startup state uses a real group handoff");
        for (const auto& [K,T]:Pending)
            Check(T.Source==Ground.Player && T.Target==Commander.Player
                && Early.Ownership().TargetPrepared(T.Target,K,T.IdValue)
                && Early.Ownership().SourceFrozen(T.Source,K,T.IdValue,900)
                && Early.Ownership().TargetInstalled(T.Target,K,T.IdValue,900),"startup group state imported");
        Early.TakeOwnershipActions(.2);
        Check(!Early.CommitAssignment(Offers[1].ProposedOwner,Offers[1].Object,Offers[1].ProposedEpoch,.2),"old initial offer cannot split the committed group");
        for (const auto& A:Early.Propose(.3)) Check(A.ProposedOwner==Commander.Player
            && Early.CommitAssignment(A.ProposedOwner,A.Object,A.ProposedEpoch,.3),"unassigned state follows current group authority");
        const auto Group=StableId("GuLiOwnershipGroup/v1","match/resources");const auto Authority=Early.OwnershipGroups().at(Group);
        for (const auto& K:Initial) Check(Early.Objects().at(K).Grant.SimOwner==Authority.Owner
            && Early.Objects().at(K).Grant.GroupEpoch==Authority.Version && Authority.Owner==Commander.Player,"group metadata and actual startup grants agree");
    }
    Check(R.ConfigureDefinitions(Definitions),"generic group definitions");
    for (unsigned N:{1u,2u,4u})
    {Member M;M.Player=ID(N);M.Faction=N<4?Team::Red:Team::Blue;M.Seat=N==2?Role::Ground:Role::Commander;
        Check(R.Join(M,"config","map",0,Error) && R.Ready(M.Player),"group roster");}
    std::vector<ObjectKey> Keys;
    for (unsigned N=0;N<3;++N)
    {ObjectRegistration O;O.Grant.Object={ObjectKind::Building,ID(5100+N),1};O.Grant.Faction=Team(N);O.Definition="resource";
        Check(R.Register(O,0),"resource member declaration");Keys.push_back(O.Grant.Object);}
    for (const auto& A:R.Propose(0)) Check(A.ProposedOwner==ID(1) && R.CommitAssignment(A.ProposedOwner,A.Object,A.ProposedEpoch,0),"one host across both factions and neutral state");
    R.SetInitialRosterReady(true);R.Poll(0);Check(R.Phase()==RoomPhase::Running,"group ready");
    {
        Room Lost(ID(5003),"config","map");Check(Lost.ConfigureDefinitions(Definitions),"abrupt resource group definitions");
        for (const auto& [P,M]:R.Members()) Check(Lost.Join(M,"config","map",0,Error)&&Lost.Ready(P),"abrupt resource roster");
        for (const auto K:Keys) {auto O=R.Objects().at(K);O.Grant.SimOwner={};O.Grant.OwnerEpoch=0;Check(Lost.Register(O,0),"abrupt resource declaration");}
        for (const auto& A:Lost.Propose(0)) Check(Lost.CommitAssignment(A.ProposedOwner,A.Object,A.ProposedEpoch,0),"abrupt initial grant");
        Lost.SetInitialRosterReady(true);Lost.Poll(0);
        const auto Group=StableId("GuLiOwnershipGroup/v1","match/resources");
        const auto Before=Lost.ResourceGroups().at(Group);
        Check(Before.Owner==ID(1)&&Before.Backup==ID(2),"backup is a Ground storage custodian, not the next Commander");
        std::vector<ObjectRegistration> Cut;for (const auto K:Keys) Cut.push_back(Lost.Objects().at(K));
        Check(Lost.BeginLeave(ID(1),true,1),"abrupt resource owner exit");
        const auto Recovery=Lost.ResourceGroups().at(Group);
        Check(Recovery.Publisher==ID(2)&&Recovery.Target==ID(4)&&Recovery.Deadline==6,
            "storage custodian publishes directly to the final simulation owner within the same budget");
        for (const auto P:{ID(2),ID(4)}) Check(Lost.ResourceSnapshotInstalled(P,Group,Recovery.Recovery,1,std::string(64,'b'),Cut,1.1),"abrupt complete cut installed");
        const auto Version=Lost.Membership()->Version;
        for (const auto P:{ID(2),ID(4)}) Check(Lost.MembershipReady(P,Version,1.2),"abrupt member ready");
        Lost.Propose(1.3);
        Check(Lost.Phase()==RoomPhase::Running&&Lost.Ownership().Transfers().empty(),"snapshot recovery must not immediately start a second full-state group handoff");
        for (const auto K:Keys) Check(Lost.Objects().at(K).Grant.SimOwner==ID(4),"only final Commander receives recovered simulation authority");
    }
    Check(R.Objects().at(Keys[2]).Grant.Controller==ID(4),"host does not gain enemy command");
    auto Wrong=R.Members().at(ID(2));Wrong.Seat=Role::Air;
    Check(!R.Join(Wrong,"config","map",.1,Error) && Error=="Role is locked for this match","online role mutation refused");
    Check(R.BeginLeave(ID(1),false,1),"graceful group host exit");
    const auto Group=StableId("GuLiOwnershipGroup/v1","match/resources");const auto Recovery=R.ResourceGroups().at(Group);
    Check(R.Ownership().Transfers().empty() && Recovery.Target==ID(4) && Recovery.RequestEpoch==2
        && Recovery.Deadline==6,"resource exit uses one snapshot epoch and the original five-second budget");
    std::vector<ObjectRegistration> Cut;for (const auto K:Keys) Cut.push_back(R.Objects().at(K));
    for (auto P:{ID(1),ID(2)})
    {
        Check(R.ResourceSnapshotInstalled(P,Group,Recovery.Recovery,1,std::string(64,'a'),Cut,1.1),"complete snapshot installation acknowledged");
        for (const auto K:Keys) Check(R.Objects().at(K).Grant.SimOwner==ID(1),"no group member activates before every client installs");
    }
    Check(R.ResourceSnapshotInstalled(ID(4),Group,Recovery.Recovery,1,std::string(64,'a'),Cut,1.2),"last snapshot installation commits all group grants");
    for (auto P:{ID(1),ID(2),ID(4)}) R.MembershipReady(P,R.Membership()->Version,1);
    for (const auto& K:Keys) Check(R.Objects().at(K).Grant.SimOwner==ID(4) && R.Objects().at(K).Grant.GroupEpoch==2,"group host/version move together");
    auto Return=R.Members().at(ID(1));Return.Seat=Role::Air;
    Check(!R.Join(Return,"config","map",2,Error),"offline identity cannot change its seat");
    Member New;New.Player=ID(8);New.Faction=Team::Red;New.Seat=Role::Commander;
    Check(R.Join(New,"config","map",2,Error) && R.Ready(New.Player),"new identity may fill vacant seat");
    const auto JoinVersion=R.Membership()->Version;
    for (auto P:{ID(2),ID(4),ID(8)}) R.MembershipReady(P,JoinVersion,2);
    Return.Seat=Role::Commander;
    Check(!R.Join(Return,"config","map",3,Error) && Error=="Role seat occupied","returning identity cannot displace replacement");
    for (const auto& K:Keys) Check(R.Objects().at(K).Grant.SimOwner==ID(4),"another Commander joining does not migrate a Commander-owned group");
    Room Copy(ID(5000),"config","map");Check(Copy.ConfigureDefinitions(Definitions) && Copy.ImportMetadata(R.ExportMetadata(),4),"group and fixed seats persist without gameplay data");
}
void CurrentResourceSnapshotContracts()
{
    Message Hello;Hello.Header.Kind=MessageKind::Hello;Bytes Wire;
    Check(EncodeMessage(Hello,Wire),"encode current protocol hello");
    Wire[4]=31;Wire[5]=0;Message Rejected;
    Check(!DecodeMessage(Wire,Rejected),"protocol 31 cannot enter a protocol 32 room");
    ResourceSnapshot A;A.Match=ID(7000);A.Group=ID(7001);A.Owner=ID(1);A.OwnerVersion=A.RequestEpoch=A.Number=1;
    A.Sample=60;A.CapturedUtcMicroseconds=1000000;A.Parts={{"object",Bytes(128,1)},{"tasks",Bytes(64,2)}};A.Digest=ResourceSnapshotDigest(A);
    ResourceSnapshotStore Store;const auto Full=DiffResourceSnapshot(A,nullptr);ResourceSnapshotDelta Decoded;
    const auto Encoded=EncodeResourceSnapshotDelta(Full);
    Check(DecodeResourceSnapshotDelta(Encoded,Decoded)&&Store.Receive(Decoded,1)==ResourceSnapshotStore::Result::Installed,"complete resource baseline validates");
    Check(Store.Receive(Decoded,2)==ResourceSnapshotStore::Result::Duplicate&&Store.CompletedAt()==1,"duplicate does not refresh replica age");
    auto Corrupt=Decoded;Corrupt.Value.Parts.at("object")[0]^=1;
    Check(Store.Receive(Corrupt,2)==ResourceSnapshotStore::Result::Rejected&&Store.Latest()->Digest==A.Digest,"corrupt duplicate cannot replace or acknowledge a complete cut");
    auto Truncated=Encoded;Truncated.pop_back();Check(!DecodeResourceSnapshotDelta(Truncated,Decoded),"incomplete snapshot cannot be restored");
    auto B=A;B.Number=2;B.Sample=66;B.Parts.erase("tasks");B.Parts["cargo"]={3,4};B.Digest=ResourceSnapshotDigest(B);
    auto Delta=DiffResourceSnapshot(B,&A);ResourceSnapshotStore Empty;
    Check(Empty.Receive(Delta,2)==ResourceSnapshotStore::Result::MissingBaseline,"missing delta baseline fails closed");
    Check(Store.Receive(Delta,2)==ResourceSnapshotStore::Result::Installed&&Store.Latest()->Parts==B.Parts,"delta installation preserves complete current state and deletions");
    auto C=B;C.Number=3;C.Sample=72;C.Digest=ResourceSnapshotDigest(C);
    Check(Store.Receive(DiffResourceSnapshot(C,&B),3)==ResourceSnapshotStore::Result::Installed
        && !Store.Find(A.Number,A.Digest)&&Store.Find(B.Number,B.Digest),"backup retains exactly the last two complete versions");
    auto Wrong=DiffResourceSnapshot(C,nullptr);Wrong.Value.RequestEpoch=0;
    Check(Store.Receive(Wrong,4)==ResourceSnapshotStore::Result::Rejected,"invalid epoch cannot enter snapshot storage");
    Check(Store.Receive(Full,4)==ResourceSnapshotStore::Result::Rejected,"older complete cut never replaces a newer cut");
    const auto Bound=Store.BytesHeld();
    for (unsigned N=4;N<1028;++N)
    {
        auto Next=*Store.Latest();Next.Number=N;Next.Sample+=6;Next.Digest=ResourceSnapshotDigest(Next);
        Check(Store.Receive(DiffResourceSnapshot(Next,Store.Latest()),N)==ResourceSnapshotStore::Result::Installed
            &&Store.Size()==2&&Store.BytesHeld()==Bound,"unchanged current payload retains two versions with constant storage");
    }
    std::cout<<"Resource snapshot full/delta/integrity/retention contracts passed\n";
}
void ResourceEpochAndRecoveryContracts()
{
    Fixture F;const Id Group=ID(7100);const std::vector<ObjectKey> Scope{F.Key(0),TeamAccountObject(Team::Red)};
    ResourceGroupState G;G.Group=Group;G.Owner=ID(1);G.Backup=ID(2);G.OwnerVersion=G.RequestEpoch=1;
    for (auto& [P,C]:F.Clients)
    {
        for (const auto K:Scope)
        {
            auto O=C->Objects().at(K);O.Grant.Policy=OwnershipPolicy::SharedGroup;O.Grant.OwnershipGroup=Group;O.Grant.GroupEpoch=1;O.Grant.SimOwner=ID(1);
            Check(C->Install(O,true),"resource epoch fixture grant");
        }
        C->SetResourceGroups({{Group,G}});
    }
    F.Step(4);
    Check(!F.Client(2).HasSimulation(F.Key(0)),"backup storage does not acquire simulation authority");
    TransactionRequest Pending;Pending.Header.Object=F.Key(0);Pending.Header.Event=ID(7101);Pending.Kind=TransactionKind::Reserve;
    Pending.RuleId="reserve";Pending.Faction=Team::Red;Pending.ReservationKey={DeriveEconomyAccount(F.Key(0)),1};
    unsigned Interrupted=0;
    Check(F.Client(1).Submit(Pending,[&](const auto& R){Check(R.Status==CommitStatus::Interrupted,"unfinished request receives interruption");++Interrupted;}).Valid(),"queue resource request before membership pause");
    auto Late=std::move(F.Queue);F.Queue.clear();
    G.Recovery=9;G.PreviousOwner=G.Publisher=ID(1);G.Target=ID(2);G.RequestEpoch=2;G.Deadline=F.Now+5;
    for (auto& [P,C]:F.Clients) {C->SetResourceGroups({{Group,G}});C->SetResourceGroups({{Group,G}});}
    Check(Interrupted==1&&F.Client(1).PendingTransactions().empty(),"epoch closure interrupts once and releases pending requests");
    for (auto& D:Late) if (D.Value.Header.ResourceGroup==Group)
        Check(!F.Clients.at(D.To)->ReceivePeer(D.From,D.Value),"late old-epoch message is rejected");
    for (auto& [P,C]:F.Clients) Check(!C->HasSimulation(F.Key(0)),"all clients remain fenced during installation");
    F.Step(10);
    std::set<Id> Installed;Revision Number=0;std::string Digest;
    for (const auto& D:F.Metadata) if (D.Value.Header.Kind==MessageKind::ResourceSnapshotInstalled)
    {
        Reader R(D.Value.Body);Id K;Revision Recovery,N;std::string Hash;
        Check(R.Identity(K)&&R.U64(Recovery)&&R.U64(N)&&R.Text(Hash)&&K==Group&&Recovery==9,"resource installation acknowledgment identity");
        if (Number) Check(Number==N&&Digest==Hash,"every peer installs the identical complete cut");
        Number=N;Digest=Hash;Installed.insert(D.From);
    }
    if (Installed.size()!=F.Clients.size())
        for (const auto& [P,C]:F.Clients) {std::cerr<<"resource client="<<P.ToString()<<" error="<<C->Error()<<'\n';for (const auto& [K,N]:C->HandoffDiagnostics()) std::cerr<<K<<"="<<N<<'\n';}
    Check(Installed.size()==F.Clients.size(),"every peer acknowledges complete resource installation");
    for (auto& [P,C]:F.Clients)
    {
        for (const auto K:Scope)
        {auto A=C->Objects().at(K).Grant;A.SimOwner=ID(2);++A.OwnerEpoch;A.GroupEpoch=2;Check(C->InstallGrant(A),"install new resource owner grant");}
    }
    G.Owner=ID(2);G.Backup=ID(3);G.OwnerVersion=2;G.Recovery=0;G.PreviousOwner=G.Publisher=G.Target={};G.Deadline=0;
    for (auto& [P,C]:F.Clients) {C->SetResourceGroups({{Group,G}});C->SetMembershipPaused(false);}
    Check(!F.Client(1).HasSimulation(F.Key(0))&&F.Client(2).HasSimulation(F.Key(0)),"only the installed new owner resumes simulation");
    auto Next=Pending;Next.Header.Event=ID(7102);Next.ReservationKey.Request=2;unsigned Completed=0;
    Check(F.Client(2).Submit(Next,[&](const auto& R){Check(R.Status==CommitStatus::Committed,"new epoch resource request commits");++Completed;}).Valid(),"submit fresh resource request after recovery");
    F.Step(10);Check(Completed==1&&Interrupted==1,"new requests progress without replaying the interrupted request");
    std::cout<<"Resource interruption and complete-install activation contracts passed\n";
}
void ResourceInterruptionCutpoints()
{
    for (unsigned Cutpoint=0;Cutpoint<4;++Cutpoint)
    {
        Fixture F;const Id Group=ID(7400);const std::vector<ObjectKey> Scope{F.Key(0),TeamAccountObject(Team::Red)};
        ResourceGroupState G;G.Group=Group;G.Owner=ID(1);G.Backup=ID(2);G.OwnerVersion=G.RequestEpoch=1;
        for (auto& [P,C]:F.Clients)
        {
            for (const auto K:Scope)
            {auto O=C->Objects().at(K);O.Grant.Policy=OwnershipPolicy::SharedGroup;O.Grant.OwnershipGroup=Group;
                O.Grant.GroupEpoch=1;O.Grant.SimOwner=ID(1);Check(C->Install(O,true),"cutpoint resource grant");}
            C->SetResourceGroups({{Group,G}});
        }
        F.Step(4);
        TransactionRequest Hit;Hit.Header.Object=F.Key(3);Hit.Header.Event=ID(7401+Cutpoint);
        Hit.Kind=TransactionKind::Damage;Hit.Target=F.Key(0);Hit.RuleId="hit";Hit.Faction=Team::Blue;
        unsigned Interrupted=0,Completed=0;
        Check(F.Client(4).Submit(Hit,[&](const auto& R)
            {if (R.Status==CommitStatus::Interrupted) ++Interrupted;else ++Completed;}).Valid(),"submit real distributed damage before exit");
        bool Reached=Cutpoint==0;
        if (Cutpoint==1)
        {
            // Single-owner resource writes prepare and commit atomically during
            // ReceivePeer. Exercise their prepared numeric boundary directly.
            const auto Requests=F.Client(4).PendingTransactions();OwnerWork Prepared;
            auto Numbers=F.Client(1).NumericState();
            Check(Requests.size()==1&&Numbers.Plan(ID(4),Requests.front(),Prepared)
                &&Numbers.Prepare(ID(1),Prepared)&&!Numbers.LockView().empty(),"reach real numeric prepare before commit");
            Numbers.SealResourceEpoch(Group,2);
            Check(Numbers.LockView().empty()&&!Numbers.Commit(ID(1),Prepared),"sealing prepared work releases locks and prevents its commit");
            Reached=true;
        }
        if (Cutpoint>=2) F.Client(4).PollNetwork(F.Now+.01);
        for (unsigned N=0;!Reached&&N<512&&!F.Queue.empty();++N)
        {
            // Stop at real protocol boundaries; neither commits nor receipts are fabricated.
            const auto& Next=F.Queue.front();
            if (Cutpoint==3&&Next.Value.Header.Kind==MessageKind::TransactionResult
                &&Next.Value.Header.Event==Hit.Header.Event&&Next.To==ID(4)) {Reached=true;break;}
            auto D=std::move(F.Queue.front());F.Queue.pop_front();
            Check(F.Clients.at(D.To)->ReceivePeer(D.From,D.Value),"deliver pre-exit work normally");
            if (Cutpoint==2&&F.Client(1).Objects().at(F.Key(0)).Vitals.Value==75
                &&F.Client(2).Objects().at(F.Key(0)).Vitals.Value==100) Reached=true;
        }
        const auto Pending=F.Client(4).PendingTransactions();
        if (!Reached||Completed||Pending.empty()) std::cerr<<"cutpoint="<<Cutpoint<<" reached="<<Reached<<" completed="<<Completed
            <<" pending="<<Pending.size()<<" owner_health="<<F.Client(1).Objects().at(F.Key(0)).Vitals.Value
            <<" observer_health="<<F.Client(2).Objects().at(F.Key(0)).Vitals.Value<<" queued="<<F.Queue.size()<<'\n';
        Check(Reached&&Completed==0&&std::any_of(Pending.begin(),Pending.end(),[&](const auto& R){return R.Header.Event==Hit.Header.Event;}),"reach requested unfinished resource boundary");
        const auto Health=F.Client(1).Objects().at(F.Key(0)).Vitals.Value;
        auto Late=std::move(F.Queue);F.Queue.clear();
        G.Recovery=20+Cutpoint;G.PreviousOwner=G.Publisher=ID(1);G.Target=ID(2);G.RequestEpoch=2;G.Deadline=F.Now+5;
        for (auto& [P,C]:F.Clients) {C->SetResourceGroups({{Group,G}});C->SetResourceGroups({{Group,G}});}
        Check(Interrupted==1&&Completed==0&&F.Client(4).PendingTransactions().empty(),"each unfinished phase interrupts exactly once");
        for (const auto& D:Late) if (D.Value.Header.ResourceGroup==Group)
            Check(!F.Clients.at(D.To)->ReceivePeer(D.From,D.Value),"old work and results reject after epoch closure");
        F.Step(10);
        for (auto& [P,C]:F.Clients)
        {
            Check(C->Error().empty()&&C->Objects().at(F.Key(0)).Vitals.Value==Health,"restore the selected current value without refund or duplicate damage");
            Check(C->NumericState().LockView().empty(),"exit releases temporary transaction locks");
        }
        Check(Interrupted==1&&Completed==0&&F.Client(4).PendingTransactions().empty(),"polling cannot resurrect an interrupted player request");
        std::cout<<"Resource interruption cutpoint passed phase="<<Cutpoint<<" snapshot_health="<<Health<<'\n';
    }
}
void ResourceSnapshotQueueBounds()
{
    Fixture F;const Id Group=ID(7500);ResourceGroupState G;G.Group=Group;G.Owner=ID(1);G.Backup=ID(2);G.OwnerVersion=G.RequestEpoch=1;
    for (auto& [P,C]:F.Clients)
    {
        auto O=C->Objects().at(F.Key(0));O.Grant.Policy=OwnershipPolicy::SharedGroup;O.Grant.OwnershipGroup=Group;
        O.Grant.GroupEpoch=1;Check(C->Install(O,true),"bounded backup fixture");C->SetResourceGroups({{Group,G}});
    }
    F.RejectResourceSnapshots=true;
    F.Step(1);
    Check(F.Client(1).HandoffMeasurements().at("resource/captured_versions")==1,"first backup is immediate");
    F.Now+=4.8;F.Step(1);
    Check(F.Client(1).HandoffMeasurements().at("resource/captured_versions")==1,"periodic backup does not capture before five seconds");
    F.Now+=.2;F.Step(1);
    Check(F.Client(1).HandoffMeasurements().at("resource/captured_versions")==2,"periodic backup resumes after five seconds");
    for (unsigned N=0;N<512;++N)
    {
        // Advance the offline clock directly; do not wait in real time to prepare replicas.
        F.Now+=5.;F.Step(1);const auto Status=F.Client(1).ResourceSnapshotStatuses().at(Group);
        Check(Status.QueuedVersions<=2,"disconnected backup cannot accumulate snapshot versions");
    }
    Check(F.Client(1).ResourceSnapshotStatuses().at(Group).QueuedVersions==2,"one sending and one latest pending version survive backpressure");
    F.RejectResourceSnapshots=false;F.Step(8);
    Check(F.Client(2).ResourceSnapshotStatuses().at(Group).CompleteCopies==2,"reconnected backup receives complete bounded copies");
    const auto Captured=F.Client(1).HandoffMeasurements().at("resource/captured_versions");
    const auto Transmitted=F.Client(1).HandoffMeasurements().at("resource/transmitted_versions");
    Check(Captured>500&&Transmitted<20,"obsolete pending versions are replaced, never replayed after connection returns");
    std::cout<<"Resource disconnected backup bounds passed captured="<<Captured<<" transmitted="<<Transmitted<<'\n';
}
void AbruptResourceRecoveryContracts(bool Lethal)
{
    auto M=Manifest();M.Rules.at("hit").Damage=Lethal?100:25;
    Fixture F(std::move(M));const Id Group=ID(7300);const std::vector<ObjectKey> Scope{F.Key(0),TeamAccountObject(Team::Red)};
    ResourceGroupState G;G.Group=Group;G.Owner=ID(1);G.Backup=ID(2);G.OwnerVersion=G.RequestEpoch=1;
    for (auto& [P,C]:F.Clients)
    {
        for (const auto K:Scope)
        {auto O=C->Objects().at(K);O.Grant.Policy=OwnershipPolicy::SharedGroup;O.Grant.OwnershipGroup=Group;
            O.Grant.GroupEpoch=1;O.Grant.SimOwner=ID(1);Check(C->Install(O,true),"abrupt resource fixture grant");}
        C->SetResourceGroups({{Group,G}});
    }
    F.Step(3);F.Now+=5.;F.Step(3);
    Check(F.Client(2).ResourceSnapshotStatuses().at(Group).CompleteCopies==2,"designated backup retains two complete cuts");
    F.RejectResourceSnapshots=true;
    TransactionRequest Debit;Debit.Header.Object=F.Key(0);Debit.Header.Event=ID(7301);Debit.Kind=TransactionKind::Reserve;
    Debit.RuleId="reserve";Debit.Faction=Team::Red;Debit.ReservationKey={DeriveEconomyAccount(F.Key(0)),1};
    Check(F.Client(1).Submit(Debit,{}).Valid(),"commit a debit after the last stored cut");
    TransactionRequest Hit;Hit.Header.Object=F.Key(3);Hit.Header.Event=ID(7302);Hit.Kind=TransactionKind::Damage;
    Hit.Target=F.Key(0);Hit.RuleId="hit";Hit.Faction=Team::Blue;
    Check(F.Client(4).Submit(Hit,{}).Valid(),"commit damage after the last stored cut");F.Step(10);
    for (const auto& [P,C]:F.Clients)
        Check(C->Objects().at(F.Key(0)).Vitals.Value==(Lethal?0:75)
            &&C->Objects().at(F.Key(0)).Grant.State==(Lethal?Life::Dead:Life::Alive)
            &&C->NumericState().TeamEconomy().Balance(Team::Red).Blue==80,
            "all peers observed changes beyond the recoverable backup");
    F.Members.at(ID(1)).Online=false;
    G.Recovery=10;G.PreviousOwner=ID(1);G.Publisher=G.Target=ID(2);G.Abrupt=true;G.RequestEpoch=2;G.Deadline=F.Now+5;
    for (auto& [P,C]:F.Clients) if (P!=ID(1))
    {
        C->SetMembers(F.Members);
        for (const auto K:Scope)
        {auto A=C->Objects().at(K).Grant;A.SimOwner={};A.Controller={};++A.ControlEpoch;
            Check(C->InstallGrant(A),"fence disconnected owner and revoke its control before installation");}
        C->SetResourceGroups({{Group,G}});
    }
    F.Step(10);
    std::set<Id> Installed;
    for (const auto& D:F.Metadata) if (D.Value.Header.Kind==MessageKind::ResourceSnapshotInstalled) Installed.insert(D.From);
    Check(Installed.size()==5,"every surviving peer installs the latest complete backup");
    for (const auto& [P,C]:F.Clients) if (P!=ID(1))
        Check(C->Objects().at(F.Key(0)).Vitals.Value==100&&C->Objects().at(F.Key(0)).Grant.State==Life::Alive
            &&F.Adapters.at({P,F.Key(0)})->AppliedHealth==100
            &&C->Objects().at(F.Key(0)).Grant.ControlEpoch==2&&!C->Objects().at(F.Key(0)).Grant.Controller.Valid()
            &&C->NumericState().TeamEconomy().Balance(Team::Red).Blue==100
            &&C->NumericState().TeamEconomy().Snapshot().Pending.empty()&&!C->HasSimulation(F.Key(0)),
            "abrupt recovery rolls all clients back to the same exact cut without early activation");
    for (auto& [P,C]:F.Clients) if (P!=ID(1)) for (const auto K:Scope)
    {auto A=C->Objects().at(K).Grant;A.SimOwner=ID(2);++A.OwnerEpoch;A.GroupEpoch=2;Check(C->InstallGrant(A),"commit recovered resource grants");}
    G.Owner=ID(2);G.Backup=ID(3);G.OwnerVersion=2;G.Recovery=0;G.PreviousOwner=G.Publisher=G.Target={};G.Abrupt=false;G.Deadline=0;
    for (auto& [P,C]:F.Clients) if (P!=ID(1)) {C->SetResourceGroups({{Group,G}});C->SetMembershipPaused(false);}
    for (const auto& [P,C]:F.Clients) if (P!=ID(1))
        Check(C->HasSimulation(F.Key(0))==(P==ID(2)),"the restored living object has exactly one simulator");
    Check(F.Client(3).ResourceSnapshotStatuses().at(Group).CompleteCopies==2,"new backup retains the received cut and its committed authority version before another owner tick");
    // The newly installed owner can disconnect before publishing any new version.
    F.Metadata.clear();F.Members.at(ID(2)).Online=false;
    G.Recovery=11;G.PreviousOwner=ID(2);G.Publisher=G.Target=ID(3);G.Abrupt=true;G.RequestEpoch=3;G.Deadline=F.Now+5;
    for (auto& [P,C]:F.Clients) if (F.Members.at(P).Online)
    {
        C->SetMembers(F.Members);
        for (const auto K:Scope) {auto A=C->Objects().at(K).Grant;A.SimOwner={};Check(C->InstallGrant(A),"fence consecutive disconnected owner");}
        C->SetResourceGroups({{Group,G}});
    }
    F.Step(10);Installed.clear();
    for (const auto& D:F.Metadata) if (D.Value.Header.Kind==MessageKind::ResourceSnapshotInstalled) Installed.insert(D.From);
    Check(Installed.size()==4,"immediate consecutive owner loss restores the retained complete cut");
    std::cout<<"Abrupt rollback and consecutive resource recovery contracts passed lethal="<<Lethal<<'\n';
}
void CurrentStateHistoryIndependence()
{
    Fixture F;const std::vector<ObjectKey> Keys{F.Key(0),TeamAccountObject(Team::Red)};
    TransactionRequest R;R.Header.Object=F.Key(0);R.Header.Event=ID(7200);R.Kind=TransactionKind::Reserve;
    R.RuleId="reserve";R.Faction=Team::Red;R.ReservationKey={DeriveEconomyAccount(F.Key(0)),1};
    Check(F.Client(1).Submit(R,{}).Valid(),"create one live reservation");F.Step();
    const auto Encode=[](const auto& Fragments){Writer W(MaxPrivateStateBytes);for (const auto& V:Fragments) WriteStateFragment(W,V);return W.Take();};
    const auto Before=F.Client(1).NumericState().ExportCurrent(Keys);
    const auto BeforeAge=F.Client(1).NumericState().Objects().at(F.Key(0)).LogicalAge;
    const auto CurrentBytes=Encode(Before).size(),HistoryBytes=Encode(F.Client(1).NumericState().Export(Keys)).size();
    for (unsigned N=0;N<256;++N)
    {auto Extra=R;Extra.Header.Event=ID(7201+N);Extra.RuleId="missing-rule";Check(F.Client(1).Submit(Extra,{}).Valid(),"create completed request history");F.Step(2);}
    const auto After=F.Client(1).NumericState().ExportCurrent(Keys);
    const auto AfterBytes=Encode(After).size(),AfterHistory=Encode(F.Client(1).NumericState().Export(Keys)).size();
    Check(CurrentBytes==AfterBytes&&AfterHistory>HistoryBytes+256*100,"current-state encoding size is independent of completed request history");
    auto Receiver=F.Client(2).NumericState();const auto Identity=F.Client(1).NumericState().ExportCurrentIdentity(Keys);
    const auto IdentityBytes=Receiver.ExportCurrentIdentity(Keys).size();
    for (unsigned N=0;N<256;++N)
    {Authority A;A.Object={ObjectKind::BusinessWork,ID(7600+N),1};A.State=Life::Destroyed;Receiver.ObserveTerminalAuthority(A);}
    Check(Receiver.ExportCurrentIdentity(Keys).size()==IdentityBytes,
        "identity guards exclude unrelated completed work tombstones");
    const auto Balance=Receiver.TeamEconomy().Balance(Team::Red);auto Broken=After;
    std::erase_if(Broken,[&](const auto& V){return V.Key.Object==F.Key(0)&&V.Key.Capability=="tasks";});
    Check(!Receiver.ReplaceCurrent(ID(1),Keys,Broken,Identity)&&Receiver.TeamEconomy().Balance(Team::Red)==Balance,
        "missing required current capability rejects before changing live state");
    Check(Receiver.ReplaceCurrent(ID(1),Keys,After,Identity)
        &&Receiver.TeamEconomy().Snapshot().Pending.size()==1
        &&Receiver.TeamEconomy().Balance(Team::Red)==F.Client(1).NumericState().TeamEconomy().Balance(Team::Red),
        "current snapshot preserves the committed debit and active reservation");
    Check(Receiver.ReplaceCurrent(ID(1),Keys,Before,Identity)
        &&Receiver.Objects().at(F.Key(0)).LogicalAge==BeforeAge,
        "an older complete cut restores its exact logical sampling point");
    std::cout<<"Resource history scaling: current_bytes="<<CurrentBytes<<" -> "<<AfterBytes
        <<" history_bytes="<<HistoryBytes<<" -> "<<AfterHistory<<" completed_requests=256\n";
}
void CurrentMiningLeaseContracts()
{
    const ObjectKey Miner{ObjectKind::Miner,ID(7950),1},Other{ObjectKind::Miner,ID(7951),1};
    MiningReservations Before;
    Check(Before.Configure({{1,1},{2,1},{3,9}}),"configure current mining lease contract");
    const auto Claim=Before.ClaimPosition(Miner,1,0,1,ID(7952));
    Check(Claim.IsCommitted()&&Before.ClaimNode(Miner,Claim.Lease.Token,1,ID(7953)).IsCommitted(),"establish real mining occupancy");
    auto Current=Before.Snapshot();Current.Decisions.clear();
    Check(!ValidMiningReservations(Current),"unproven ordinary lease fragments still require an admission");
    Current.SnapshotLeases=Current.Leases;
    MiningReservations Restored;Check(Restored.Restore(Current),"complete current-cut occupancy is valid without old decisions");
    Check(Restored.Snapshot().Decisions.empty()&&Restored.Snapshot().SnapshotLeases.size()==1
        &&Restored.OwnsNode(Miner,Claim.Lease.Token,1),"only the active lease survives the request epoch boundary");
    MiningReservationState Decoded;
    Check(DecodeMiningReservations(EncodeMiningReservations(Restored.Snapshot()),Decoded)&&Decoded.Decisions.empty()
        &&Decoded.SnapshotLeases==Current.SnapshotLeases,"bounded current lease anchors round trip without historical outcomes");
    auto Invalid=Current;Invalid.SnapshotLeases.at(Miner).Node=2;
    Check(!ValidMiningReservations(Invalid),"a current lease cannot contradict its selected snapshot");
    Invalid=Current;auto Duplicate=Invalid.Leases.at(Miner);Duplicate.Source=Other;Duplicate.Token=ID(7954);
    Invalid.Leases[Other]=Duplicate;Invalid.SnapshotLeases[Other]=Duplicate;
    Check(!ValidMiningReservations(Invalid),"snapshot restore retains exclusive mining positions and nodes");
    const auto Next=Restored.ClaimNode(Miner,Claim.Lease.Token,2,ID(7955));
    Check(Next.IsCommitted()&&Restored.OwnsNode(Miner,Claim.Lease.Token,2)&&ValidMiningReservations(Restored.Snapshot()),
        "a new request advances the restored lease using the new epoch ledger");
    Check(Restored.Release(Miner,Claim.Lease.Token,ID(7956)).IsCommitted()
        &&Restored.Snapshot().SnapshotLeases.empty()&&ValidMiningReservations(Restored.Snapshot()),
        "release discards the current lease anchor");
    Check(Restored.Restore(Current)&&Restored.ClaimPosition(Miner,9,1,2,ID(7957)).IsCommitted()
        &&Restored.Snapshot().SnapshotLeases.empty()&&ValidMiningReservations(Restored.Snapshot()),
        "replacement uses a new admission and discards the previous anchor");
    Check(Restored.Restore(Current)&&Restored.AbandonSource(Miner)&&Restored.Snapshot().SnapshotLeases.empty()
        &&ValidMiningReservations(Restored.Snapshot()),"death releases restored occupancy and its anchor");
    std::cout<<"Current mining lease restore, exclusivity, continuation and release contracts passed\n";
}
void DelayedAcknowledgementFairness()
{
    QueueLimits Limits;Limits.BytesPerSecond[0]=4000;
    PeerChannel Tx(ID(78),Limits),Rx(ID(78),Limits);
    std::vector<Bytes> Acks;std::vector<Id> Received;bool Dropped=false;
    auto Ack=[&](std::span<const std::uint8_t> B){Acks.emplace_back(B.begin(),B.end());return true;};
    auto Apply=[&](Message M){Received.push_back(M.Header.Event);};
    for (unsigned I=0;I<9;++I)
    {
        Message M;M.Header.Kind=MessageKind::Command;M.Header.Match=ID(100);M.Header.Epoch=1;
        M.Header.Sender=ID(1);M.Header.Event=ID(1300+I);M.Body=Bytes(750,static_cast<std::uint8_t>(I));
        Check(Tx.Queue(M,0),"queue delayed-ack cohort");
    }
    for (unsigned Step=0;Step<=12;++Step)
    {
        const double Now=Step*.25;
        Tx.Pump(Now,[&](std::span<const std::uint8_t> Packet)
        {
            Check(Packet.size()<=1000,"fair scheduling retains packet bound");
            if (!Dropped) {Dropped=true;return true;}
            return Rx.Receive(Packet,Now,Ack,Apply);
        });
    }
    std::vector<Id> Expected;for (unsigned I=0;I<9;++I) Expected.push_back(ID(1300+I));
    Check(Received==Expected,"delayed acknowledgements must not starve unsent reliable messages; recover the lost head in order");
    for (const auto& Packet:Acks)
        Check(Tx.Receive(Packet,3.1,[](auto){return true;},[](auto){}),"apply delayed acknowledgement");
    Check(Tx.Unacknowledged().empty(),"every delayed-ack event is eventually acknowledged once");
}
void ReliableCapacityWithBatchedAcknowledgements()
{
    QueueLimits Limits;Limits.BytesPerSecond[0]=512*1024;
    PeerChannel Tx(ID(79),Limits),Rx(ID(79),Limits);std::deque<Bytes> ToReceiver,Acks;
    unsigned Produced=0,Delivered=0;std::size_t Peak=0;
    for (unsigned Step=0;Step<=1260;++Step)
    {
        const double Now=Step/60.;
        while (!Acks.empty())
        {Check(Tx.Receive(Acks.front(),Now,[](auto){return true;},[](auto){}),"apply batched ACK");Acks.pop_front();}
        if (Step<1200) for (unsigned N=0;N<5;++N)
        {
            Message M;M.Header.Kind=MessageKind::Command;M.Header.Match=ID(100);M.Header.Epoch=1;
            M.Header.Sender=ID(1);M.Header.Event=ID(6000+Produced++);M.Body=Bytes(1300,42);
            Check(Tx.Queue(M,Now),"a stream within the byte budget retains queue capacity");
        }
        Tx.Pump(Now,[&](auto B){Check(B.size()<=1000,"packet limit remains 1000 bytes");ToReceiver.emplace_back(B.begin(),B.end());return true;});
        Peak=std::max(Peak,Tx.Metrics().QueuedBytes);
        // An independent client services its input every 250 ms. Its slower
        // game thread must not reduce the configured sender budget to 64/RTT.
        if (Step%15==0) while (!ToReceiver.empty())
        {
            Check(Rx.Receive(ToReceiver.front(),Now,[&](auto B){Acks.emplace_back(B.begin(),B.end());return true;},
                [&](Message M){Check(M.Header.Event==ID(6000+Delivered++),"batched stream is delivered in order once");}),"batched receive");
            ToReceiver.pop_front();
        }
    }
    std::cout<<"Batched ACK capacity delivered="<<Delivered<<'/'<<Produced<<" peak_queue_bytes="<<Peak<<'\n';
    Check(Delivered==Produced && Tx.Unacknowledged().empty(),"a budget-compliant reliable stream drains after a slow peer's ACK batch");
    // A slow transaction worker can receive more than 4096 small application
    // confirmations while still remaining well inside its control byte budget.
    std::promise<void> ReleaseWorker;
    const auto WorkerGate=ReleaseWorker.get_future().share();
    AsyncClientWorld Async(ID(100),ID(1),1,
        [](const ObjectRecord&){return std::make_unique<Adapter>();},
        [](Id,Message,Bytes,bool){return true;},{},false,[WorkerGate]{WorkerGate.wait();});
    struct FReleaseOnExit {std::promise<void>& Event;~FReleaseOnExit(){Event.set_value();}} Release{ReleaseWorker};
    for (unsigned N=0;N<8192;++N) Async.SetMembershipPaused((N&1)!=0);
    Async.PumpApplications();
    Check(Async.Error().empty(),"small reliable application confirmations must use their byte budget, not a 4096-entry cutoff");
}
void FinalEventAfterMetadata()
{
    Fixture F;const auto K=F.Key(0);auto& Receiver=F.Client(2);
    auto Grant=Receiver.Objects().at(K).Grant;Grant.State=Life::Destroyed;
    Check(Receiver.InstallGrant(Grant),"metadata channel can report retirement before the P2P close event");
    Message Final;Final.Header.Kind=MessageKind::ObjectEvent;Final.Header.Match=ID(100);Final.Header.Epoch=1;
    Final.Header.Sender=ID(1);Final.Header.Object=K;Final.Header.OwnerEpoch=1;Final.Header.Event=ID(1200);
    Writer W;W.U64(777);Final.Body=W.Take();
    Check(Receiver.ReceivePeer(ID(1),Final),"ordered final event remains deliverable after metadata retirement");
    Check(F.Adapters.at({ID(2),K})->Position==777,"the final event reached its capability adapter");
    auto Stale=Final;Stale.Header.Kind=MessageKind::Motion;
    Check(!Receiver.ReceivePeer(ID(1),Stale),"retired objects cannot resume ordinary motion");
    Stale=Final;Stale.Header.OwnerEpoch=2;Check(!Receiver.ReceivePeer(ID(1),Stale),"wrong owner version is still rejected");
    Stale=Final;Stale.Header.Object.Generation=2;Check(!Receiver.ReceivePeer(ID(1),Stale),"a different life cannot deliver the old close event");
    Stale=Final;Stale.Header.Epoch=2;Check(!Receiver.ReceivePeer(ID(1),Stale),"a different match epoch is still rejected");
    Stale=Final;Stale.Header.Sender=ID(3);Check(!Receiver.ReceivePeer(ID(3),Stale),"another online peer cannot close the object");
}
void ScopedFragmentIsolation()
{
    auto M=Manifest();OwnerState State(ID(100),ID(2));std::string Error;
    Check(State.Configure(M,Error),"configure isolated fragment receiver");
    const ObjectKey K{ObjectKind::Soldier,ID(20),1},Parent{ObjectKind::Soldier,ID(21),1};
    for (const auto& [Key,O]:M.Objects)
    {
        auto A=O.Grant;A.SimOwner=A.Controller=ID(1);A.OwnerEpoch=A.ControlEpoch=1;A.State=Life::Alive;
        Check(State.SetAuthority(A),"install fragment test owner");
    }
    Check(State.ConfigureResourceCatalog({{1,{1,0,Commodity::Blue,3}},{2,{1,0,Commodity::Blue,3}},{3,{9,0,Commodity::Red,3}}},{Team::Red})
        && State.RegisterResourceMiner(K,10),"configure inventory fragment dependency");
    for (const auto Cluster:{1u,9u})
    {auto V=State.Objects().at(K);V.Grant.Object=MiningClusterObject(Cluster);V.Ordinal=0;
        Check(State.Install(V),"install numeric resource-cluster shard");}
    MiningReservations Occupancy;
    Check(Occupancy.Configure({{1,1},{2,1},{3,9}}),"configure live mining reservation fixture");
    const auto Lease=Occupancy.ClaimPosition(K,1,0,1,ID(3100));
    Check(Lease.IsCommitted()&&Occupancy.ClaimNode(K,Lease.Lease.Token,1,ID(3101)).IsCommitted(),"create a real occupied mining node");
    Writer Occupied;Occupied.U32(static_cast<std::uint32_t>(Occupancy.Snapshot().Leases.size()));
    for (const auto& [Source,L]:Occupancy.Snapshot().Leases)
    {Occupied.Key(L.Source);Occupied.U32(L.Cluster);Occupied.U8(L.Sample);Occupied.U32(L.Task);Occupied.Identity(L.Token);Occupied.U32(L.Node);Occupied.U64(L.Version);}
    Occupied.U32(static_cast<std::uint32_t>(Occupancy.Snapshot().Decisions.size()));
    for (const auto& [Event,D]:Occupancy.Snapshot().Decisions)
    {Occupied.Identity(Event);Occupied.Blob(D.Fingerprint);Occupied.Blob(EncodeMiningReservationOutcome(D.Outcome));}
    Check(State.Import(ID(1),{{{MiningClusterObject(1),"leases/1"},1,1,Occupied.Take()}}),"install authenticated mining state through normal fragment validation");
    {
        std::vector<ObjectKey> Scope;for (const auto& [Key,O]:State.Objects()) if (HasPrivateCustody(O.Grant,ID(1))) Scope.push_back(Key);
        auto Receiver=State;std::string Failure;
        const auto Current=State.ExportCurrent(Scope);const auto Identity=State.ExportCurrentIdentity(Scope);
        if (!Receiver.ReplaceCurrent(ID(1),Scope,Current,Identity,&Failure)) throw std::runtime_error(Failure);
        Check(EncodeResourceLedger(Receiver.WorldResources().Snapshot())==EncodeResourceLedger(State.WorldResources().Snapshot()),
            "complete current numeric snapshot preserves configured ore and empty inventories");
        Check(Receiver.MiningPositions().OwnsNode(K,Lease.Lease.Token,1),"current snapshot retains an occupied mining node without old request receipts");
        const auto Moved=Occupancy.ClaimPosition(K,9,1,2,ID(3102));
        Check(Moved.IsCommitted()&&Occupancy.ClaimNode(K,Moved.Lease.Token,3,ID(3103)).IsCommitted(),
            "advance the same miner to another cluster after the selected recovery cut");
        const auto MiningFragment=[&](std::uint32_t Cluster)
        {
            const auto& Mining=Occupancy.Snapshot();Writer W;std::uint32_t Count=0;
            for (const auto& [Source,L]:Mining.Leases) if (L.Cluster==Cluster) ++Count;
            W.U32(Count);
            for (const auto& [Source,L]:Mining.Leases) if (L.Cluster==Cluster)
            {W.Key(L.Source);W.U32(L.Cluster);W.U8(L.Sample);W.U32(L.Task);W.Identity(L.Token);W.U32(L.Node);W.U64(L.Version);}
            Count=0;for (const auto& [Event,D]:Mining.Decisions) if (D.Outcome.Lease.Cluster==Cluster) ++Count;
            W.U32(Count);
            for (const auto& [Event,D]:Mining.Decisions) if (D.Outcome.Lease.Cluster==Cluster)
            {W.Identity(Event);W.Blob(D.Fingerprint);W.Blob(EncodeMiningReservationOutcome(D.Outcome));}
            return StateFragment{{MiningClusterObject(Cluster),"leases/"+std::to_string(Cluster)},1,2,W.Take()};
        };
        Check(Receiver.Import(ID(1),{MiningFragment(1),MiningFragment(9)})
            &&Receiver.MiningPositions().OwnsNode(K,Moved.Lease.Token,3),"apply a real cross-cluster reservation change");
        for (const bool Reverse:{false,true})
        {
            auto Recovery=Receiver;auto Cut=Current;if (Reverse) std::reverse(Cut.begin(),Cut.end());
            Check(Recovery.ReplaceCurrent(ID(1),Scope,Cut,Identity,&Failure)
                &&Recovery.MiningPositions().OwnsNode(K,Lease.Lease.Token,1)
                &&Recovery.MiningPositions().Snapshot().Decisions.empty(),
                "restore a miner's older cluster atomically in either fragment order without old decisions");
        }
        auto Invalid=Current;
        for (auto& F:Invalid) if (F.Key.Capability=="leases/9") F=MiningFragment(9);
        const auto Before=EncodeMiningReservations(Receiver.MiningPositions().Snapshot());
        Check(!Receiver.ReplaceCurrent(ID(1),Scope,Invalid,Identity,&Failure)
            &&EncodeMiningReservations(Receiver.MiningPositions().Snapshot())==Before,
            "a duplicate miner in two snapshot clusters rejects the complete batch without partial state");
    }
    const std::array<ObjectKey,1> ClusterCut{MiningClusterObject(1)};
    const auto ClusterFragments=State.Export(ClusterCut);
    const auto Has=[&](const char* Name){return std::count_if(ClusterFragments.begin(),ClusterFragments.end(),
        [&](const auto& V){return V.Key.Capability==Name;});};
    Check(Has("ore/1")==1 && Has("ore/2")==1 && Has("leases/1")==1 && !Has("ore/3") && !Has("leases/9"),
        "sparse cluster export contains its own ore and one lease fragment without another cluster");
    auto Object=State.Objects().at(K);Object.Vitals.Value=75;
    auto Fragment=[](const ObjectRecord& O,Revision Version)
    {Writer W;WriteObject(W,O);return StateFragment{{O.Grant.Object,"object"},1,Version,W.Take()};};
    Writer Cargo;Cargo.U8(1);Cargo.U32(10);WriteResources(Cargo,{2,0});Cargo.U64(60);Cargo.U64(60);Cargo.U64(2);
    StateFragment C{{K,"cargo"},1,1,Cargo.Take()};
    auto Bad=C;Bad.Value.pop_back();
    Check(!State.Import(ID(1),{Fragment(Object,1),Bad}),"invalid later fragment rejects the whole import");
    Check(State.Objects().at(K).Vitals.Value==100 && State.WorldResources().Snapshot().Miners.at(K).Cargo.Blue==0,
        "rejected import leaves prior object and inventory untouched");
    Check(State.Import(ID(1),{Fragment(Object,1),C}),"object and cargo validate and apply together");
    Check(State.Objects().at(K).Vitals.Value==75 && State.WorldResources().Snapshot().Miners.at(K).Cargo.Blue==2
        && State.WorldResources().Snapshot().Remaining.at(1)==3 && State.Objects().at(Parent).Vitals.Value==100,
        "scoped import preserves unrelated world state");
    Check(State.RegisterResourceMiner(Parent,10),"configure another inventory in the same imported ledger");
    const auto CargoFragment=[](ObjectKey Key,std::int32_t Amount,Revision Version)
    {Writer W;W.U8(1);W.U32(10);WriteResources(W,{Amount,0});W.U64(60);W.U64(60);W.U64(2);
        return StateFragment{{Key,"cargo"},1,Version,W.Take()};};
    const auto BeforeBatch=EncodeResourceLedger(State.WorldResources().Snapshot());
    Check(!State.Import(ID(1),{CargoFragment(K,3,2),CargoFragment(Parent,11,1)}),
        "final resource batch validation rejects a later over-capacity inventory");
    Check(EncodeResourceLedger(State.WorldResources().Snapshot())==BeforeBatch && State.Version({K,"cargo"})==1,
        "invalid final batch cannot leak an earlier inventory or fragment version");
    Check(State.Import(ID(1),{CargoFragment(K,3,2),CargoFragment(Parent,4,1)})
        && State.WorldResources().Snapshot().Miners.at(K).Cargo.Blue==3
        && State.WorldResources().Snapshot().Miners.at(Parent).Cargo.Blue==4
        && State.WorldResources().Snapshot().Remaining.at(1)==3,
        "one validated resource batch imports both inventories and preserves unrelated ore");
    auto Birth=State.Objects().at(Parent);Birth.Grant.Object.StableId=ID(3000);Birth.Parent=Parent;Birth.Grant.SourceLease=ID(3001);Birth.Ordinal=0;
    Check(State.Import(ID(1),{Fragment(Birth,1)}),"newborn authorization resolves a parent outside the fragment set");
    FrozenWorkRecipe Recipe;auto& R=Recipe.Request;R.Kind=TransactionKind::Upgrade;R.RuleId="upgrade";R.Faction=Team::Red;
    R.Header.Kind=MessageKind::Transaction;R.Header.Match=ID(100);R.Header.Epoch=1;R.Header.Sender=ID(1);
    R.Header.Object=Parent;R.Header.Event=R.RootEvent=ID(3002);R.Header.OwnerEpoch=R.Header.ControlEpoch=R.Header.Sequence=1;
    R.Target=Parent;
    auto Work=State.Objects().at(Parent);Work.Definition=TransactionWorkDefinition;Work.Ordinal=0;Work.Parent=Parent;
    Work.Grant.Object={ObjectKind::BusinessWork,StableId("GuLiTransactionWork/v1",R.Header.Event.ToString()),1};
    Work.Grant.SourceLease=StableId("GuLiTransactionAdmission/v1",R.Header.Event.ToString());Work.FrozenWork=EncodeFrozenWork(Recipe);
    FrozenWorkRecipe Decoded;Check(DecodeFrozenWork(Work,Decoded) && State.Install(Work),"install unrelated admitted work");
    Writer Task;Task.U8(1);Task.Key(Work.Grant.Object);
    Check(State.Import(ID(1),{{{Parent,"tasks"},1,1,Task.Take()}}),"task index can refer to an existing work outside this import");
    Object.Vitals.Value=50;
    Check(State.Import(ID(1),{Fragment(Object,2)}),"unrelated work survives another scoped commit");
    auto InvalidWork=Work;InvalidWork.FrozenWork[0]^=1;
    Object.Vitals.Value=25;
    Check(!State.Import(ID(1),{Fragment(Object,3),Fragment(InvalidWork,1)}),"invalid work reference rejects all staged changes");
    Check(State.Objects().at(K).Vitals.Value==50 && State.Objects().at(Work.Grant.Object).FrozenWork==Work.FrozenWork,
        "failed work validation cannot leak a partial object update");
}
void AsyncCurrentMiningReadback()
{
    // Numeric-only resource shards must invalidate readback without relying on
    // a materialized actor's BeginResourceRestore callback.
    auto M=Manifest();for (auto& [K,O]:M.Objects) O.Materialized=false;
    M.Numbers["resource_map/node_count"]=2;M.Numbers["resource_map/Clusters/count"]=1;
    M.Numbers["resource_map/Territories/count"]=1;M.Numbers["resource_map/Territories[0]/InitialOwner"]=1;
    for (const auto& [Name,Value]:std::map<std::string,double>{{"ClusterId",1},{"TerritoryIndex",0},{"ResourceType",0},{"FirstNodeIndex",0},{"NodeCount",2}})
        M.Numbers["resource_map/Clusters[0]/"+Name]=Value;
    for (unsigned N=1;N<=2;++N)
    {const auto P="ore/"+std::to_string(N)+"/";M.Numbers[P+"cluster"]=1;M.Numbers[P+"type"]=0;M.Numbers[P+"amount"]=3;}
    OwnerState Source(ID(100),ID(1));std::string Error;Check(Source.Configure(M,Error),"configure numeric-only recovery world");
    std::vector<ObjectKey> Scope;
    for (const auto& [K,O]:Source.Objects())
    {
        auto A=O.Grant;A.SimOwner=A.Controller=ID(1);A.OwnerEpoch=A.ControlEpoch=1;A.State=Life::Alive;
        Check(Source.SetAuthority(A),"install numeric-only resource ownership");Scope.push_back(K);
    }
    const ObjectKey Miner{ObjectKind::Soldier,ID(20),1};MiningReservations Mining;
    Check(Mining.Configure({{1,1},{2,1}}),"configure readback mining catalog");
    const auto Lease=Mining.ClaimPosition(Miner,1,0,1,ID(3300));
    Check(Lease.IsCommitted()&&Mining.ClaimNode(Miner,Lease.Lease.Token,1,ID(3301)).IsCommitted(),"claim initial readback node");
    const auto Capture=[&](Revision Epoch)
    {
        const auto& V=Mining.Snapshot();Writer W;W.U32(static_cast<std::uint32_t>(V.Leases.size()));
        for (const auto& [K,L]:V.Leases)
        {W.Key(L.Source);W.U32(L.Cluster);W.U8(L.Sample);W.U32(L.Task);W.Identity(L.Token);W.U32(L.Node);W.U64(L.Version);}
        W.U32(static_cast<std::uint32_t>(V.Decisions.size()));
        for (const auto& [E,D]:V.Decisions) {W.Identity(E);W.Blob(D.Fingerprint);W.Blob(EncodeMiningReservationOutcome(D.Outcome));}
        Check(Source.Import(ID(1),{{{MiningClusterObject(1),"leases/1"},1,Epoch,W.Take()}}),"apply real mining changes before capture");
        ObjectHandoffState S;S.Match=ID(100);S.Source=ID(1);S.Target=ID(2);S.CurrentOnly=true;S.ResourceEpoch=Epoch;
        for (const auto& [K,O]:Source.Objects()) S.Objects.push_back({O,0,{}});
        S.Numeric=Source.ExportCurrent(Scope);S.CurrentIdentity=Source.ExportCurrentIdentity(Scope);return S;
    };
    const auto Older=Capture(1);
    Check(Mining.ClaimNode(Miner,Lease.Lease.Token,2,ID(3302)).IsCommitted(),"advance real mining occupancy after the older cut");
    const auto Newer=Capture(2);
    {
        AsyncClientWorld A(ID(100),ID(2),1,[](const ObjectRecord&){return std::make_unique<Adapter>();},
            [](Id,Message,Bytes,bool){return true;},{},false,{},0,{false,{}});
        const auto Until=[&](auto Done)
        {
            const auto End=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while (std::chrono::steady_clock::now()<End) {A.PumpApplications();if (Done()) return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            Check(false,"numeric-only recovery readback timed out");
        };
        Check(A.Configure(M,Error),"configure async numeric-only receiver");
        Member Member1;Member1.Player=ID(1);Member1.Faction=Team::Red;Member1.Ready=true;
        auto Member2=Member1;Member2.Player=ID(2);A.SetMembers({{ID(1),Member1},{ID(2),Member2}});
        for (const auto& [K,O]:Source.Objects()) Check(A.Install(O,true),"install numeric-only receiver record");
        Until([&]{return A.Objects().size()==Scope.size()&&!A.IsBootstrapMaterializationDeferred();});
        unsigned Observed=0;
        A.SetQueryObserver([&](const ClientQueryDelta& D)
        {
            if (!D.MiningChanged) return;++Observed;
            for (const auto& [K,L]:D.Leases) Check(A.NumericState().MiningPositions().Snapshot().Leases.at(K)==L,
                "mining query callback sees the restored complete read mirror");
        });
        const auto Install=[&](const ObjectHandoffState& Cut,const char* Name)
        {
            std::string Result;
            Until([&]
            {
                if (!A.Inspect(Name,[Cut](ClientWorld& C){return C.ImportObjects(Cut)?"done":C.LastApplicationPending()?"pending":"failed";},Result)) return false;
                Check(Result!="failed","authenticated numeric-only cut imports");return Result=="done";
            });
        };
        Install(Newer,"newer mining cut");const auto Version=A.NumericState().MiningPositions().Snapshot().Version;
        Check(A.NumericState().MiningPositions().Snapshot().Leases.at(Miner).Node==2,"newer mining cut reaches the main thread");
        const auto Query=A.NumericState().MiningDirectoryRevision;
        Install(Older,"older mining cut");
        std::cout<<"Async mining restore version="<<Version<<" -> "
            <<A.NumericState().MiningPositions().Snapshot().Version<<" node="<<A.NumericState().MiningPositions().Snapshot().Leases.at(Miner).Node
            <<" query="<<Query<<" -> "<<A.NumericState().MiningDirectoryRevision<<" callbacks="<<Observed<<'\n';
        Check(A.NumericState().MiningPositions().Snapshot().Version==Version
            &&A.NumericState().MiningPositions().Snapshot().Leases.at(Miner).Node==1
            &&A.NumericState().MiningDirectoryRevision>Query&&Observed==2,
            "same aggregate version does not hide recovered mining state in the ordered incremental delivery");
    }
    std::cout<<"Async current mining snapshot readback and callback order contracts passed\n";
}
void PlannedStateIsolation()
{
    const ObjectKey Source{ObjectKind::Soldier,ID(20),1},Target{ObjectKind::Soldier,ID(23),1};
    const ObjectKey ShieldA{ObjectKind::Soldier,ID(24),1},ShieldB{ObjectKind::Soldier,ID(25),1};
    auto M=Manifest();
    CapabilityContract Contract;Contract.Name="acceptance_read_dependency";Contract.Version=1;
    Contract.ValidateState=[](const auto&,const auto&){return true;};
    Contract.ValidateRule=[](const auto&){return false;};
    Contract.Prepare=[](const auto&,auto&){return CommitStatus::InvalidRule;};
    Contract.ValidateTransition=[](const auto&,const auto&){return true;};
    Contract.ResolveRule=[ShieldB](const auto&,const auto& Objects,const auto&,auto&)
    {return Objects.contains(ShieldB) && Objects.at(ShieldB).Definition=="data/unit";};
    Check(RegisterCapability(std::move(Contract)),"register a data-defined read dependency outside the write set");
    M.Objects.at(Source).Capabilities["acceptance_read_dependency"]={1,{1}};
    for (const auto K:{ShieldA,ShieldB})
    {auto& O=M.Objects.at(K);O.Supplied=true;O.ShieldRadius=100;O.Vitals.Shield=O.Vitals.MaximumShield=K==ShieldA?5:7;}
    Rule Spawn;Spawn.Kind=TransactionKind::Spawn;Spawn.SourceDefinition=Spawn.TargetDefinition="data/unit";
    Spawn.SpawnKind=ObjectKind::Soldier;Spawn.SpawnHealth=100;Spawn.MaximumLiveChildren=1;M.Rules["birth"]=Spawn;
    Rule Parent;Parent.Kind=TransactionKind::Relation;Parent.SourceDefinition=Parent.TargetDefinition="data/unit";
    Parent.Relation=RelationKind::Parent;Parent.RelationValue=true;M.Rules["parent"]=Parent;
    OwnerState State(ID(100),ID(4));std::string Error;Check(State.Configure(M,Error),Error.c_str());
    std::map<Id,Member> Members;
    for (const unsigned N:{1u,4u})
    {Member V;V.Player=ID(N);V.Faction=N==1?Team::Red:Team::Blue;V.Seat=Role::Commander;V.Ready=true;V.OrdinalSlot=N;Members.emplace(V.Player,V);}
    State.SetMembers(Members);
    auto Initial=State.Objects();for (const auto& [K,O]:Initial)
    {auto A=O.Grant;A.SimOwner=A.Controller=ID(A.Faction==Team::Red?1:4);A.OwnerEpoch=A.ControlEpoch=1;A.State=Life::Alive;Check(State.SetAuthority(A),"install isolated plan grants");}
    auto Request=[&](ObjectKey K,Id Sender,unsigned Event,TransactionKind Kind,const char* Rule)
    {
        TransactionRequest R;R.Header.Match=ID(100);R.Header.Epoch=1;R.Header.Kind=MessageKind::Transaction;
        R.Header.Object=K;R.Header.Sender=Sender;R.Header.Event=R.RootEvent=ID(Event);
        R.Header.OwnerEpoch=R.Header.ControlEpoch=R.Header.Sequence=1;R.Header.At=R.StartedAt=60;
        R.Kind=Kind;R.RuleId=Rule;R.Faction=State.Objects().at(K).Grant.Faction;return R;
    };
    auto Hit=Request(Source,ID(1),4000,TransactionKind::Damage,"hit");Hit.Target=Target;Hit.DamageAmount=25;
    OwnerWork W;Check(State.Plan(ID(1),Hit,W) && W.Result.Status==CommitStatus::Committed && W.Result.Barriers.size()==2
        && W.Result.Vitals.Value==87,"planning reads all shield/dependency objects in stable order");
    Check(State.Objects().at(Target).Vitals.Value==100 && State.Objects().at(ShieldA).Vitals.Shield==5
        && State.Objects().at(ShieldB).Vitals.Shield==7,"planning cannot leak health or shield writes");
    Check(State.Prepare(ID(4),W),"prepare isolated plan");State.Cancel(ID(4),W.Event,W.Proposal);
    Check(State.Objects().at(Target).Vitals.Value==100,"cancel leaves numeric state intact");
    Check(State.Plan(ID(1),Hit,W) && State.Prepare(ID(4),W) && State.Commit(ID(4),W),"commit isolated target and barriers");
    Check(State.Commit(ID(4),W) && State.Objects().at(Target).Vitals.Value==87
        && State.Objects().at(ShieldB).Vitals.Shield==0 && State.Objects().at(Source).Vitals.Value==100,"duplicate commit preserves once-only writes");
    const auto FirstDecision=W.Result;
    OwnerWork Replay;Check(State.Plan(ID(1),Hit,Replay) && Replay.Result.Status==CommitStatus::Duplicate
        && Replay.Changes.empty(),"retry returns the stored decision without new mutations");
    const auto DecisionBytes=EncodeCommitDecision(FirstDecision);
    Check(EncodeCommit(FirstDecision)!=EncodeCommit(Replay.Result)
        && DecisionBytes==EncodeCommitDecision(Replay.Result),"receipt status changes the wire, not the full settlement decision");
    auto Changed=Replay.Result;Changed.Vitals.Value-=1;
    Check(DecisionBytes!=EncodeCommitDecision(Changed),"decision comparison detects changed health");
    Changed=Replay.Result;Changed.RewardEvent=ID(4999);
    Check(DecisionBytes!=EncodeCommitDecision(Changed),"decision comparison detects changed reward identity");
    Changed=Replay.Result;Changed.Objects.front().Grant.Object.Generation+=1;
    Check(DecisionBytes!=EncodeCommitDecision(Changed),"decision comparison detects changed lifecycle identity");
    Changed=Replay.Result;Changed.Balance.Blue+=1;
    Check(DecisionBytes!=EncodeCommitDecision(Changed),"decision comparison detects changed account balance");
    Changed=Replay.Result;Changed.Status=CommitStatus::InvalidRule;
    Check(DecisionBytes!=EncodeCommitDecision(Changed),"a rejection cannot compare equal to a commit");
    auto Link=Request(Target,ID(4),4001,TransactionKind::Relation,"parent");Link.Target=Target;Link.Related=ShieldA;
    Check(State.Plan(ID(4),Link,W) && W.Result.Status==CommitStatus::Committed
        && !State.Objects().at(Target).Parent.Valid(),"parent lookup outside the staged writes remains available");
    Check(State.Prepare(ID(4),W) && State.Commit(ID(4),W) && State.Objects().at(Target).Parent==ShieldA,"relation commit");
    auto Cycle=Request(ShieldA,ID(4),4002,TransactionKind::Relation,"parent");Cycle.Target=ShieldA;Cycle.Related=Target;
    Check(State.Plan(ID(4),Cycle,W) && W.Result.Status==CommitStatus::InvalidRule && !State.Objects().at(ShieldA).Parent.Valid(),"full ancestor lookup rejects a cycle without leaking a partial link");
    auto Birth=Request(Target,ID(4),4003,TransactionKind::Spawn,"birth");Birth.Target={ObjectKind::Soldier,ID(4004),1};
    Check(State.Plan(ID(4),Birth,W) && W.Result.Births.size()==1 && !State.Objects().contains(Birth.Target),"birth is isolated until commit");
    Check(State.Prepare(ID(4),W) && State.Commit(ID(4),W) && State.Objects().at(Birth.Target).Parent==Target,"birth with a preexisting parent commits");
    auto NextBirth=Request(Target,ID(4),4005,TransactionKind::Spawn,"birth");NextBirth.Target=Birth.Target;++NextBirth.Target.Generation;
    Check(State.Plan(ID(4),NextBirth,W) && W.Result.Status==CommitStatus::InvalidRule,"a new life cannot overlap the old live child");
    auto Retired=State.Objects().at(Birth.Target).Grant;Retired.State=Life::Destroyed;
    Check(State.SetAuthority(Retired),"retire the original child without erasing its identity");
    const auto Archive=State.Objects().at(Birth.Target);
    for (unsigned N=0;N<6000;++N)
    {
        auto O=Archive;O.Grant.Object.StableId=ID(10000+N);O.Ordinal=10000+N;
        Check(State.Install(O),"retain unrelated retired lives for pending event lookup");
    }
    NextBirth.Header.Event=NextBirth.RootEvent=ID(4006);NextBirth.Target.Generation=3;
    Check(State.Plan(ID(4),NextBirth,W) && W.Result.Status==CommitStatus::InvalidRule,"historical generation cannot be skipped");
    NextBirth.Header.Event=NextBirth.RootEvent=ID(4007);NextBirth.Target.Generation=2;
    Check(State.Plan(ID(4),NextBirth,W) && W.Result.Status==CommitStatus::Committed && W.Result.Births.size()==1,
        "retired history neither occupies the live child limit nor prevents its next life");
    Check(State.Prepare(ID(4),W) && State.Commit(ID(4),W),"the next life enters the live query index at commit");
}
void ControlPointSupplyProjection()
{
    const auto* Contract=FindCapability(ControlPointCapability);Check(Contract && Contract->Version==ControlPointCapabilityVersion,"control point supply contract loaded");
    const auto* Maintenance=FindCapability(ResourceMaintenanceCapability);Check(Maintenance!=nullptr,"resource clock contract loaded");
    const ObjectKey PointKey{ObjectKind::Territory,ID(4200),1},FacilityKey{ObjectKind::Building,ID(4201),1},DeadKey{ObjectKind::Building,ID(4202),1};
    ObjectRecord Point;Point.Grant.Object=PointKey;Point.Grant.State=Life::Alive;Point.RelationVersion=1;Point.Supplied=true;
    ControlPointState P;P.Index=P.UpkeepRank=0;P.Owner=Team::Red;P.Progress=-1;P.FirstCapture=ID(4203);
    Check(SetControlPoint(Point,P),"point definition");
    ObjectRecord Facility;Facility.Grant.Object=FacilityKey;Facility.Parent=PointKey;Facility.Definition="data/any-facility";
    Facility.Grant.State=Life::Alive;Facility.Grant.Faction=Team::Red;Facility.Grant.Controller=ID(1);
    Facility.Grant.SimOwner=ID(2);Facility.Grant.ControlEpoch=Facility.Grant.OwnerEpoch=1;Facility.RelationVersion=1;Facility.Supplied=true;
    auto Dead=Facility;Dead.Grant.Object=DeadKey;Dead.Grant.State=Life::Dead;
    std::map<ObjectKey,ObjectRecord> Objects{{PointKey,Point},{FacilityKey,Facility},{DeadKey,Dead}};
    Economy Accounts;Check(Accounts.Begin({0,0},{0,0}) && Accounts.Open(),"empty team economy");
    ResourceLedger Resources;Check(Resources.Configure({{1,{1,0,Commodity::Blue,3}}},{Team::Red}),"point resource catalog");
    std::map<Id,Member> Members;
    for (unsigned N:{1u,4u}) {Member M;M.Player=ID(N);M.Faction=N==1?Team::Red:Team::Blue;M.Seat=Role::Commander;M.Ready=true;Members.emplace(M.Player,M);}
    ObjectRecord Runtime;Runtime.Grant.Object={ObjectKind::BusinessWork,ID(4210),1};Runtime.Grant.State=Life::Alive;Runtime.RelationVersion=1;
    Check(SetResourceMaintenance(Runtime,{}),"resource runtime clock");Objects.emplace(Runtime.Grant.Object,Runtime);
    Rule Operation;Operation.CapabilityOperation="upkeep";
    TransactionRequest R;R.Header.Object=R.Target=Runtime.Grant.Object;R.Header.Event=ID(4204);R.Header.At=1800;R.Faction=Team::Red;R.ExpectedRelationVersion=1;
    auto Prepare=[&](CapabilityDelta& Delta) {return (Operation.CapabilityOperation=="upkeep"?Maintenance:Contract)->Prepare({R,Operation,Objects.at(R.Header.Object),Objects,Accounts,Resources,Members},Delta);};
    CapabilityDelta Lost;Check(Prepare(Lost)==CommitStatus::Committed && Lost.Objects.size()==3
        && !Lost.Objects.at(PointKey).Supplied && !Lost.Objects.at(FacilityKey).Supplied,"unpaid supply reaches living facilities in the same delta");
    Check(Objects.at(PointKey).Supplied && Objects.at(FacilityKey).Supplied && Objects.at(DeadKey).Supplied,"preparation cannot mutate existing facilities or dead state");
    Check(Maintenance->ValidateTransition(Facility,Lost.Objects.at(FacilityKey))
        && Lost.Objects.at(FacilityKey).Grant.SimOwner==ID(2) && Lost.Objects.at(FacilityKey).Grant.ControlEpoch==1,"supply-only change preserves simulation and control grants");
    auto Invalid=Lost.Objects.at(FacilityKey);Invalid.X=99;
    Check(!Maintenance->ValidateTransition(Facility,Invalid),"a supply transition cannot also move a facility");
    for (const auto& [K,O]:Lost.Objects) Objects.insert_or_assign(K,O);Accounts=*Lost.Accounts;
    CapabilityDelta Duplicate;R.ExpectedRelationVersion=Objects.at(Runtime.Grant.Object).RelationVersion;
    Check(Prepare(Duplicate)==CommitStatus::InvalidRule,"the same upkeep cycle cannot charge twice");
    Check(Accounts.Add(Team::Red,{1,1}),"fund next ordinary upkeep");R.Header.At=3600;R.Header.Event=ID(4205);
    CapabilityDelta Restored;Check(Prepare(Restored)==CommitStatus::Committed && Restored.Objects.at(PointKey).Supplied
        && Restored.Objects.at(FacilityKey).Supplied && Restored.Accounts->Balance(Team::Red).Blue==0,"paid upkeep restores attached facilities and debits once");
    Check(!Objects.at(FacilityKey).Supplied && Accounts.Balance(Team::Red).Blue==1,"prepared payment remains isolated");
    for (const auto& [K,O]:Restored.Objects) Objects.insert_or_assign(K,O);Accounts=*Restored.Accounts;
    auto New=Facility;New.Supplied=false;
    Check(DeriveSpawnCapabilities(Objects.at(PointKey),Objects,R,New) && New.Supplied,"new facility inherits current point supply without a type route");
    Operation.CapabilityOperation="capture";R.Header.Object=R.Target=PointKey;R.Faction=Team::None;R.Header.Event=ID(4206);R.Header.At=4800;
    R.ExpectedRelationVersion=Objects.at(PointKey).RelationVersion;R.CapabilityPayload=EncodeControlPointSample(0,1);
    CapabilityDelta Capture;Check(Prepare(Capture)==CommitStatus::Committed && Capture.Objects.at(FacilityKey).Grant.Faction==Team::Blue
        && Capture.Objects.at(FacilityKey).Grant.Controller==ID(4) && Capture.Objects.at(FacilityKey).Grant.SimOwner==ID(2)
        && Capture.Objects.at(FacilityKey).Supplied && Contract->ValidateTransition(Objects.at(FacilityKey),Capture.Objects.at(FacilityKey)),
        "capture updates control and inherited supply while preserving the existing simulator");
}
void LateBirthAcrossHandoff()
{
    Fixture F;const auto Source=F.Key(1);auto& Old=F.Client(2);
    ObjectRecord Birth=Old.Objects().at(Source);Birth.Grant.Object={ObjectKind::Soldier,ID(4200),1};
    Birth.Parent=Source;Birth.Ordinal=4200;Birth.Grant.SourceLease=ID(4201);
    Birth.Grant.Policy=OwnershipPolicy::RetainedSource;
    Writer BirthBytes;WriteObject(BirthBytes,Birth);
    Writer SourceBytes;WriteObject(SourceBytes,Old.Objects().at(Source));
    std::vector<StateFragment> Values{{{Source,"object"},1,1,SourceBytes.Take()},
        {{Birth.Grant.Object,"object"},1,1,BirthBytes.Take()}};
    Writer Body;Body.U32(static_cast<std::uint32_t>(Values.size()));for (const auto& V:Values) WriteStateFragment(Body,V);
    Message Delayed;Delayed.Header.Match=ID(100);Delayed.Header.Epoch=1;Delayed.Header.Sender=ID(2);
    Delayed.Header.Kind=MessageKind::StateFragments;Delayed.Header.Object=Source;Delayed.Body=Body.Take();
    Check(Old.ReceivePeer(ID(2),Delayed),"source commits its child before moving");
    ObjectHandoff T;T.IdValue=42;T.Object=Source;T.Source=ID(2);T.Target=ID(1);
    T.PreviousOwnerVersion=1;T.NextOwnerVersion=2;T.TargetPrepared=true;
    for (auto& [P,C]:F.Clients) Check(C->PrepareHandoff(T),"prepare late birth handoff");
    Check(Old.FreezeHandoff(T),"capture source with completed birth decision");F.Flush();
    auto Grant=Old.Objects().at(Source).Grant;Grant.SimOwner=ID(1);Grant.OwnerEpoch=2;
    for (auto& [P,C]:F.Clients) Check(C->CommitHandoff(T,Grant),"commit late birth handoff");F.Flush();
    // Grant-first and P2P-first deliveries must converge without replaying the
    // source's old authority, or giving the new source owner the retained child.
    for (const auto P:{1u,3u,4u,5u,6u})
    {
        auto& C=F.Client(P);
        if (P%2) Check(C.InstallGrant(Birth.Grant),"late child room admission");
        Check(C.ReceivePeer(ID(2),Delayed),"late child state accepted or held for its grant");
        if (!(P%2)) Check(C.InstallGrant(Birth.Grant),"room admission after child state");
        C.PollNetwork(1);
        Check(C.Objects().contains(Birth.Grant.Object) && C.Objects().at(Birth.Grant.Object).Grant.SimOwner==ID(2)
            && C.Objects().at(Source).Grant.SimOwner==ID(1) && C.RejectedStateImports()==0,
            "retained child survives source handoff in either arrival order");
    }
    auto Forged=Birth;Forged.Grant.Object.StableId=ID(4202);Writer Invalid;WriteObject(Invalid,Forged);
    const std::vector<StateFragment> ForgedParts{{{Forged.Grant.Object,"object"},1,1,Invalid.Take()}};
    OwnerState Observer(ID(100),ID(3));std::string Error;Check(Observer.Configure(F.Data,Error),"late birth rejection setup");
    Check(Observer.SetAuthority(Grant) && !Observer.Import(ID(2),ForgedParts),"old source owner cannot invent an unadmitted child");
    auto Recovered=Birth;Recovered.Grant.SimOwner=ID(3);Recovered.Grant.OwnerEpoch=2;
    Writer RecoveredBody;WriteObject(RecoveredBody,Recovered);
    const std::vector<StateFragment> RecoveredParts{{{Recovered.Grant.Object,"object"},1,1,RecoveredBody.Take()}};
    Check(!Observer.Import(ID(3),RecoveredParts),"new owner version requires the exact room admission");
    const std::map<ObjectKey,Authority> Admitted{{Recovered.Grant.Object,Recovered.Grant}};
    Check(Observer.Import(ID(3),RecoveredParts,false,&Admitted),"a receiver missing the original birth accepts its exact recovered grant and state");
}
void ResourceCopyIsolation()
{
    const ObjectKey Cargo{ObjectKind::Soldier,ID(4100),1},Factory{ObjectKind::Building,ID(4101),1};
    ResourceLedger Base;Check(Base.Configure({{1,{1,0,Commodity::Blue,3}}},{Team::Red})
        && Base.RegisterMiner(Cargo,Team::Red,10) && Base.RegisterFactory(Factory,Team::Red,10),"configure shared resource read view");
    const auto Original=EncodeResourceLedger(Base.Snapshot());auto Trial=Base;
    Check(Trial.Extract(Cargo,1,ID(4102),0).IsCommitted() && Trial.Deliver(Cargo,Factory,ID(4103),0).IsCommitted(),"copied resource plan can extract and deliver");
    Check(Original==EncodeResourceLedger(Base.Snapshot()),"trial extraction and delivery cannot mutate its source");
    auto Deferred=Trial;Check(Deferred.Process(Factory,ID(4104),0,[](Team,const Resources&){return false;}).Status==ResourceStatus::Deferred,
        "credit deferral leaves work pending");
    Check(EncodeResourceLedger(Deferred.Snapshot())==EncodeResourceLedger(Trial.Snapshot()),"deferred credit cannot consume the queue");
    auto Committed=Trial;Check(Committed.Process(Factory,ID(4104),0,[](Team,const Resources&){return true;}).IsCommitted(),"processing can commit on a separate branch");
    Check(Committed.Snapshot().Processed.Blue==1 && Trial.Snapshot().Processed.Blue==0 && ResourceLedger::Queued(Trial.Snapshot().Factories.at(Factory))==1,
        "processing and deduplication history stay private to the committing branch");
    Check(Base.SetTerritoryOwner(0,Team::Blue) && Trial.Snapshot().Owners[0]==Team::Red,"the original can mutate independently too");
}
void AsyncExternalSendBindsPackingIdentity(bool FastCacheKey)
{
    // Bootstrap callers provide a body/kind; the session binds the actual
    // room and sender before transmission. An identity-based codec must see
    // those same fields before it produces or reuses cached compressed bytes.
    const auto Encode=[](const Message& M)
    {
        Writer W;W.Identity(M.Header.Match);W.Identity(M.Header.Sender);W.U64(M.Header.Epoch);
        W.Identity(M.Header.Event);W.Key(M.Header.Object);W.Blob(M.Body);return W.Take();
    };
    unsigned Delivered=0,Compressed=0;
    const std::array<std::uint8_t,8> Contents{17,17,18,17,18,19,19,17};
    AsyncClientWorld A(ID(100),ID(1),7,[](const ObjectRecord&){return std::make_unique<Adapter>();},
        [&](Id Peer,const Message& M,const Bytes& Packed,bool Encoded)
        {
            Check(Peer==ID(2),"external peer destination remains unchanged");
            Check(Delivered<Contents.size() && M.Body==Bytes(1024,Contents[Delivered]),
                "same envelope with different equal-sized bodies retains exact delivery order");
            Check(M.Header.Match==ID(100) && M.Header.Sender==ID(1) && M.Header.Epoch==7,
                "native transport receives canonical identity without rewriting its borrowed buffers");
            Check(Encoded && Packed==Encode(M),"external messages bind transport identity before codec/cache");
            ++Delivered;return true;
        },[&](Message& M,Bytes& Packed){++Compressed;Packed=Encode(M);return true;},
        false,{},0,{false,{},true,FastCacheKey});
    for (unsigned I=0;I<Contents.size();++I)
    {
        Message M;M.Header.Kind=MessageKind::BootstrapState;M.Header.Event=ID(6000);
        M.Header.Object={ObjectKind::Soldier,ID(6001),1};M.Body.resize(1024,Contents[I]);
        if (I%2) {M.Header.Match=ID(91);M.Header.Sender=ID(92);M.Header.Epoch=99;}
        Check(A.SendPeer(ID(2),std::move(M)),"admit external bootstrap message");
    }
    const auto Deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (Delivered<Contents.size() && std::chrono::steady_clock::now()<Deadline)
    {A.PumpApplications();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    Check(Delivered==Contents.size() && A.Error().empty(),"all external messages retain order and payload");A.Stop();
    Check(Compressed==3,"both cache indexes reuse only the three exact payload variants");
}
void AsyncIndependentOutboundProgress()
{
    {
        bool Blocked=true;std::map<Id,std::vector<unsigned>> Delivered;
        AsyncClientWorld A(ID(100),ID(1),1,[](const ObjectRecord&){return std::make_unique<Adapter>();},
            [&](Id Peer,const Message& M,const Bytes&,bool)
            {
                if (Peer==ID(2) && Blocked) return false;
                Delivered[Peer].push_back(M.Body.at(0));return true;
            },{},false,{},0,{false,{}});
        for (unsigned N=1;N<=2;++N) for (const auto Peer:{ID(2),ID(3)})
        {
            Message M;M.Header.Kind=MessageKind::BootstrapState;M.Body={static_cast<std::uint8_t>(N)};
            Check(A.SendPeer(Peer,std::move(M)),"admit interleaved bootstrap destinations");
        }
        const auto Until=[&](auto Predicate)
        {
            const auto End=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while (std::chrono::steady_clock::now()<End)
            {A.PumpApplications();if (Predicate()) return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            return false;
        };
        Check(Until([&]{return A.Statistics().InputKinds[unsigned(AsyncInputKind::PeerSend)].Processed==4;}),"all sends reach worker");
        Check(Until([&]{return Delivered[ID(3)].size()==2;}),"blocked destination cannot hold independent peer bootstrap");
        Check(Delivered[ID(2)].empty(),"blocked messages remain undelivered");
        Blocked=false;
        Check(Until([&]{return Delivered[ID(2)].size()==2 && Delivered[ID(3)].size()==2
            && A.Statistics().OutputBytes==0;}),"unblocked peers drain retained bytes");
        for (const auto Peer:{ID(2),ID(3)})
            Check(Delivered[Peer]==std::vector<unsigned>({1,2}),"each peer retains exact order and once-only delivery");
        for (unsigned I=0;I<4;++I) A.PumpApplications();
        Check(Delivered[ID(2)].size()==2 && Delivered[ID(3)].size()==2,"later pumps do not replay completed sends");
        A.Stop();
    }
}
void AsyncMainAdvanceEligibility(std::uint32_t WorkerPeriod=1)
{
    Fixture F;std::map<ObjectKey,Adapter*> Applied;
    AsyncClientWorld A(ID(100),ID(1),1,[&](const ObjectRecord& O)
        {auto P=std::make_unique<Adapter>();P->WorkerPeriod=WorkerPeriod;P->MainPeriod=WorkerPeriod?6:0;P->WakeOnCommand=true;Applied[O.Grant.Object]=P.get();return P;},
        [](Id,Message,Bytes,bool){return true;},{},false,{},0,{true,{}});
    auto Until=[&](auto Predicate,const char* Why)
    {
        const auto End=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        do {A.PumpApplications();if (Predicate()) return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        while (std::chrono::steady_clock::now()<End);
        Check(false,Why);
    };
    std::string Error;Check(A.Configure(F.Data,Error),"eligibility configure");A.SetMembers(F.Members);
    for (const auto& [K,O]:F.Client(1).Objects()) Check(A.Install(O,true),"eligibility install");
    A.SetMembershipPaused(false);
    Until([&]{return A.HasSimulation(F.Key(0))&&!A.IsBootstrapMaterializationDeferred();},"eligibility ready");
    const auto Step=[&]
    {
        const auto Before=A.ObjectNow(F.Key(0));A.Advance(1./60.);
        Until([&]{return A.ObjectNow(F.Key(0))>Before;},"skipped callbacks still advance the logical clock");
    };
    for (unsigned I=0;I<12;++I) Step();
    auto* Scene=Applied.at(F.Key(0));
    if (!WorkerPeriod)
    {
        Check(A.ObjectNow(F.Key(0))==12 && A.Objects().at(F.Key(0)).LogicalAge==12
            && Scene->Position==0 && Scene->LastTick==0,
            "registration with no periodic work still publishes every main-thread object clock step");
        A.Stop();return;
    }
    Check(A.ObjectNow(F.Key(0))==12 && Scene->Position==2 && Scene->LastTick==12,
        "60 Hz object age retains the existing 10 Hz due callbacks");
    FrozenCommand Command;Command.Event=ID(4990);Command.Operation="move";Command.Members.push_back({F.Key(0),1,1});
    Check(A.Route(Command),"idle object accepts its normal command");
    Until([&]{return A.CommandResult(Command.Event).has_value();},"idle object command receipt does not wait for periodic work");
    Check(Scene->Executions==1 && Scene->MainAwake,"command wakes the main adapter exactly once");
    Step();
    Check(Scene->Position==3 && Scene->LastTick==13 && !Scene->MainAwake,
        "command wake is observed on the immediately following logical step");
    for (unsigned I=0;I<5;++I) Step();
    Check(Scene->Position==4 && Scene->LastTick==18 && A.Error().empty(),"waking does not shift the periodic cadence");
    Scene->MainPeriod=0;
    for (unsigned I=0;I<5;++I) Step();
    Check(A.ObjectNow(F.Key(0))==23 && Scene->Position==4 && Scene->LastTick==18,
        "an empty family callback can be disabled without skipping any object clock step");
    A.Stop();
}
void SharedMembershipImport()
{
    struct Shared final:ISharedStateAdapter
    {
        std::vector<ObjectKey> Keys;unsigned Restores=0;
        std::uint32_t Version() const override {return 1;}
        bool UsesObject(ObjectKey K) const override {return std::find(Keys.begin(),Keys.end(),K)!=Keys.end();}
        bool Capture(const std::vector<ObjectKey>& Objects,Bytes& Out) const override
        {Writer W;W.U32(static_cast<std::uint32_t>(Objects.size()));Out=W.Take();return true;}
        bool Validate(const std::vector<PrivateSystemState>& Parts,const std::vector<ObjectKey>& Objects) const override
        {std::uint32_t Count=0;return Parts.size()==1 && Parts.front().Objects==Objects
            && [&]{Reader R(Parts.front().State);return R.U32(Count)&&Count==Objects.size()&&R.Done();}();}
        bool Restore(const std::vector<PrivateSystemState>& Parts,const std::vector<ObjectKey>& Objects,RestoreStage) override
        {if (!Validate(Parts,Objects)) return false;++Restores;return true;}
        bool RestoreQuery(const std::vector<PrivateSystemState>& Parts,const std::vector<ObjectKey>& Objects,RestoreStage Stage) override
        {return Restore(Parts,Objects,Stage);}
    };
    Fixture F;auto Extra=F.Client(2).Objects().at(F.Key(1));Extra.Grant.Object.StableId=ID(6270);Extra.Ordinal=77;
    for (auto& [P,C]:F.Clients) Check(C->Install(Extra,true),"declare related source in the same private cut");
    const std::vector<ObjectKey> Keys{F.Key(1),Extra.Grant.Object};
    F.Adapters.at({ID(2),F.Key(1)})->SharedSystems={"recorded-work"};
    F.Adapters.at({ID(1),F.Key(1)})->SharedSystems={"recorded-work"};
    auto Source=std::make_unique<Shared>();Source->Keys=Keys;
    Check(F.Client(2).RegisterSystem("recorded-work",std::move(Source)),"register required and incidental shared members");
    ObjectHandoffState Cut;Check(F.Client(2).CaptureObjects(Keys,ID(1),0,Cut),"capture shared member superset");
    Check(Cut.Systems.size()==1 && Cut.Systems.front().Objects==Keys,"private cut retains the related source too");
    auto Target=std::make_unique<Shared>();auto* TargetPtr=Target.get();
    Check(F.Client(1).RegisterSystem("recorded-work",std::move(Target)),"register exact-membership validator");
    auto Bad=Cut;Bad.Systems.front().Objects.erase(Bad.Systems.front().Objects.begin());
    TargetPtr->Capture(Bad.Systems.front().Objects,Bad.Systems.front().State);
    Check(!F.Client(1).ImportObjects(Bad),"a missing required shared member still rejects");
    Bad=Cut;Bad.Systems.front().Objects.push_back(F.Key(0));
    TargetPtr->Capture(Bad.Systems.front().Objects,Bad.Systems.front().State);
    Check(!F.Client(1).ImportObjects(Bad),"a shared member outside the private cut still rejects");
    Check(TargetPtr->Restores==0,"invalid membership cannot reach private restoration");
    Check(F.Client(1).ImportObjects(Cut),"validate the complete declared shared cut, not only its required subset");
    Check(TargetPtr->Restores==4,"all four restore stages retain the complete shared membership");
    {
        AsyncExecutionOptions Options;
        AsyncClientWorld A(ID(100),ID(1),1,[&](const ObjectRecord& O)
            {auto P=std::make_unique<Adapter>();if (O.Grant.Object==F.Key(1)) P->SharedSystems={"recorded-work"};return P;},
            [](Id,Message,Bytes,bool){return true;},{},false,{},0,Options);
        auto System=std::make_unique<Shared>();auto* SystemPtr=System.get();
        Check(A.RegisterSystem("recorded-work",std::move(System)),"register async shared validation");
        std::string Error;Check(A.Configure(F.Data,Error),"configure async shared cut target");A.SetMembers(F.Members);
        for (const auto& [K,O]:F.Client(1).Objects()) Check(A.Install(O,true),"install shared cut directory");
        A.SetMembershipPaused(false);
        const auto Until=[&](auto Predicate,const char* Why)
        {
            const auto End=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            do {A.PumpApplications();if (Predicate()) return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            while (std::chrono::steady_clock::now()<End);Check(false,Why);
        };
        Until([&]{return !A.IsBootstrapMaterializationDeferred()&&A.HasSimulation(F.Key(0));},"shared import target ready");
        const std::array<ObjectHandoffState,1> Parts{Cut};
        Until([&]{return A.StageBootstrapDirectory(Parts);},"shared import preflight ready");
        Until([&]{return A.ImportBootstrap(Parts,[&](Id SourceId,const OwnerState::FragmentVisitor& Visit)
            {if (SourceId!=Cut.Source) return false;for (const auto& V:Cut.Numeric) if (!Visit(V)) return false;return true;});},
            "async shared cut accepts required plus incidental members");
        Check(SystemPtr->Restores==4 && A.Error().empty(),"async shared cut restores exactly four stages without an error");
    }
}
void AsyncContinuationAndGameplayDelivery(unsigned Workers=0)
{
    Fixture F;const auto Main=std::this_thread::get_id();std::map<ObjectKey,Adapter*> Applied;
    AsyncClientWorld A(ID(100),ID(1),1,[&](const ObjectRecord& O)
        {Check(std::this_thread::get_id()==Main,"scene creation stays on caller thread");auto P=std::make_unique<Adapter>();Applied[O.Grant.Object]=P.get();return P;},
        [](Id,Message,Bytes,bool){return true;},{},false,{},0,{true,Workers?MakeGameplayTaskPool(Workers):nullptr});
    auto Until=[&](auto Predicate,const char* Why)
    {
        const auto End=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        do {A.PumpApplications();if (Predicate()) return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        while (std::chrono::steady_clock::now()<End);
        std::cerr<<"async failure: "<<Why<<" world="<<A.Error()<<" pending="<<A.PendingTransactions().size()<<'\n';
        const auto Stats=A.Statistics();std::cerr<<"inputs="<<Stats.InputBytes<<" output="<<Stats.OutputBytes<<" reserved="<<Stats.ResultReservedBytes<<'\n';
        std::string Diagnosis;
        for (unsigned Try=0;Try<100;++Try)
        {
            if (A.Inspect("failure",[](ClientWorld& C){std::string S="paused="+std::to_string(C.IsPaused());for (const auto& [K,V]:C.WorkCounts()) S+=" "+K+"="+std::to_string(V);return S;},Diagnosis)) break;
            A.PumpApplications();std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::cerr<<Diagnosis<<'\n';
        for (const auto& [K,O]:A.Objects()) if (K.Kind==ObjectKind::Soldier)
            std::cerr<<K.StableId.ToString()<<" owner="<<O.Grant.SimOwner.ToString()<<" control="<<O.Grant.Controller.ToString()
                <<" epochs="<<O.Grant.OwnerEpoch<<'/'<<O.Grant.ControlEpoch<<" executions="<<Applied.at(K)->Executions<<'\n';
        Check(false,Why);
    };
    std::string Error;Check(A.Configure(F.Data,Error),"async configure accepted");A.SetMembers(F.Members);
    for (const auto& [K,O]:F.Client(1).Objects()) Check(A.Install(O,true),"async directory install accepted");
    A.SetMembershipPaused(false);
    Until([&]{return A.HasSimulation(F.Key(0))&&!A.IsBootstrapMaterializationDeferred();},"async creation confirms without blocking worker");
    FrozenCommand Command;Command.Event=ID(4800);Command.Operation="move";Command.Members.push_back({F.Key(0),1,1});
    Check(A.Route(Command),"async command accepted");A.Advance(1./60.);
    // Withhold GT applications deliberately. The old MainCall would stop the
    // worker before this independent inspection could run.
    std::atomic<bool> Inspected=false;std::string Inspection;
    A.Inspect("pending-command-progress",[&](ClientWorld&){Inspected=true;return std::string("ready");},Inspection);
    const auto Deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while (!Inspected && std::chrono::steady_clock::now()<Deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(Inspected,"pending command cannot block independent worker inputs");
    Until([&]{return A.CommandResult(Command.Event).has_value();},"command resumes after its application acknowledgment");
    Check(Applied.at(F.Key(0))->Executions==1,"command applies exactly once");
    auto Other=F.Client(1).Objects().at(F.Key(2));Other.Grant.SimOwner=ID(1);
    Check(A.Install(Other,true),"install another local command domain");
    Until([&]{return A.HasSimulation(F.Key(2));},"second local domain ready");
    FrozenCommand Multi=Command;Multi.Event=ID(4801);Multi.Members.push_back({F.Key(2),1,1,1});
    Check(A.Route(Multi),"one event addresses two independent objects");A.Advance(1./60.);
    Until([&]{return A.CommandResult(Multi.Event).has_value();},"multi-object command confirms");
    const auto FirstExecutions=Applied.at(F.Key(0))->Executions,OtherExecutions=Applied.at(F.Key(2))->Executions;
    Check(A.RebuildFromReplica(F.Key(0))&&A.RebuildFromReplica(F.Key(2)),"enqueue both replica rebuilds");
    Until([&]
    {
        return Applied.at(F.Key(0))->Executions==FirstExecutions+1&&Applied.at(F.Key(2))->Executions==OtherExecutions+1;
    },"same event rebuilds separate object subsets without sharing application results");
    Check(Applied.at(F.Key(0))->Executions==FirstExecutions+1&&Applied.at(F.Key(2))->Executions==OtherExecutions+1,
        "each restored object replays its latest command once");
    struct Calculation final:IGameplayCalculation
    {
        bool CanRunParallel() const override {return true;}
        std::atomic<bool> Computed=false;std::thread::id Thread;unsigned Value=0;
        void Compute() override {Thread=std::this_thread::get_id();Value=100;Computed=true;}
    };
    auto Work=std::make_shared<Calculation>();unsigned Completed=0;
    if (Workers)
    {
        struct HeldCalculation final:IGameplayCalculation
        {
            bool CanRunParallel() const override {return true;}
            std::atomic<bool> Started=false,Release=false;
            void Compute() override
            {
                Started=true;const auto End=std::chrono::steady_clock::now()+std::chrono::seconds(3);
                while (!Release&&std::chrono::steady_clock::now()<End) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
        auto Held=std::make_shared<HeldCalculation>();auto Following=std::make_shared<Calculation>();
        struct OwnerCalculation final:IGameplayCalculation
        {
            std::atomic<bool> Computed=false;std::thread::id Thread;
            void Compute() override {Thread=std::this_thread::get_id();Computed=true;}
        };
        auto Stateful=std::make_shared<OwnerCalculation>();std::thread::id OwnerThread;
        std::vector<unsigned> ApplicationOrder;std::atomic<bool> Independent=false;std::string Value;
        Check(A.SubmitGameplay("held",Held,1024,1024,[&]{ApplicationOrder.push_back(1);})
            &&A.SubmitGameplay("following",Following,1024,1024,[&]{ApplicationOrder.push_back(2);})
            &&A.SubmitGameplay("codec-owner",Stateful,1024,1024,[&]{ApplicationOrder.push_back(3);}),"admit independent pool work");
        Until([&]{return Held->Started.load();},"pool starts without a main-thread wait");
        A.Inspect("pool-progress",[&](ClientWorld&){OwnerThread=std::this_thread::get_id();Independent=true;return std::string("ready");},Value);
        const auto End=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        while ((!Independent||!Stateful->Computed||(Workers>1&&!Following->Computed))&&std::chrono::steady_clock::now()<End)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        A.PumpApplications();const bool StayedOrdered=ApplicationOrder.empty();Held->Release=true;
        Check(Independent,"in-flight computation cannot block transaction-owner inputs");
        Check(Stateful->Computed&&Stateful->Thread==OwnerThread&&OwnerThread!=Main,"stateful codecs keep their network-owner affinity");
        Check(Workers==1||Following->Computed,"independent computations can finish out of order");
        Check(StayedOrdered,"completion does not cross an earlier uncompleted batch");
        Until([&]{return ApplicationOrder.size()==3;},"pool results drain in admission order");
        Check(ApplicationOrder==std::vector<unsigned>{1,2,3},"ordered application survives parallel completion");
    }
    Check(A.SubmitGameplay("movement",Work,1024,1024,[&]
        {Check(std::this_thread::get_id()==Main && Work->Thread!=Main,"compute/application thread ownership");Applied.at(F.Key(0))->Position+=Work->Value;++Completed;}),"gameplay batch reserves its result before admission");
    Check(!A.SubmitGameplay("movement",Work,1024,1024,[]{}),"one unconfirmed batch per dependency group");
    Check(!A.SubmitGameplay("too-large",Work,1024,65ull*1024*1024,[]{}),"oversized result is rejected before computation");
    ObjectHandoffState Cut;const std::array<ObjectKey,1> Keys{F.Key(0)};
    Until([&]{return A.CaptureObjects(Keys,ID(2),0,Cut);},"capture waits for applied movement and its confirmation");
    Check(Completed==1 && Work->Computed && !A.HasPendingGameplay("movement"),"accepted gameplay completes once and releases the dependency group");
    Reader Captured(Cut.Objects.front().State);std::uint64_t Position=0;
    Check(Captured.U64(Position)&&Position==Applied.at(F.Key(0))->Position,"private cut includes acknowledged movement result");
    F.Step(2);ObjectHandoffState Import;const std::array<ObjectKey,1> Remote{F.Key(1)};
    Check(F.Client(2).CaptureObjects(Remote,ID(1),0,Import),"prepare existing-format private state");
    const std::array<ObjectHandoffState,1> Parts{Import};
    Until([&]{return A.StageBootstrapDirectory(Parts);},"preflight resumes across GT confirmations");
    Until([&]{return A.ImportBootstrap(Parts,[&](Id Source,const OwnerState::FragmentVisitor& Visit)
        {if (Source!=Import.Source) return false;for (const auto& V:Import.Numeric) if (!Visit(V)) return false;return true;});},"four restore stages resume without blocking");
    Check(Applied.at(F.Key(1))->Position==F.Adapters.at({ID(2),F.Key(1)})->Position,"restored private state matches captured source");
    Check(A.Error().empty(),"resumable paths retain a healthy world");
    auto LargeA=std::make_shared<Calculation>(),LargeB=std::make_shared<Calculation>(),Refused=std::make_shared<Calculation>();
    unsigned LargeApplied=0;
    Check(A.SubmitGameplay("capacity-a",LargeA,20ull*1024*1024,31ull*1024*1024,[&]{++LargeApplied;})
        &&A.SubmitGameplay("capacity-b",LargeB,20ull*1024*1024,31ull*1024*1024,[&]{++LargeApplied;}),"reserve two near-capacity reliable results");
    Check(!A.SubmitGameplay("capacity-c",Refused,1024,4ull*1024*1024,[]{}),"backpressure refuses new computation before admission");
    std::atomic<bool> ControlRan=false;
    struct Confirmation final:IGameplayCalculation {std::atomic<bool>& Done;explicit Confirmation(std::atomic<bool>& D):Done(D){}void Compute() override {Done=true;}};
    Check(A.ConfirmGameplay(std::make_shared<Confirmation>(ControlRan),128),"control lane accepts confirmation under data backpressure");
    const auto ComputeEnd=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while ((!LargeA->Computed||!LargeB->Computed||!ControlRan)&&std::chrono::steady_clock::now()<ComputeEnd)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(LargeA->Computed&&LargeB->Computed&&ControlRan,"accepted calculations and control progress without GT pumping");
    Check(A.Statistics().InputBytes>=40ull*1024*1024,"computed snapshots retain the shared input budget until application confirmation");
    Until([&]{return LargeApplied==2&&ControlRan&&!A.HasPendingGameplay("capacity-b");},"near-capacity results and controls drain without a mutual wait");
    Check(!Refused->Computed&&A.Error().empty(),"refused work never computes and accepted results remain reliable");
    ObjectHandoffState CapacityCut;
    Check(!A.CaptureObjects(Keys,ID(2),0,CapacityCut),"new capture starts asynchronously");
    const auto CaptureDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while (A.Statistics().OutputBytes<32ull*1024*1024&&std::chrono::steady_clock::now()<CaptureDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(A.Statistics().OutputBytes>=32ull*1024*1024,"capture reservation is queued before stress output");
    std::string LargeObservation;bool ObservationDone=false,CutDone=false;
    A.Inspect("capture-capacity",[](ClientWorld&){return std::string(40ull*1024*1024,'x');},LargeObservation);
    Until([&]
    {
        if (!ObservationDone) ObservationDone=A.Inspect("capture-capacity",[](ClientWorld&){return std::string{};},LargeObservation);
        if (!CutDone) CutDone=A.CaptureObjects(Keys,ID(2),0,CapacityCut);
        return ObservationDone&&CutDone;
    },"GT capture transfers capacity before its worker acknowledgment, avoiding the output/control cycle");
    Check(LargeObservation.size()==40ull*1024*1024&&A.Statistics().OutputPeak<=64ull*1024*1024,
        "capture and large reliable output finish inside the 64 MiB budget");
    if (!Workers)
    {
        const auto CapturesBefore=Applied.at(F.Key(0))->Captures;
        ObjectHandoffState DelayedCut;
        Check(!A.CaptureObjects(Keys,ID(2),42,DelayedCut),"start a delayed application capture");
        const auto QueuedUntil=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while (A.Statistics().OutputBytes<32ull*1024*1024&&std::chrono::steady_clock::now()<QueuedUntil)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Check(A.Statistics().OutputBytes>=32ull*1024*1024,"delayed capture reached the GT queue");
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        Until([&]{return A.CaptureObjects(Keys,ID(2),42,DelayedCut);},"a delayed acknowledgment retains its fresh capture result");
        Check(Applied.at(F.Key(0))->Captures==CapturesBefore+1,
            "GT queue delay cannot expire the private cut before its first consumption");
        // A capture fence can coalesce many ordinary frame deltas. Passing the
        // sum through BattleClock's per-call four-step clamp loses game time.
        const auto ClockBefore=A.Now();A.Advance(.5/60.);ObjectHandoffState ClockCut;
        Check(!A.CaptureObjects(Keys,ID(2),43,ClockCut),"start a clock-retention capture");
        const auto ClockQueued=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while (A.Statistics().OutputBytes<32ull*1024*1024&&std::chrono::steady_clock::now()<ClockQueued)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Check(A.Statistics().OutputBytes>=32ull*1024*1024,"clock-retention capture is waiting for GT");
        for (unsigned Frame=0;Frame<20;++Frame) A.Advance(1./60.);
        Until([&]{return A.CaptureObjects(Keys,ID(2),43,ClockCut);},"consume the clock-retention capture");
        Until([&]{return A.Now()>=ClockBefore+20;},"capture fence must retain all twenty submitted logical steps");
        Check(A.Now()==ClockBefore+20,"capture catch-up neither loses nor invents simulation time");
        A.Advance(.5/60.);
        Until([&]{return A.Now()==ClockBefore+21;},"four-step slicing must preserve the clock's pre-existing fractional tick");

        // A periodic snapshot requester can disappear or change scope after a
        // birth/command. Its completed cut must not hold a half-second lease on
        // unrelated simulation while waiting for a consumer that will not return.
        std::string Abandoned;bool Begun=false;
        Until([&]
        {
            return A.Inspect("abandoned-periodic-cut",[&](ClientWorld& C)
            {ObjectHandoffState Ignored;Begun=!C.CaptureObjects(Keys,ID(2),44,Ignored);return std::string("requested");},Abandoned);
        },"queue an explicitly unconsumed periodic cut");
        Check(Begun,"orphaned snapshot began asynchronously");
        const auto OrphanClock=A.Now();A.Advance(1./60.);
        const auto ResumeBy=std::chrono::steady_clock::now()+std::chrono::milliseconds(200);
        while (A.Now()==OrphanClock&&std::chrono::steady_clock::now()<ResumeBy)
        {A.PumpApplications();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        Check(A.Now()==OrphanClock+1,"completed orphaned capture cannot impose a 500 ms simulation stall");
        std::cout<<"Async snapshot capture clock retention and orphan release contracts passed\n";
    }
    unsigned AfterStop=0;auto Last=std::make_shared<Calculation>();
    Check(A.SubmitGameplay("shutdown",Last,1024,1024,[&]{++AfterStop;}),"admit final batch before shutdown");
    A.Stop();Check(AfterStop==0,"shutdown revokes callbacks and joins without pumping the scene");
}
void AsyncCaptureChainClockProgress(bool Yield)
{
    Fixture F;std::map<ObjectKey,Adapter*> Applied;
    AsyncClientWorld A(ID(100),ID(1),1,[&](const ObjectRecord& O)
        {auto P=std::make_unique<Adapter>();Applied[O.Grant.Object]=P.get();return P;},
        [](Id,Message,Bytes,bool){return true;},{},false,{},0,{false,nullptr,Yield});
    const auto Until=[&](auto Predicate,const char* Why)
    {
        const auto End=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        do {A.PumpApplications();if (Predicate()) return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        while (std::chrono::steady_clock::now()<End);
        Check(false,Why);
    };
    std::string Error;Check(A.Configure(F.Data,Error),"capture chain configure");
    A.SetMembers(F.Members);
    for (const auto& [K,O]:F.Client(1).Objects()) Check(A.Install(O,true),"capture chain install");
    A.SetMembershipPaused(false);
    Until([&]{return A.HasSimulation(F.Key(0))&&!A.IsBootstrapMaterializationDeferred();},"capture chain ready");
    const std::array<ObjectKey,1> Keys{F.Key(0)};
    constexpr unsigned Count=8,Steps=24;
    std::array<ObjectHandoffState,Count> Cuts;
    std::array<bool,Count> Done{};
    for (unsigned I=0;I<Count;++I)
        Check(!A.CaptureObjects(Keys,ID(2),100+I,Cuts[I]),"queue serial private cuts");
    const auto QueuedBy=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while (A.Statistics().OutputBytes<32ull*1024*1024&&std::chrono::steady_clock::now()<QueuedBy)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(A.Statistics().OutputBytes>=32ull*1024*1024,"first cut holds the simulation clock");
    const auto Before=A.Now();
    for (unsigned I=0;I<Steps;++I) A.Advance(1./60.);
    // Confirm the input order without pumping the first private capture.
    std::atomic<bool> InputsReady=false;std::string Observation;
    A.Inspect("capture-chain-inputs",[&](ClientWorld&){InputsReady=true;return std::string("ready");},Observation);
    const auto InputsBy=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while (!InputsReady&&std::chrono::steady_clock::now()<InputsBy)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(InputsReady,"all ordinary frame deltas precede capture acknowledgments");
    Until([&]
    {
        bool Complete=true;
        for (unsigned I=0;I<Count;++I)
        {
            if (!Done[I]) Done[I]=A.CaptureObjects(Keys,ID(2),100+I,Cuts[I]);
            Complete&=Done[I];
        }
        return Complete;
    },"all serial captures complete");
    Tick First=0,Last=0;
    for (unsigned I=0;I<Count;++I)
    {
        Reader R(Cuts[I].Objects.front().State);std::uint64_t Position=0,Executions=0,At=0;
        Check(R.U64(Position)&&R.U64(Executions)&&R.U64(At)&&R.Done(),"captured adapter bytes remain valid");
        Check(At==Cuts[I].Objects.front().Record.LogicalAge,"private adapter and numeric state share the same logical cut");
        if (!I) First=At;
        Check(At>=Last,"serial cut clocks never regress");Last=At;
    }
    Check(Yield?Last>First:Last==First,"simulation advances between cuts only with capture fairness enabled");
    Until([&]{return A.Now()>=Before+Steps;},"every submitted step eventually applies");
    Check(A.Now()==Before+Steps&&A.Error().empty(),"capture scheduling neither loses nor invents logical time");
    std::cout<<"Async capture chain clock progress passed yield="<<Yield<<" first="<<First<<" last="<<Last<<" total="<<Steps<<'\n';
}
void BoundedReliableApplicationAdmission()
{
    QueueLimits Limits;Limits.MaxReassemblyBytes=500;Limits.ResendSeconds=.01;
    PeerChannel Sender(ID(8000),Limits),Receiver(ID(8000),Limits);
    std::deque<Bytes> Wire,Acks;std::vector<Id> Delivered;bool Ready=false;
    const auto Apply=[&](const Message& M)
    {if (M.Header.Kind==MessageKind::Command&&!Ready) return false;Delivered.push_back(M.Header.Event);return true;};
    const auto Ack=[&](std::span<const std::uint8_t> B){Acks.emplace_back(B.begin(),B.end());return true;};
    const auto Receive=[&](double At)
    {
        while (!Wire.empty()) {auto B=std::move(Wire.front());Wire.pop_front();Receiver.ReceiveWithAdmission(B,At,Ack,Apply);}
        while (!Acks.empty()) {auto B=std::move(Acks.front());Acks.pop_front();Sender.Receive(B,At,[](auto){return true;},[](auto){});}
    };
    Message M;M.Header.Kind=MessageKind::Command;M.Header.Match=ID(100);M.Header.Sender=ID(1);M.Body.resize(280,42);
    for (unsigned N=0;N<3;++N) {M.Header.Event=ID(8100+N);Check(Sender.Queue(M,0),"queue reliable application");}
    Sender.Pump(.1,[&](auto B){Wire.emplace_back(B.begin(),B.end());return true;});Receive(.1);
    Check(Delivered.empty()&&Receiver.ReceivedThrough(Lane::Control)==0,"ACK cannot advance the application boundary");
    Check(Receiver.Metrics().PendingDeliveryMessages==1&&Receiver.Metrics().PendingDeliveryBytes<=500,
        "ACKed backpressured message remains bounded and retained");
    Check(Sender.Unacknowledged().size()==2,"overflow messages remain sender-owned until receiver capacity returns");
    M.Header.Kind=MessageKind::HandoffState;M.Header.Event=ID(8200);M.Body={1};
    Check(Sender.Queue(M,.2),"independent lane accepted");Sender.Pump(.2,[&](auto B){Wire.emplace_back(B.begin(),B.end());return true;});Receive(.2);
    Check(Delivered==std::vector<Id>{ID(8200)},"blocked application lane does not block independent delivery");
    Ready=true;Receiver.DrainDeliveries(Apply);
    for (unsigned I=1;I<40&&Delivered.size()<4;++I)
    {Sender.Pump(.2+I*.02,[&](auto B){Wire.emplace_back(B.begin(),B.end());return true;});Receive(.2+I*.02);Receiver.DrainDeliveries(Apply);}
    Check(Delivered==std::vector<Id>{ID(8200),ID(8100),ID(8101),ID(8102)},"retained and retried messages finish once in lane order");
    Check(Receiver.Metrics().PendingDeliveryMessages==0&&Sender.Unacknowledged().empty(),"all reliable storage drains after recovery");
}
void CompactPeerEnvelopeRoundTrip()
{
    std::uint64_t Original=0,Compact=0;
    for (unsigned Kind=unsigned(MessageKind::Hello);Kind<=unsigned(MessageKind::ResourceSnapshotInstalled);++Kind)
        for (unsigned Variant=0;Variant<5;++Variant)
    {
        Message M;auto& H=M.Header;H.Kind=MessageKind(Kind);H.Match=ID(100);H.Sender=ID(1);H.Epoch=1;H.Sequence=5000;H.At=90000;
        if (Variant!=1) H.Object={ObjectKind::Flight,ID(9010),Variant==3?UINT32_MAX:1u};
        if (Variant!=2) H.Event=ID(9100+Kind);
        H.OwnerEpoch=H.ControlEpoch=Variant==3?UINT64_MAX:2;
        if (Variant==3) H.At=H.Epoch=H.Sequence=UINT64_MAX;
        if (Variant==4) {H.Object.Kind=ObjectKind::None;H.Object.Generation=0;H.Event={};}
        M.Body=Bytes(Variant==3?4096:48,std::uint8_t(Kind));Bytes A,B,C;
        Check(EncodeMessage(M,A)&&EncodePeerMessage(M,B),"encode canonical and compact message");
        Message R;Check(DecodeMessage(B,R)&&EncodeMessage(R,C)&&C==A,"compact transport preserves every canonical byte including identities");
        Check(B.size()<=A.size(),"compact representation never inflates worst-case counters");
        Bytes Bound;Check(EncodePeerMessage(M,Bound,{H.Match,H.Sender})&&DecodeMessage(Bound,R,{H.Match,H.Sender})
            &&EncodeMessage(R,C)&&C==A,"authenticated connection identities reconstruct canonical bytes");
        Check(!DecodeMessage(Bound,R),"connection-relative envelope is rejected without its authenticated context");
        auto Foreign=M;Foreign.Header.Match=ID(9999);Foreign.Header.Sender=ID(9998);Bytes CanonicalForeign;
        Check(EncodePeerMessage(Foreign,Bound,{H.Match,H.Sender})&&DecodeMessage(Bound,R,{H.Match,H.Sender})
            &&EncodeMessage(R,C)&&EncodeMessage(Foreign,CanonicalForeign)&&C==CanonicalForeign,
            "explicit stale match/sender survive encoding and remain available to rejection checks");
        if (Variant==0) {Original+=A.size();Compact+=B.size();}
        for (std::size_t N=0;N<B.size();++N) Check(!DecodeMessage(std::span(B).first(N),R),"truncated compact message rejected");
        B.push_back(0);Check(!DecodeMessage(B,R),"trailing compact bytes rejected");
    }
    Message M;M.Header.Epoch=1;Bytes B;Check(EncodePeerMessage(M,B),"encode zero identity test");
    Message R;auto Bad=B;Bad[7]=16;Check(!DecodeMessage(Bad,R),"unknown compact flags rejected");
    Bad=B;Bad[24]=0x81;Bad.insert(Bad.begin()+25,0);Check(!DecodeMessage(Bad,R),"noncanonical varint rejected");
    Bad=B;Bad[24]=0x80;Bad.insert(Bad.begin()+25,9,0x80);Check(!DecodeMessage(Bad,R),"overflow varint rejected");
    Check(Compact*100<=Original*80,"representative small sync message envelope saves at least twenty percent");
    PeerChannel Sender(ID(9001),{},{ID(100),ID(1),ID(2)}),Receiver(ID(9001),{},{ID(100),ID(2),ID(1)});
    PeerChannel ForeignConnection(ID(9002),{},{ID(101),ID(2),ID(1)});
    M.Header.Kind=MessageKind::TransactionResult;M.Header.Match=ID(100);M.Header.Sender=ID(1);M.Header.Event=ID(9010);M.Header.Sequence=1;M.Body={1,2,3};
    Check(Sender.Queue(M,0),"bound connection accepts peer envelope");unsigned Delivered=0;
    Sender.Pump(0,[&](auto Packet)
    {
        const auto Ack=[](auto){return true;};const auto Apply=[&](const Message& V)
        {Bytes A,C;Check(EncodeMessage(M,A)&&EncodeMessage(V,C)&&A==C,"bound channel reconstructs original identity and content");++Delivered;};
        Check(!ForeignConnection.Receive(Packet,0,Ack,Apply),"wrong connection cannot supply identity context");
        Check(Receiver.Receive(Packet,0,Ack,Apply),"authenticated bound channel receives compact envelope");return true;
    });
    Check(Delivered==1,"bound reliable peer message delivered once");
    std::cout<<"Compact envelope fixture: "<<Original<<" -> "<<Compact<<" bytes (fixture only, live traffic measured separately)\n";
}
void AsyncSharedClockAndLazyDirectory()
{
    {
        Fixture F;AsyncClientWorld A(ID(100),ID(1),1,[](const ObjectRecord&){return std::make_unique<Adapter>();},
            [](Id,Message,Bytes,bool){return true;},{},false,{},0,{false,{}});
        auto Until=[&](auto P)
        {
            const auto End=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while (std::chrono::steady_clock::now()<End) {A.PumpApplications();if (P()) return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            Check(false,"async shared context setup/application timed out");
        };
        std::string Error;Check(A.Configure(F.Data,Error),"clock test configure");A.SetMembers(F.Members);
        for (const auto& [K,O]:F.Client(1).Objects()) Check(A.Install(O,true),"clock test install");
        A.SetMembershipPaused(false);Until([&]{return A.HasSimulation(F.Key(0))&&!A.IsBootstrapMaterializationDeferred();});
        struct System final:ISharedStateAdapter
        {
            AsyncClientWorld& World;Tick Last=0;unsigned Calls=0;explicit System(AsyncClientWorld& W):World(W){}
            std::uint32_t Version() const override {return 1;}
            void Advance(Tick At,double) override {Check(World.Now()==At&&World.ContextNow()==At,"shared publication uses its actual applied logical step");Last=At;++Calls;}
            bool Capture(const std::vector<ObjectKey>&,Bytes&) const override {return true;}
            bool Validate(const std::vector<PrivateSystemState>&,const std::vector<ObjectKey>&) const override {return true;}
            bool Restore(const std::vector<PrivateSystemState>&,const std::vector<ObjectKey>&,RestoreStage) override {return true;}
        };
        auto S=std::make_unique<System>(A);auto* View=S.get();Check(A.RegisterSystem("clock",std::move(S)),"shared clock registration");
        A.Advance(4./60.);Until([&]{return View->Calls>=4;});Check(A.Now()>=View->Last,"publication outside callback cannot precede applied geometry");
        const auto Expected=[&]{Writer W(MaxPrivateStateBytes);for (const auto& [K,O]:A.Objects()) WriteAuthority(W,O.Grant);return Sha256(W.Take());};
        Check(A.DirectoryHash()==Expected(),"lazy review digest equals canonical applied directory");const auto Before=A.DirectoryHash();
        auto Changed=A.Objects().at(F.Key(0));++Changed.Grant.ControlEpoch;
        Check(A.Install(Changed,true),"update grant after cached digest");Until([&]{return A.Objects().at(F.Key(0)).Grant.ControlEpoch==Changed.Grant.ControlEpoch;});
        Check(A.DirectoryHash()==Expected()&&A.DirectoryHash()!=Before,"lazy hash invalidates on authority changes");
        const auto Stats=A.Statistics();Check(Stats.InputKinds[unsigned(AsyncInputKind::Simulation)].Accepted>0,"bounded per-kind admission diagnostics count simulation work");
        // The normal path must retain stale-life protection without repeatedly
        // putting the full combat history into every main-thread readback.
        for (unsigned Batch=0;Batch<32;++Batch)
        {
            for (unsigned I=0;I<32;++I)
            {
                Authority Dead;Dead.Object={ObjectKind::Flight,ID(20000+Batch*32+I),3};
                Dead.State=Life::Destroyed;Dead.OwnerEpoch=2;
                Check(A.InstallGrant(Dead),"retain retired flight without a live adapter");
            }
            Until([&]{return A.NumericState().LastRetiredLife(ObjectKind::Flight,ID(20000+Batch*32+31))==3;});
        }
        const auto Calls=View->Calls;A.Advance(4./60.);Until([&]{return View->Calls>=Calls+4;});
        for (unsigned I=0;I<1024;++I)
            Check(A.NumericState().LastRetiredLife(ObjectKind::Flight,ID(20000+I))==3,"unchanged publication preserves every retired lifetime");
        Check(A.Statistics().OutputPeak<1024*1024,"ordinary readback remains bounded with 1024 retired flights in the unique synchronous path");
    }
}
void IndexedCrossObjectValidation()
{
    Fixture F;auto& A=F.Client(1);const auto Parent=F.Key(0);
    auto Child=A.Objects().at(Parent);Child.Grant.Object.StableId=ID(8450);Child.Ordinal=8450;
    Child.Parent=Parent;Child.Grant.Policy=OwnershipPolicy::ParentWork;
    for (auto& [P,C]:F.Clients) Check(C->Install(Child,true),"install live dependent work before handoff");
    ObjectHandoff T;T.IdValue=8451;T.Object=Parent;T.Source=ID(1);T.Target=ID(2);T.Group=ID(8452);
    T.PreviousOwnerVersion=1;T.NextOwnerVersion=2;T.TargetPrepared=true;
    for (auto& [P,C]:F.Clients) Check(C->PrepareHandoff(T),"prepare indexed parent handoff");
    Check(A.FreezeHandoff(T),"parent waits for its live child");
    Check(std::none_of(F.Queue.begin(),F.Queue.end(),[](const auto& D){return D.Value.Header.Kind==MessageKind::HandoffState;}),"unprepared live child prevents private cut");
    Child.Grant.State=Life::Dead;
    Check(A.Install(Child,true),"terminal grant removes dependent from live index");
    Check(A.FreezeHandoff(T),"retired child no longer blocks parent");
    Check(std::count_if(F.Queue.begin(),F.Queue.end(),[](const auto& D){return D.Value.Header.Kind==MessageKind::HandoffState;})==1,"one parent cut after child termination");

    auto Source=F.Client(2).Objects().at(F.Key(1));Source.Grant.State=Life::Destroyed;
    auto Target=Source;Target.Grant.SourceLease=ID(8460);Target.Parent=Source.Grant.Object;
    Target.Grant.Object=Replacement::Child(Source.Grant.Object,Target.Grant.SourceLease,ObjectKind::Soldier);
    GuLiNet::Writer W;W.Key(Target.Grant.Object);Source.Capabilities[Replacement::Capability]={Replacement::Version,W.Take()};
    Target.Capabilities.clear();
    std::map<ObjectKey,ObjectRecord> Previous{{Source.Grant.Object,Source},{Target.Grant.Object,Target}};
    std::map<std::string,std::set<ObjectKey>> Members{{Replacement::Capability,{Source.Grant.Object}}};
    const std::array<ObjectRecord,1> Same{Target};
    Check(ValidateCapabilityMutation(Previous,Same) && ValidateCapabilityMutation(Previous,Same,&Members),"indexed and full validation accept complete historical replacement pair");
    Target.Grant.SourceLease=ID(8461);const std::array<ObjectRecord,1> Bad{Target};
    Check(!ValidateCapabilityMutation(Previous,Bad) && !ValidateCapabilityMutation(Previous,Bad,&Members),"indexed validation still checks unchanged retired source against changed child");
}
void WaitingReceiptDependencyRecovery()
{
    Fixture F;auto& C=F.Client(1);unsigned Completed=0;
    C.SetCommitCallback([&](const CommitResult& R){if (R.Event.Words[3]>=10000) ++Completed;});
    CommitResult R;R.Status=CommitStatus::Committed;R.StateVersion=1;
    const auto Version=C.NumericState().Version({F.Key(3),"object"});R.RequiredStates.push_back({F.Key(3),"object",Version+1});
    for (unsigned I=0;I<2048;++I) {R.Event=ID(10000+I);Check(C.ApplyCommit(R),"retain state-dependent receipt");}
    for (unsigned I=0;I<100;++I) C.PollNetwork(I*.01);
    Check(!Completed&&C.WorkCounts().at("waiting_result")==2048,"missing dependencies neither complete nor discard receipts");
    TransactionRequest Hit;Hit.Header.Object=F.Key(0);Hit.Header.Event=ID(9000);Hit.Kind=TransactionKind::Damage;Hit.Target=F.Key(3);Hit.RuleId="hit";Hit.Faction=Team::Red;
    Check(C.Submit(Hit,{}).Valid(),"formal damage supplies next target state");F.Step(20);
    Check(Completed==2048&&C.WorkCounts().at("waiting_result")==0,"blocked receipts complete once after real state delivery");
    for (unsigned I=0;I<2048;++I) {R.Event=ID(10000+I);Check(C.ApplyCommit(R),"replay receipt");}
    Check(Completed==2048,"retry preserves once-only completion");
}
void ReceiptBeforeDomainAuthority()
{
    Fixture F;auto& Receiver=F.Client(5);unsigned Completed=0;
    auto Domain=F.Client(2).Objects().at(F.Key(1));Domain.Grant.Object.StableId=ID(4910);
    Domain.Grant.SourceLease=ID(4911);Domain.Parent=F.Key(1);Domain.Ordinal=4910;
    CommitResult R;R.Event=ID(4912);R.StateDomain=Domain.Grant.Object;R.Target=Domain.Grant.Object;
    R.Status=CommitStatus::Committed;R.StateVersion=1;R.Kind=TransactionKind::Spawn;
    R.Objects.push_back(Domain);R.RequiredStates.push_back({Domain.Grant.Object,"object",1});
    Receiver.SetCommitCallback([&](const CommitResult& V)
    {
        if (V.Event!=R.Event) return;
        Check(Receiver.Objects().contains(Domain.Grant.Object)
            && Receiver.NumericState().Version({Domain.Grant.Object,"object"})>=1,
            "late receipt callback reads the authenticated applied domain state");
        ++Completed;
    });
    Envelope H;H.Kind=MessageKind::TransactionResult;H.Match=ID(100);H.Epoch=1;H.Sender=ID(2);
    H.Object=Domain.Grant.Object;H.OwnerEpoch=1;H.Event=R.Event;
    const Message Result{H,EncodeCommit(R)};
    CommitResult Decoded;Check(DecodeCommit(Result.Body,Decoded),"early receipt has a valid original wire encoding");
    Check(Receiver.ReceivePeer(ID(2),Result),"receipt before its domain grant must be retained, not dropped");
    Check(Receiver.ReceivePeer(ID(2),Result) && !Completed,"duplicate early receipt remains once-only and waits for authority");
    Check(Receiver.WorkCounts().at("waiting_result_authority")==1,"retries cannot consume additional pending receipt slots");
    auto Forged=Result;Forged.Header.Sender=ID(4);
    Check(Receiver.ReceivePeer(ID(4),Forged) && !Completed,"unverified identity cannot produce a callback before the grant");
    Receiver.PollNetwork(6.);
    Check(!Completed,"elapsed time alone cannot authorize a pending receipt");
    Check(std::any_of(F.Queue.begin(),F.Queue.end(),[&](const auto& D)
        {return D.From==ID(5) && D.To==ID(2) && D.Value.Header.Kind==MessageKind::WorkStateRequest;}),
        "pending authority receipt requests its original domain body through normal state repair");
    Check(Receiver.InstallGrant(Domain.Grant),"install original room authority after the result");
    Writer Body;Body.U32(1);Writer Record;WriteObject(Record,Domain);
    WriteStateFragment(Body,{{Domain.Grant.Object,"object"},1,1,Record.Take()});
    auto StateHeader=H;StateHeader.Kind=MessageKind::StateFragments;
    Check(Receiver.ReceivePeer(ID(2),{StateHeader,Body.Take()}),"original owner publishes the delayed domain body");
    Receiver.PollNetwork(6.1);
    Check(Completed==1,"delayed authority and state release the exact final receipt once");
    Check(Receiver.WorkCounts().at("waiting_result_authority")==0
        && Receiver.WorkCounts().at("waiting_result_authority_bytes")==0,"successful receipt releases its bounded authority queue");
    Check(Receiver.ReceivePeer(ID(2),Result) && Completed==1,"late repeated result cannot duplicate callbacks");
    Check(!Receiver.ReceivePeer(ID(4),Forged) && Completed==1,"a different owner cannot impersonate the result authority");
}
void PrivateRestoreRetainsNumericHistory()
{
    auto ManifestValue=Manifest();Rule Credit;Credit.Kind=TransactionKind::Credit;
    Credit.SourceDefinition="data/unit";Credit.Amount={1,0};ManifestValue.Rules["history-credit"]=Credit;
    Fixture F(ManifestValue);constexpr unsigned Count=6000;
    TransactionRequest R;R.Kind=TransactionKind::Credit;R.Header.Object=F.Key(0);
    R.RuleId="history-credit";R.Faction=Team::Red;
    for (unsigned I=0;I<Count;++I)
    {
        R.Header.Event=ID(60000+I);
        Check(F.Client(1).Submit(R,{}).Valid(),"admit real numeric history credit");F.Step(1);
        const auto Result=F.Client(1).NumericState().Result(R.Header.Event);
        Check(Result && Result->Status==CommitStatus::Committed,"history fixture uses committed transactions");
    }
    const ObjectKey Account=TeamAccountObject(Team::Red);
    const std::array<ObjectKey,2> Keys{F.Key(0),Account};ObjectHandoffState Cut;
    Check(F.Client(1).CaptureObjects(Keys,ID(2),0,Cut),"capture object and full committed account history");
    Check(EncodeHandoffState(Cut).size()>2*1024*1024,"history fixture exceeds a small engine application batch");
    const StateKey LastEvent{Account,"event/"+ID(60000+Count-1).ToString()};
    const auto Expected=std::find_if(Cut.Numeric.begin(),Cut.Numeric.end(),[&](const auto& V){return V.Key==LastEvent;})->Value;
    {
        auto Observed=F.Client(2).NumericState();OwnerState::ImportStats Stats;
        Check(Observed.Import(ID(1),Cut.Numeric,true,nullptr,nullptr,&Stats),"observer imports the complete original handoff");
        Check(Stats.ReusedHistory==Count && Stats.ReusedHistoryBytes>2*1024*1024,
            "already published exact history is reused without reparsing or reindexing");
        const auto Encode=[](const auto& Values){Writer W;for (const auto& V:Values) WriteStateFragment(W,V);return W.Take();};
        const auto Before=Encode(Observed.Export(Keys));
        Check(!Observed.Import(ID(3),Cut.Numeric,true),"equal history cannot bypass current sender authority");
        auto Corrupted=Cut.Numeric;
        const auto Last=std::find_if(Corrupted.begin(),Corrupted.end(),[&](const auto& V){return V.Key==LastEvent;});
        Last->Value.pop_back();
        Check(!Observed.Import(ID(1),Corrupted,true),"equal versions cannot bypass complete history payload validation");
        Corrupted=Cut.Numeric;
        std::find_if(Corrupted.begin(),Corrupted.end(),[&](const auto& V){return V.Key==LastEvent;})->Schema=2;
        Check(!Observed.Import(ID(1),Corrupted,true),"equal history cannot bypass schema validation");
        Corrupted=Cut.Numeric;Corrupted.push_back(Corrupted.back());
        Check(!Observed.Import(ID(1),Corrupted,true),"equal history cannot bypass duplicate key rejection");
        Check(Encode(Observed.Export(Keys))==Before,"failed history imports preserve every prior value and decision");
    }
    Adapter* View=nullptr;
    AsyncClientWorld A(ID(100),ID(2),1,[&](const ObjectRecord& O)
        {auto V=std::make_unique<Adapter>();if (O.Grant.Object==F.Key(0)) View=V.get();return V;},
        [](Id,const Message&,const Bytes&,bool){return true;});
    const auto Until=[&](auto Predicate,const char* Why)
    {
        const auto End=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        while (std::chrono::steady_clock::now()<End)
        {A.PumpApplications();if (Predicate()) return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        Check(false,Why);
    };
    std::string Error;Check(A.Configure(F.Data,Error),"history restore configure");A.SetMembers(F.Members);
    for (const auto& [K,O]:F.Client(2).Objects()) Check(A.Install(O,true),"history restore authority");
    A.SetMembershipPaused(false);Until([&]{return !A.IsBootstrapMaterializationDeferred()&&View;},"history restore adapters ready");
    const std::array<ObjectHandoffState,1> Parts{Cut};
    Until([&]{return A.StageBootstrapDirectory(Parts);},"history restore directory prepared");
    Until([&]{return A.ImportBootstrap(Parts,[&](Id Source,const OwnerState::FragmentVisitor& Visit)
        {if (Source!=Cut.Source) return false;for (const auto& V:Cut.Numeric) if (!Visit(V)) return false;return true;});},
        "all four private stages restore while numeric history remains owner-side");
    Check(View->Position==F.Adapters.at({ID(1),F.Key(0)})->Position,"history optimization preserves private actor state");
    std::string Evidence;
    Until([&]{return A.Inspect("restored-history",[&](ClientWorld& C)
    {
        Check(C.NumericState().TeamEconomy().Balance(Team::Red).Blue==100+Count,"full balance retained during private restore");
        for (unsigned I=0;I<Count;++I) Check(C.NumericState().Result(ID(60000+I)).has_value(),"every original final decision remains available");
        const auto Exported=C.NumericState().Export(Keys);
        const auto Last=std::find_if(Exported.begin(),Exported.end(),[&](const auto& V){return V.Key==LastEvent;});
        Check(Last!=Exported.end()&&Last->Value==Expected,
            "restored original decision and request fingerprint keep exact bytes");
        return std::string("complete");
    },Evidence);},"inspect complete restored numeric history");
    Check(Evidence=="complete"&&A.Error().empty(),"history and private restore both completed");
    Check(A.Statistics().OutputPeak<1024*1024,"large numeric history cannot consume the engine restoration output budget");
}
void BoundedTransactionRetryProgress()
{
    auto M=Manifest();M.Rules.at("hit").Damage=1;
    Fixture F(M);auto& Source=F.Client(1);auto& Target=F.Client(4);
    Target.SetPeerTransactionDeferral(true);
    unsigned Completed=0;bool AdmitResults=false;std::vector<Id> Attempts;
    Target.SetApplicationResultBudget([&](const OwnerWork& W){Attempts.push_back(W.Event);return AdmitResults;},[](Id){});
    const auto Submit=[&](unsigned Event)
    {
        TransactionRequest R;R.Header.Object=F.Key(0);R.Header.Event=ID(Event);R.Kind=TransactionKind::Damage;
        R.Target=F.Key(3);R.RuleId="hit";R.Faction=Team::Red;
        Check(Source.Submit(R,[&](const CommitResult& C){Check(C.Status==CommitStatus::Committed,"retained retry commits");++Completed;}).Valid(),"submit bounded retry");
    };
    for (unsigned N=0;N<9;++N) Submit(50010+N);
    std::set<Id> Sent;
    for (unsigned Pass=0;Pass<5;++Pass)
    {
        const auto Before=F.Queue.size();Source.PollNetwork(1.,2);
        unsigned Transactions=0;
        for (std::size_t I=Before;I<F.Queue.size();++I) if (F.Queue[I].Value.Header.Kind==MessageKind::Transaction)
        {++Transactions;Check(Sent.insert(F.Queue[I].Value.Header.Event).second,"one attempt per due pass");}
        Check(Transactions<=2,"pending retry batch is bounded");
        if (Pass==0)
        {
            Submit(50001); // New smaller keys must not starve the remainder of the due set.
            FrozenCommand C;C.Event=ID(50050);C.Operation="move";C.Members.push_back({F.Key(0),1,1});
            Check(Source.Route(C),"command proceeds between retry batches");
        }
        if (Pass==1) Check(F.Adapters.at({ID(1),F.Key(0)})->Executions==1&&Source.HasPendingNetworkRetryBatch(),
            "command executes on the next owner turn while transaction retries remain");
        const auto BeforeTick=Source.Now();Source.Advance(1./60);
        Check(Source.Now()>BeforeTick,"simulation proceeds between retry batches");
    }
    Check(Sent.size()==10&&!Source.HasPendingNetworkRetryBatch(),"round-robin scheduling reaches every due identity");
    F.Flush();Check(Target.WorkCounts().at("retry")==10,"backpressure retains every transaction");
    Check(Attempts.empty(),"peer receive queues transaction work without planning it inline");
    Attempts.clear();
    for (unsigned Pass=0;Pass<5;++Pass)
    {
        const auto Before=Attempts.size();Target.PollNetwork(1.,2);
        Check(Attempts.size()-Before<=2,"failed work retry batch is bounded");
    }
    Check(Attempts.size()==10&&std::set<Id>(Attempts.begin(),Attempts.end()).size()==10,
        "a failed first retry cannot monopolize the continuation queue");
    Check(!Target.HasPendingNetworkRetryBatch()&&Target.WorkCounts().at("retry")==10,
        "a completed blocked round sleeps until its retry deadline without losing work");
    Target.PollNetwork(1.,2);Check(Attempts.size()==10,"blocked retries do not busy-spin at the same deadline");
    AdmitResults=true;
    for (unsigned Pass=0;Pass<100&&Completed<10;++Pass)
    {
        const double Now=1.2+Pass*.01;
        for (auto& [P,C]:F.Clients) C->PollNetwork(Now,2);
        F.Flush();
    }
    Check(Completed==10&&Source.PendingTransactions().empty(),"every retained request completes exactly once after recovery");
    for (const auto& [P,C]:F.Clients) Check(C->Objects().at(F.Key(3)).Vitals.Value==90,"retry slicing preserves final gameplay value");
}
void FragmentCacheAndMiningIndexes()
{
    MiningReservations R;Check(R.Configure({{1,1},{2,1},{3,9}}),"configure indexed mining history");
    const ObjectKey A{ObjectKind::Soldier,ID(20),1},B{ObjectKind::Soldier,ID(21),1};
    const auto Verify=[&]
    {
        for (const auto Cluster:{0u,1u,9u})
        {
            std::set<ObjectKey> Leases;std::set<Id> Decisions;
            for (const auto& [K,V]:R.Snapshot().Leases) if (V.Cluster==Cluster) Leases.insert(K);
            for (const auto& [K,V]:R.Snapshot().Decisions) if (V.Outcome.Lease.Cluster==Cluster) Decisions.insert(K);
            Check(Leases==R.LeasesForCluster(Cluster)&&Decisions==R.DecisionsForCluster(Cluster),"cluster indexes preserve full historical selection");
        }
        MiningReservations Copy;Check(Copy.Restore(R.Snapshot()),"restore rebuilds derived indexes");
        Check(EncodeMiningReservations(Copy.Snapshot())==EncodeMiningReservations(R.Snapshot()),"derived indexes do not alter serialized bytes");
        for (const auto Cluster:{0u,1u,9u}) Check(Copy.LeasesForCluster(Cluster)==R.LeasesForCluster(Cluster)
            &&Copy.DecisionsForCluster(Cluster)==R.DecisionsForCluster(Cluster),"restored cluster index agrees");
    };
    auto Claim=R.ClaimPosition(A,1,0,1,ID(4900));Check(Claim.IsCommitted(),"claim first position");Verify();
    Check(R.ClaimPosition(B,1,0,1,ID(4901)).Status==MiningReservationStatus::Occupied,"retain failed claim history");Verify();
    Check(R.ClaimNode(A,Claim.Lease.Token,1,ID(4902)).IsCommitted(),"claim indexed node");Verify();
    auto Branch=R;Check(R.Release(A,Claim.Lease.Token,ID(4903)).IsCommitted(),"release indexed lease");Verify();
    Check(Branch.Snapshot().Leases.size()==1,"index changes keep trial state isolated");
    Check(R.ClaimPosition(A,9,2,2,ID(4904)).IsCommitted()&&R.AbandonSource(A),"source death removes lease without dropping decision history");Verify();
    Fixture F;auto& State=F.Client(1).NumericState();
    std::vector<ObjectKey> Keys;for (const auto& [K,O]:State.Objects()) Keys.push_back(K);
    const auto Compare=[&]
    {
        const auto First=State.Export(Keys);const auto Cached=State.Export(Keys);
        const auto Encode=[](const auto& Values){Writer W;for (const auto& V:Values) WriteStateFragment(W,V);return W.Take();};
        Check(Encode(First)==Encode(Cached),"cached fragments preserve exact wire bytes and hashes");
        const auto Account=std::find_if(Cached.begin(),Cached.end(),[](const auto& V){return V.Key.Object==TeamAccountObject(Team::Red)&&V.Key.Capability=="account";});
        Check(Account!=Cached.end(),"cached export contains the team account");
        Reader Read(Account->Value);std::uint8_t TeamValue;Resources Balance;
        Check(Read.U8(TeamValue)&&TeamValue==unsigned(Team::Red)&&ReadResources(Read,Balance)
            &&Balance==State.TeamEconomy().Balance(Team::Red),"account cache matches the current ledger after a real commit");
    };
    Compare();
    TransactionRequest Reserve;Reserve.Header.Object=F.Key(0);Reserve.Header.Event=ID(4905);Reserve.Kind=TransactionKind::Reserve;
    Reserve.RuleId="reserve";Reserve.Faction=Team::Red;Reserve.ReservationKey={DeriveEconomyAccount(F.Key(0)),1};
    Check(F.Client(1).Submit(Reserve,{}).Valid(),"commit changes to an encoded account");F.Step();Compare();
}
}
int main(int argc,char** argv)
{
    try
    {
        for (int I=1;I<argc;++I)
        {
            Check(std::string_view(argv[I])=="--reuse-resource-validation-snapshot","unknown acceptance option");
            ReuseResourceValidationSnapshot=true;
        }
        std::cout<<"Fixture resource validation snapshot reuse="<<ReuseResourceValidationSnapshot<<'\n';
        CurrentResourceSnapshotContracts();
        ResourceEpochAndRecoveryContracts();
        ResourceInterruptionCutpoints();ResourceSnapshotQueueBounds();
        AbruptResourceRecoveryContracts(false);AbruptResourceRecoveryContracts(true);
        CurrentStateHistoryIndependence();
        CurrentMiningLeaseContracts();
        ReceiptBeforeDomainAuthority();
        CompactPeerEnvelopeRoundTrip();BoundedReliableApplicationAdmission();AsyncSharedClockAndLazyDirectory();IndexedCrossObjectValidation();WaitingReceiptDependencyRecovery();PrivateRestoreRetainsNumericHistory();
        FrozenFlightAdmissionAndRequestTime();
        IndependentProgressAndValues();ScopedHandoff();CommittedPublicationBackpressure();PendingWorkStaysAtFrozenBoundary();HandoffBackpressureKeepsSourceRunning();BoundedHandoffLaneAdmission();CommanderActivityPolicy();OrderedReliableTransport();LatestStateDuringFragmentedSend();RoomMetadata();IndivisibleGroupsAndFixedRoles();FinalEventAfterMetadata();DelayedAcknowledgementFairness();ScopedFragmentIsolation();AsyncCurrentMiningReadback();PlannedStateIsolation();ResourceCopyIsolation();ControlPointSupplyProjection();LateBirthAcrossHandoff();
        TargetOwnerSettlesDamageIndependently();ReliableCapacityWithBatchedAcknowledgements();SlowObserverCannotBlockOwnerEconomy();
        AsyncExternalSendBindsPackingIdentity(false);AsyncExternalSendBindsPackingIdentity(true);AsyncIndependentOutboundProgress();AsyncMainAdvanceEligibility();AsyncMainAdvanceEligibility(0);SharedMembershipImport();AsyncContinuationAndGameplayDelivery();
        AsyncCaptureChainClockProgress(false);AsyncCaptureChainClockProgress(true);
        for (const auto Workers:{1u,2u,4u}) AsyncContinuationAndGameplayDelivery(Workers);
        BoundedTransactionRetryProgress();FragmentCacheAndMiningIndexes();
        std::cout<<"Client ownership contracts passed; protocol="<<ProtocolVersion<<" manifest="<<ManifestVersion<<" handoff="<<HandoffVersion<<'\n';return 0;
    }
    catch (const std::exception& E) {std::cerr<<E.what()<<'\n';return 1;}
}
