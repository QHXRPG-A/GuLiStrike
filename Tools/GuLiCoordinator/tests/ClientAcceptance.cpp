#include "GuLiNetClient.h"
#include "GuLiNetRoom.h"
#include "GuLiNetManifest.h"
#include "GuLiNetTransport.h"
#include "GuLiNetHash.h"
#include "GuLiNetCapability.h"
#include "GuLiNetControlPoint.h"
#include "GuLiNetResourceMaintenance.h"
#include "GuLiNetLinearFlightRecipe.h"
#include "GuLiNetRuntimeEffectRecipe.h"
#include <algorithm>
#include <array>
#include <deque>
#include <iostream>
#include <stdexcept>

using namespace GuLiNet;
namespace
{
Id ID(unsigned N) {return {{0x47554c49,0,0,N}};}
void Check(bool Value,const char* Why) {if (!Value) throw std::runtime_error(Why);}
struct Adapter final:IObjectAdapter
{
    bool Local=false;std::uint64_t Position=0,Executions=0,LastTick=0;
    Revision AppliedVersion=0;float AppliedHealth=0;
    std::uint32_t Version() const override {return 1;}
    bool Prepare(const ObjectRecord&) override {return true;}
    void SetAuthority(const Authority&,bool Simulate) override {Local=Simulate;}
    void Advance(Tick At,double) override {if (Local) {++Position;LastTick=At;}}
    bool Command(const FrozenCommand&,const CommandMember&) override {++Executions;return true;}
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
    bool Capture(Bytes& Out) const override {Writer W;W.U64(Position);W.U64(Executions);W.U64(LastTick);Out=W.Take();return true;}
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
    std::deque<Delivery> Queue;std::vector<Delivery> Metadata;double Now=0;bool RejectHandoffQueue=false;
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
                if (RejectPublications.contains(To) && (V.Header.Kind==MessageKind::StateFragments || V.Header.Kind==MessageKind::ObjectEvent)) return false;
                if (To.Valid()) Queue.push_back({To,P,std::move(V)});else Metadata.push_back({{},P,std::move(V)});return true;});
            Check(World->Configure(Data,Error),Error.c_str());World->SetMembers(Members);
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
        for (auto V:Batch) {V.TargetPrepared=true;Check(G.Client(2).FreezeHandoff(V),"freeze group at admitted boundary");}
        Check(std::count_if(G.Queue.begin(),G.Queue.end(),[](const auto& D){return D.Value.Header.Kind==MessageKind::HandoffState;})==1,"one shared cut serves all member identities");
        G.Flush();Check(TargetCount->Detaches==Round*2,"early cut waits for every member prepare");
        for (std::size_t I=1;I<Batch.size();++I) Check(G.Client(1).PrepareHandoff(Batch[I]),"non-leader prepare releases the early cut");
        G.Flush();Check(TargetCount->Detaches==Round*2+1,"target restores the shared cut once");
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
    Check(R.Objects().at(Keys[2]).Grant.Controller==ID(4),"host does not gain enemy command");
    auto Wrong=R.Members().at(ID(2));Wrong.Seat=Role::Air;
    Check(!R.Join(Wrong,"config","map",.1,Error) && Error=="Role is locked for this match","online role mutation refused");
    Check(R.BeginLeave(ID(1),false,1),"graceful group host exit");
    auto Pending=R.Ownership().Transfers();Check(Pending.size()==Keys.size(),"group moves as one batch");
    unsigned Installed=0;
    for (const auto& [K,T]:Pending)
    {
        Check(T.Target==ID(4),"remaining Commander preferred across factions");
        Check(R.Ownership().TargetPrepared(T.Target,K,T.IdValue) && R.Ownership().SourceFrozen(T.Source,K,T.IdValue,100+Installed),"group state captured");
        Check(R.Ownership().TargetInstalled(T.Target,K,T.IdValue,100+Installed),"group state installed");++Installed;
        if (Installed<Keys.size()) Check(R.Ownership().Transfers().size()==Keys.size(),"no member commits before complete group");
    }
    auto Actions=R.TakeOwnershipActions(1);
    Check(std::count_if(Actions.begin(),Actions.end(),[](const auto& A){return A.Kind==HandoffActionKind::Commit;})==Keys.size(),"all group grants commit together");
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
}
int main()
{
    try
    {
        FrozenFlightAdmissionAndRequestTime();
        IndependentProgressAndValues();ScopedHandoff();CommittedPublicationBackpressure();PendingWorkStaysAtFrozenBoundary();HandoffBackpressureKeepsSourceRunning();BoundedHandoffLaneAdmission();CommanderActivityPolicy();OrderedReliableTransport();LatestStateDuringFragmentedSend();RoomMetadata();IndivisibleGroupsAndFixedRoles();FinalEventAfterMetadata();DelayedAcknowledgementFairness();ScopedFragmentIsolation();PlannedStateIsolation();ResourceCopyIsolation();ControlPointSupplyProjection();LateBirthAcrossHandoff();
        TargetOwnerSettlesDamageIndependently();ReliableCapacityWithBatchedAcknowledgements();SlowObserverCannotBlockOwnerEconomy();
        std::cout<<"Client ownership contracts passed; protocol="<<ProtocolVersion<<" manifest="<<ManifestVersion<<" handoff="<<HandoffVersion<<'\n';return 0;
    }
    catch (const std::exception& E) {std::cerr<<E.what()<<'\n';return 1;}
}
