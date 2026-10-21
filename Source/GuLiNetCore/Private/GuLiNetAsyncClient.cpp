#include "GuLiNetAsyncClient.h"
#include "GuLiNetManifest.h"
#include "GuLiNetHash.h"
#include "GuLiNetTrace.h"
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include <cmath>
#include <algorithm>
#include <list>
#include <unordered_map>

namespace GuLiNet
{
namespace
{
using Steady=std::chrono::steady_clock;
double Seconds() {return std::chrono::duration<double>(Steady::now().time_since_epoch()).count();}
double Milliseconds(Steady::time_point Start) {return std::chrono::duration<double,std::milli>(Steady::now()-Start).count();}
constexpr std::size_t QueueLimit=64ull*1024*1024;
std::atomic<std::uint64_t> NextWorldGeneration=0;
std::string PackCacheFingerprint(std::span<const std::uint8_t> Body)
{
    // This is only a bounded local cache index, never a wire/state identity.
    // A hit still compares every original byte before reusing encoded data.
    std::uint64_t Value=14695981039346656037ull;
    for (const auto Byte:Body) {Value^=Byte;Value*=1099511628211ull;}
    constexpr char Hex[]="0123456789abcdef";
    std::string Result(16,'0');
    for (unsigned I=0;I<16;++I) {Result[15-I]=Hex[Value&15];Value>>=4;}
    return Result;
}
std::size_t Cost(const ObjectRecord& O)
{
    std::size_t N=sizeof(O)+O.Definition.size()+O.FrozenWork.size();
    for (const auto& [K,V]:O.Capabilities) N+=K.size()+V.Data.size()+128;
    for (const auto& [K,V]:O.Upgrades) {N+=K.size()+128;for (const auto& S:V.Chosen) N+=S.size()+64;}
    return N;
}
std::size_t Cost(const TransactionRequest& R) {return sizeof(R)+R.RuleId.size()+R.CapabilityPayload.size()+R.Admission.size()+1024;}
std::size_t Cost(const ObjectHandoffState& S)
{
    std::size_t N=1024;for (const auto& O:S.Objects) N+=Cost(O.Record)+O.State.size();
    for (const auto& P:S.Systems) N+=P.State.size()+P.Objects.size()*sizeof(ObjectKey)+256;
    for (const auto& F:S.Numeric) N+=F.Value.size()+256;
    for (const auto& R:S.Pending) N+=Cost(R);
    for (const auto& M:S.IncomingCommands) N+=M.Body.size()+256;
    return N+S.ExecutedCommands.size()*128;
}
}
Revision ClientNumericView::Version(const StateKey& K) const {const auto I=Versions.find(K);return I==Versions.end()?0:I->second;}
std::uint32_t ClientNumericView::LastRetiredLife(ObjectKind K,Id ID) const {const auto I=Retired.find({K,ID});return I==Retired.end()?0:I->second;}
Id ClientNumericView::Owner(ObjectKey K) const
{
    if (!Directory) return {};const auto I=Directory->find(K);if (I==Directory->end()) return {};
    return I->second.Grant.Dormant?I->second.Grant.StateCustodian:I->second.Grant.SimOwner;
}
ObjectKey ClientNumericView::ExecutionDomain(const TransactionRequest& R) const
{
    if (R.Kind==TransactionKind::Damage) return R.Target;
    if (R.Kind==TransactionKind::Credit||R.Kind==TransactionKind::Reserve||R.Kind==TransactionKind::Commit||R.Kind==TransactionKind::Refund) return TeamAccountObject(R.Faction);
    if (R.Kind==TransactionKind::AdmitWork) {Message M;TransactionRequest Original;if (DecodeMessage(R.Admission,M)&&DecodeTransaction(M.Header,M.Body,Original)) return ExecutionDomain(Original);}
    return R.Header.Object;
}

struct AsyncClientWorld::State
{
    struct ObjectProxy;
    struct SystemProxy;
    struct ObjectInfo {std::uint64_t Token=0;std::uint32_t Version=0;std::vector<std::string> Systems;bool Prepared=false;std::uint32_t Period=1;std::shared_ptr<IObjectMotionCodec> Codec;};
    struct MainObject
    {
        ObjectInfo Info;std::unique_ptr<IObjectAdapter> Adapter;
        ObjectKey Key;ObjectRecord* Record=nullptr;
    };
    struct Validation {bool Prepared=false,Valid=false;Bytes BytesValue;std::vector<ObjectKey> Dependencies;};
    struct SystemValidation {PrivateSystemState Part;bool Valid=false;};
    struct CapturedObject {bool Good=false;Bytes Value;};
    struct CapturedSystem {bool Good=false;std::vector<ObjectKey> Keys;Bytes Value;std::set<ObjectKey> Uses;};
    struct CaptureCache {std::map<ObjectKey,CapturedObject> Objects;std::map<std::string,CapturedSystem> Systems;};
    struct RestoreCache {std::map<ObjectKey,bool> Objects;std::map<std::string,bool> Systems;RestoreStage Stage=RestoreStage::Detach;};
    struct PreflightCache {std::map<ObjectKey,std::pair<ObjectInfo,Validation>> Objects;std::map<std::string,SystemValidation> Systems;};
    template<class T> struct Reply {bool Ready=false;T Value{};};
    struct Input {std::function<void(ClientWorld&)> Run;std::size_t Bytes=0;double Enqueued=0;std::uint64_t Sequence=0;bool Retained=false;AsyncInputKind Kind=AsyncInputKind::General;};
    struct Application
    {
        std::function<void()> Run;std::size_t Bytes=0;std::uint64_t Sequence=0;double Enqueued=0;bool RetainedOutbound=false;
        Id Room;Revision Epoch=0;std::uint64_t Generation=0;
    };
    template<class T> struct Operation {std::shared_ptr<Reply<T>> Result;bool Consumed=false;};
    struct CaptureResult {bool Good=false;ObjectHandoffState Value;Bytes Encoding,Staged;};
    struct CaptureRequest {std::shared_ptr<Reply<CaptureResult>> Result;std::vector<ObjectKey> Keys;Id Target;Revision Transfer=0;};
    struct CaptureBarrier
    {
        std::vector<ObjectKey> Keys;
        std::map<ObjectKey,std::pair<std::uint64_t,Tick>> Revisions;
        CaptureCache Value;
        bool Ready=false,TooLarge=false,CurrentOnly=false;double ReadyAt=0;
    };
    struct ReadDelta
    {
        struct IndexChange
        {
            std::vector<std::pair<Team,std::string>> Added,Removed;
            std::string AddedDefinition,RemovedDefinition;
        };
        std::map<ObjectKey,ObjectRecord> Records;
        std::map<ObjectKey,IndexChange> IndexChanges;
        ClientQueryDelta Query;
        std::map<StateKey,Revision> Versions;
        struct ResourceDelta
        {
            bool Seed=false,Configured=false;Revision Version=0;
            std::map<std::uint32_t,OreDefinition> Nodes;
            std::map<std::uint32_t,std::uint8_t> Remaining;
            std::vector<Team> Owners;bool OwnersChanged=false;
            std::map<ObjectKey,MinerInventory> Miners;
            std::map<ObjectKey,FactoryInventory> Factories;
            std::set<ObjectKey> RemovedMiners,RemovedFactories;
            GuLiNet::Resources Processed;
        } Resources;
        std::shared_ptr<const MiningReservationDirectory> Mining;
        GuLiNet::Resources Red,Blue;
        std::optional<std::map<EconomyKey,Reservation>> Reservations;
        Revision ResourceQuery=0,MiningQuery=0;
        std::map<std::pair<ObjectKind,Id>,std::uint32_t> Retired;
        std::set<ObjectKey> Busy,Frozen;
        Tick Clock=0;bool Paused=true,Deferred=true,OwnershipApplying=false;std::size_t Preparing=0;
        std::string Failure;
    };

    // Main-thread registry and published replica. No worker dereferences its adapters or callbacks.
    const std::thread::id MainThread=std::this_thread::get_id();
    Id Match,Player;Revision MatchEpoch=0;
    const std::uint64_t Generation=++NextWorldGeneration;
    const AsyncExecutionOptions Options;
    Factory Create;Send Transmit;Pack Compress;bool Detailed=false;
    std::function<void()> InitializeWorker;
    std::map<ObjectKey,MainObject> MainObjects;
    // Non-owning, caller-thread index. The ordered registry remains authoritative;
    // a new adapter lifetime receives a new token and release removes the lease.
    std::unordered_map<std::uint64_t,MainObject*> MainObjectTokens;
    std::map<std::string,std::unique_ptr<ISharedStateAdapter>> MainSystems;
    std::map<std::uint64_t,TransactionCallback> Callbacks;
    struct GameplayApplication {std::string Group;std::function<void()> Apply;};
    std::map<std::uint64_t,GameplayApplication> GameplayApplications;
    std::map<std::string,std::uint64_t> GameplayGroups;
    std::set<std::uint64_t> PendingGameplay;
    struct GameplayCompute
    {
        std::shared_ptr<IGameplayCalculation> Work;
        std::uint64_t Token=0;
        Id Reservation;
        std::size_t InputSize=0,ResultSize=0;
        double Submitted=0,QueueMilliseconds=0,ComputeMilliseconds=0;
        bool Done=false;
    };
    // These queues and Done are owned only by the ordered transaction thread.
    std::deque<std::shared_ptr<GameplayCompute>> WaitingComputes,ComputeOrder;
    std::size_t GameplayInputBytes=0,ActiveCalculations=0; // guarded by Mutex
    std::atomic<std::uint64_t> NextToken=0;
    std::uint64_t NextCallback=0;
    std::function<void(const ObjectRecord&)> RecordObserver;
    std::function<void(const ClientQueryDelta&)> QueryObserver;
    CommitCallback Committed;
    std::function<void(const CommandReceipt&)> CommandCompleted;
    std::function<void(const std::vector<std::pair<ObjectHandoff,Authority>>&,bool)> OwnershipCommitted;
    std::function<void(const char*,double)> TimingObserver;
    std::function<bool(Id)> HandoffAdmission;
    std::deque<std::tuple<Id,Message,Bytes,bool>> Outbound;
    std::size_t OutboundBytes=0;
    std::map<Id,Member> MainMembers;
    std::map<Id,ResourceGroupState> MainResourceGroups;
    std::map<Id,TransactionRequest> MainRequests;
    std::map<Id,std::uint64_t> MainCallbackTokens;
    std::map<Id,std::pair<Revision,std::set<Id>>> InterruptedNotices;
    bool FirstInterruption(const CommitResult& R)
    {
        auto& Notices=InterruptedNotices[R.ResourceGroup];
        if (R.ResourceEpoch<Notices.first) return false;
        if (R.ResourceEpoch>Notices.first) {Notices.first=R.ResourceEpoch;Notices.second.clear();}
        return Notices.second.insert(R.Event).second;
    }
    bool StampRequest(TransactionRequest& R) const;
    bool CurrentResult(const CommitResult& R) const
    {
        if (!R.ResourceGroup.Valid()) return !R.ResourceEpoch;
        const auto G=MainResourceGroups.find(R.ResourceGroup);
        return G!=MainResourceGroups.end()&&G->second.RequestEpoch==R.ResourceEpoch
            &&(!G->second.Recovery||R.Status==CommitStatus::Interrupted);
    }
    std::map<ObjectKey,ObjectRecord> Directory;
    std::map<std::pair<Team,std::string>,std::set<ObjectKey>> CapabilityIndex;
    std::map<std::string,std::size_t> Definitions;
    ClientNumericView Numeric;
    std::array<Revision,2> PublishedAccountVersions{UINT64_MAX,UINT64_MAX};
    std::shared_ptr<ResourceLedgerState> ResourceReplica=std::make_shared<ResourceLedgerState>();
    Tick PublishedClock=0,AppliedContextTick=0,AppliedWorldTick=0;
    std::optional<Tick> SharedContextTick;
    ObjectKey AppliedContext;
    bool Paused=true,Deferred=true,OperationPending=false;
    bool OwnershipApplying=false;
    std::size_t Preparing=0;
    std::string Failure,Hash;
    bool HashDirty=true;
    std::set<ObjectKey> Frozen;
    std::map<ObjectKey,ObjectHandoff> Handoffs;
    std::map<ObjectKey,MotionReceiveStats> Motion;
    std::map<std::string,std::uint64_t> HandoffIssues,HandoffSizes;
    std::map<Id,ResourceSnapshotStatus> ResourceStatuses;
    std::vector<ReceiveRejection> FirstRejections,RecentRejections;
    std::vector<TransactionRequest> Pending;
    std::vector<WorkDiagnostic> Diagnostics;
    std::map<std::string,std::size_t> Counts;
    std::map<ObjectKind,std::size_t> CommandCounts;
    std::map<Id,CommandReceipt> Receipts;
    std::deque<CommitResult> Unsettled;
    std::deque<PreparedCommit> UnsettledPrepared;
    std::uint64_t DuplicateCount=0,UnknownLives=0,ImportRejected=0,UnsettledCount=0;
    std::string ImportFailure;
    std::map<std::string,Operation<bool>> BooleanOperations;
    std::map<std::string,Operation<std::string>> Inspections;
    mutable std::map<std::string,Operation<CaptureResult>> Captures;
    std::map<std::string,CaptureRequest> CaptureRequests;
    std::map<std::vector<ObjectKey>,std::shared_ptr<CaptureBarrier>> CaptureBarriers;
    double DeferredAdvanceSeconds=0;
    bool CaptureAdvancePending=false;
    std::map<ObjectKey,std::uint64_t> ObjectRevisions;
    mutable Bytes LastCaptureEncoding,LastCaptureStaged;

    // Transported closures capture plain value payloads and registry identities only.
    std::mutex Mutex;
    std::condition_variable Wake;
    std::deque<Input> Inputs,Controls;
    std::deque<Application> Applications;
    std::atomic<bool> Stopping=false;
    std::thread Worker;
    std::uint64_t OutputSequence=0,AppliedSequence=0;
    std::uint64_t CommandWakeSequence=0; // protected by Mutex
    bool CommandWakeQueued=false; // protected by Mutex
    bool Pumping=false,CommandWakeDeferred=false; // caller thread only
    bool WakeNextCommandSimulation=false; // ordered worker only
    std::uint64_t InputSequence=0;
    std::size_t InputBytes=0,OutputBytes=0,ControlBytes=0;
    std::size_t ResultReservedBytes=0;
    std::map<Id,std::pair<Id,std::size_t>> ResultReservations;
    AsyncClientStats Stats;
    void RecordRequest(const TransactionRequest& R)
    {
        std::string Key=std::to_string(unsigned(R.Kind))+"/"+R.RuleId.substr(0,128);
        std::lock_guard Lock(Mutex);
        if (!Stats.RequestRules.contains(Key)&&Stats.RequestRules.size()>=127) Key="overflow";
        ++Stats.RequestRules[Key];
    }
    std::string QueueFailure;
    double WorkerWaitMilliseconds=0,LastDiagnostic=0;
    double TimeOffset=0;
    std::unique_ptr<ClientWorld> Core; // exclusively worker-owned, including its destructor
    std::map<ObjectKey,ObjectInfo> Primed;
    PreflightCache Preflight;
    std::map<std::string,std::shared_ptr<Reply<PreflightCache>>> PreflightRequests;
    std::map<std::string,std::shared_ptr<Reply<RestoreCache>>> RestoreRequests;
    std::map<ObjectKey,std::uint64_t> ApplicationProgress;
    std::map<std::string,std::set<ObjectKey>> PreflightScopes,RestoreScopes;
    using CommandKey=std::tuple<Id,ObjectKey,Revision,Revision>;
    struct CommandRequest {std::shared_ptr<Reply<bool>> Result;std::shared_ptr<std::atomic<bool>> Active;};
    std::map<CommandKey,CommandRequest> CommandRequests;
    std::map<ObjectKey,std::shared_ptr<std::atomic<std::uint64_t>>> ApplicationGenerations;
    struct PendingBoolean {std::shared_ptr<Reply<bool>> Result;std::function<bool(ClientWorld&)> Run;};
    std::deque<PendingBoolean> PendingBooleans;
    std::set<ObjectKey> PendingRebuilds;
    CaptureCache Captured;
    RestoreCache Restored;
    std::map<ObjectKey,ObjectRecord> ChangedRecords;
    std::map<StateKey,Revision> ChangedVersions;
    bool ResourcesSeeded=false,ResourcesConfigured=false;
    bool MiningRestoreDirty=false;
    std::set<StateKey> ResourceKeys;
    std::map<std::uint32_t,std::uint8_t> PreviousRemaining;
    std::vector<Team> PreviousOwners;
    std::shared_ptr<const MiningReservationDirectory> MiningSnapshot=std::make_shared<MiningReservationDirectory>();
    Revision ResourceQuery=0,MiningQuery=0;
    std::map<Id,bool> SendAdmission;
    std::map<Id,bool> LastSendAdmission;
    struct IndexMetadata
    {
        std::set<std::pair<Team,std::string>> Capabilities;
        std::string Definition;
    };
    std::map<ObjectKey,IndexMetadata> PublishedIndexes;
    struct SimulationEntry {ObjectKey Key;std::uint64_t Token=0;std::string System;Tick At=0;double Delta=0;};
    std::vector<SimulationEntry> SimulationBatch;
    using PackedKey=std::tuple<MessageKind,Id,Id,Id,ObjectKey,std::string>;
    struct PackedValue {Bytes Plain,Packed;bool Encoded=false;std::list<PackedKey>::iterator Position;std::size_t ReferenceBytes=0;};
    std::map<PackedKey,PackedValue> PackedCache;
    std::list<PackedKey> PackedOrder;
    std::size_t PackedCacheBytes=0;
    std::map<ObjectKey,ObjectProxy*> Proxies;
    bool MetaDirty=true;

    State(Id M,Id P,Revision E,Factory F,Send SendValue,Pack PackValue,bool Evidence,std::function<void()> Init,double Origin,AsyncExecutionOptions Execution)
        :Match(M),Player(P),MatchEpoch(E),Options(Execution),Create(std::move(F)),Transmit(std::move(SendValue)),Compress(std::move(PackValue)),Detailed(Evidence),InitializeWorker(std::move(Init))
    {
        Numeric.Directory=&Directory;Numeric.ResourcesView.Value=ResourceReplica;TimeOffset=Origin?Origin-Seconds():0;
        Stats.CaptureYieldEnabled=Options.YieldBetweenCaptureCuts;Stats.FastPackCacheKeyEnabled=Options.FastPackCacheKey;
    }
    void CheckMain() const {assert(std::this_thread::get_id()==MainThread);}
    bool Post(std::function<void(ClientWorld&)> Fn,std::size_t Bytes=256,bool Control=false,bool RetainInput=false,AsyncInputKind Kind=AsyncInputKind::General)
    {
        std::lock_guard Lock(Mutex);if (Stopping) return false;
        if (Control && Kind==AsyncInputKind::General) Kind=AsyncInputKind::Control;
        auto& Profile=Stats.InputKinds[static_cast<std::size_t>(Kind)];
        if (!Control && (Bytes>QueueLimit || InputBytes>QueueLimit-Bytes)) {++Stats.InputRejected;++Profile.Rejected;return false;}
        // Confirmations can arrive in a large burst while the worker finishes
        // an accepted batch. Bound their independent lane by bytes, not by an
        // unrelated entry count that rejects small confirmations prematurely.
        if (Control && (Bytes>QueueLimit || ControlBytes>QueueLimit-Bytes))
        {++Profile.Rejected;QueueFailure="Reliable application control byte budget exhausted";Wake.notify_all();return false;}
        (Control?Controls:Inputs).push_back({std::move(Fn),Bytes,Seconds(),++InputSequence,RetainInput,Kind});
        ++Profile.Accepted;++Profile.QueuedCount;Profile.AcceptedBytes+=Bytes;Profile.QueuedBytes+=Bytes;
        if (!Control) Stats.QueuedInputBytes+=Bytes;
        if (Control) ControlBytes+=Bytes;else InputBytes+=Bytes;
        Stats.ControlBytes=ControlBytes;Stats.ControlPeak=std::max(Stats.ControlPeak,std::uint64_t(ControlBytes));
        Stats.InputBytes=InputBytes;Stats.InputPeak=std::max(Stats.InputPeak,std::uint64_t(InputBytes));Wake.notify_all();return true;
    }
    bool Emit(std::function<void()> Fn,std::size_t Bytes=256,bool RetainedOutbound=false,Id Reservation={},bool CommandWake=false)
    {
        // Every non-simulation observation is a fence. A batch never crosses
        // a command, ledger update, receipt, lifecycle change or capture.
        FlushSimulation();
        std::unique_lock Lock(Mutex);
        if (Bytes>QueueLimit) {QueueFailure="Application batch exceeds bounded output capacity";Wake.notify_all();return false;}
        const auto Credit=[&] {const auto I=ResultReservations.find(Reservation);return I==ResultReservations.end()?std::size_t(0):I->second.second;};
        const auto CanEnqueue=[&]{return Stopping || (Bytes<=QueueLimit && OutputBytes+ResultReservedBytes<=QueueLimit-Bytes+std::min(Bytes,Credit()));};
        if (!CanEnqueue())
        {
            const auto Start=Steady::now();TraceScope Wait("GuLiHybrid_OutputBackpressureWait");Wake.wait(Lock,CanEnqueue);
            WorkerWaitMilliseconds+=Milliseconds(Start);
        }
        if (Stopping) return false;
        if (auto I=ResultReservations.find(Reservation);I!=ResultReservations.end())
        {const auto Consumed=std::min(Bytes,I->second.second);I->second.second-=Consumed;ResultReservedBytes-=Consumed;}
        Applications.push_back({std::move(Fn),Bytes,++OutputSequence,Seconds(),RetainedOutbound,Match,MatchEpoch,Generation});OutputBytes+=Bytes;
        Stats.ResultReservedBytes=ResultReservedBytes;Stats.OutputBytes=OutputBytes+ResultReservedBytes;Stats.OutputPeak=std::max(Stats.OutputPeak,std::uint64_t(Stats.OutputBytes));
        Lock.unlock();Wake.notify_all();
        if (CommandWake) NotifyCommandReady();
        return true;
    }
    void NotifyCommandReady()
    {
        if (!Options.CommandApplicationReady) return;
        bool Notify=false;
        {
            std::lock_guard Lock(Mutex);
            if (Stopping) return;
            CommandWakeSequence=OutputSequence;
            if (!CommandWakeQueued) {CommandWakeQueued=true;Notify=true;++Stats.CommandWakeRequests;}
        }
        if (Notify) Options.CommandApplicationReady(Generation);
    }
    template<class T> const T* RequestMain(std::shared_ptr<Reply<T>>& Result,std::function<T()> Fn,std::size_t Bytes=256,
        const std::set<ObjectKey>* Scope=nullptr)
    {
        if (Result) return Result->Ready?&Result->Value:nullptr;
        Publish();Result=std::make_shared<Reply<T>>();auto R=Result;
        const std::vector<ObjectKey> Keys=Scope
            ?std::vector<ObjectKey>(Scope->begin(),Scope->end()):std::vector<ObjectKey>{};
        Emit([this,R,Fn=std::move(Fn),Bytes,Keys]() mutable
        {
            T Value=Fn();
            Post([this,R,Value=std::move(Value),Keys](ClientWorld&) mutable
            {
                R->Value=std::move(Value);R->Ready=true;
                for (const auto K:Keys) ++ApplicationProgress[K];
                std::lock_guard Lock(Mutex);++Stats.ApplyAcks;
            },Bytes,true);
        },Bytes);
        return nullptr;
    }
    ObjectInfo PrepareMain(const ObjectRecord& R,const Bytes* Private=nullptr,std::uint64_t Token=0)
    {
        TraceScope Scope("GuLiHybrid_MainPrepareObject");
        CheckMain();auto I=MainObjects.find(R.Grant.Object);
        if (I==MainObjects.end())
        {
            auto A=Create(R);if (!A || !A->Version()) return {};
            ObjectInfo Info;Info.Token=Token?Token:++NextToken;Info.Version=A->Version();Info.Systems=A->RequiredSystems();
            I=MainObjects.emplace(R.Grant.Object,MainObject{std::move(Info),std::move(A),R.Grant.Object}).first;
            MainObjectTokens.emplace(I->second.Info.Token,&I->second);
        }
        auto Info=I->second.Info;
        Info.Prepared=Private?I->second.Adapter->PrepareRestore(R,*Private):I->second.Adapter->Prepare(R);
        Info.Period=I->second.Adapter->AdvancePeriod();
        if (Options.GameplayWorker&&!Info.Codec) Info.Codec=I->second.Adapter->CreateMotionCodec();
        I->second.Info=Info;return Info;
    }
    MainObject* MainObjectFor(ObjectKey K,std::uint64_t Token)
    {
        CheckMain();
        const auto I=MainObjectTokens.find(Token);
        return I!=MainObjectTokens.end()&&I->second->Key==K?I->second:nullptr;
    }
    IObjectAdapter* Object(ObjectKey K,std::uint64_t Token)
    {const auto* O=MainObjectFor(K,Token);return O?O->Adapter.get():nullptr;}
    void Publish();
    void FlushSimulation();
    void AdvanceShared(const std::string&,Tick,double);
    void PublishDiagnostics();
    void Run();
    void Pump();
    void PumpCommands(std::uint64_t WorldGeneration);
    void Shutdown();
    void DispatchComputes();
    void CompleteComputes();
    std::unique_ptr<IObjectAdapter> MakeProxy(const ObjectRecord&);
    ClientWorld::ApplicationStatus PrepareBatch(std::span<const PrivateObjectState>,std::span<const PrivateSystemState>);
    ClientWorld::CaptureApplicationStatus CaptureBatch(std::span<const ObjectKey>,bool CurrentOnly);
    void AdvanceCaptures();
    void AdvanceAfterCaptureBarrier(bool AfterPolling=false);
    ClientWorld::ApplicationStatus RestoreBatch(const ObjectHandoffState&,std::span<const ObjectKey>,const std::map<std::string,std::vector<ObjectKey>>&,RestoreStage);
    void ApplyDelta(ReadDelta);
    bool Boolean(const std::string&,std::function<bool(ClientWorld&)>,std::size_t Bytes=256);
    void Deliver(const CommitResult&,bool LocalRejection=false,const std::string& Reason={});
    bool TransmitWorker(Id Peer,Message M);
    bool ReserveResult(const OwnerWork&);
    void ReleaseResult(Id);
};

struct AsyncClientWorld::State::ObjectProxy final : IObjectAdapter
{
    State& Host;ObjectKey Key;ObjectInfo Info;ObjectRecord Record;bool Preparing=false;
    std::optional<std::pair<Authority,bool>> LastAuthority;
    std::optional<std::pair<Revision,ObjectRecord>> LastLedger;
    ObjectProxy(State& S,const ObjectRecord& R,ObjectInfo I):Host(S),Key(R.Grant.Object),Info(std::move(I)),Record(R) {Host.Proxies[Key]=this;}
    std::uint32_t Version() const override {return Info.Version?Info.Version:1;}
    std::vector<std::string> RequiredSystems() const override {return Info.Systems;}
    void QueuePrepare()
    {
        if (Preparing || Host.Stopping) return;Preparing=true;const auto R=Record;const auto T=Info.Token;
        Host.Publish();Host.Emit([H=&Host,R,T]
        {
            TraceScope Scope("GuLiHybrid_MainPrepareDispatch");
            const auto Reply=H->PrepareMain(R,nullptr,T);
            H->Post([H,K=R.Grant.Object,T,Reply](ClientWorld& C)
            {
                const auto P=H->Proxies.find(K);
                if (P!=H->Proxies.end() && P->second->Info.Token==T)
                {P->second->Info.Period=Reply.Period;P->second->Info.Codec=Reply.Codec;C.CompleteApplication(K,Reply.Prepared,Reply.Version,Reply.Systems);}
            },256+Reply.Systems.size()*128,true);
        },Cost(R)+256);
    }
    bool Prepare(const ObjectRecord& R) override
    {
        Record=R;if (Info.Prepared) return true;QueuePrepare();return false;
    }
    bool IsPreparationPending() const override {return !Info.Prepared;}
    void RetryPreparation() override {QueuePrepare();}
    void PreparationCompleted(bool Ready,std::uint32_t V,const std::vector<std::string>& Names) override
    {Info.Prepared=Ready;if (V) Info.Version=V;Info.Systems=Names;Preparing=false;}
    bool PrepareRestore(const ObjectRecord& R,std::span<const std::uint8_t> B) override
    {
        Record=R;
        const auto I=Host.Preflight.Objects.find(Key);
        if (I!=Host.Preflight.Objects.end() && I->second.second.BytesValue==Bytes(B.begin(),B.end()))
        {Info=I->second.first;return I->second.second.Prepared;}
        return false; // Private restore must pass the resumable preflight first.
    }
    bool Validate(std::span<const std::uint8_t> B) const override
    {
        const auto I=Host.Preflight.Objects.find(Key);
        if (I!=Host.Preflight.Objects.end() && I->second.second.BytesValue==Bytes(B.begin(),B.end())) return I->second.second.Valid;
        return false;
    }
    std::vector<ObjectKey> RestoreDependencies(std::span<const std::uint8_t> B) const override
    {
        const auto I=Host.Preflight.Objects.find(Key);
        if (I!=Host.Preflight.Objects.end() && I->second.second.BytesValue==Bytes(B.begin(),B.end())) return I->second.second.Dependencies;
        return {};
    }
    void SetAuthority(const Authority& G,bool Local) override
    {
        if (LastAuthority && LastAuthority->first==G && LastAuthority->second==Local) return;
        LastAuthority=std::pair{G,Local};
        Record.Grant=G;Host.Publish();Host.Emit([H=&Host,K=Key,T=Info.Token,G,Local]
        {TraceScope Scope("GuLiHybrid_MainSetAuthority");if (auto* A=H->Object(K,T)) A->SetAuthority(G,Local);},256);
    }
    void Advance(Tick At,double Delta) override
    {
        // Publish every applied object clock, including adapters with no family
        // callback. Public event timestamps are rebased against this main-thread
        // age; dropping clock-only entries makes fresh effects appear expired.
        // The registered callback period is enforced when this batch is applied.
        Host.Publish();Host.SimulationBatch.push_back({Key,Info.Token,{},At,Delta});
        if (Host.SimulationBatch.size()>=4096) Host.FlushSimulation();
    }
    bool Command(const FrozenCommand& C,const CommandMember& M) override
    {
        const auto I=Host.CommandRequests.find({C.Event,Key,M.OwnerEpoch,M.ControlEpoch});
        if (I==Host.CommandRequests.end()||!I->second.Result->Ready) return false;
        const bool Accepted=I->second.Result->Value;Host.CommandRequests.erase(I);return Accepted;
    }
    bool Motion(MessageKind Kind,std::span<const std::uint8_t> B,Tick At)
    {
        if (Kind==MessageKind::Motion && Info.Codec)
        {
            TraceScope Scope("GuLiHybrid_WorkerPoseDecode");bool Valid=false;
            auto Decoded=Info.Codec->Decode(B,Record.Grant.OwnerEpoch,Host.MatchEpoch,Valid);const auto Owner=Record.Grant.OwnerEpoch;
            Host.Publish();Host.Emit([H=&Host,K=Key,T=Info.Token,Decoded=std::move(Decoded),Valid,At,Kind,Owner]
            {
                TraceScope Scope("GuLiHybrid_MainPoseApply");
                const auto O=H->Directory.find(K);auto* A=H->Object(K,T);
                const bool Current=O!=H->Directory.end()&&O->second.Grant.OwnerEpoch==Owner;
                const bool Good=Current&&Valid&&A&&(!Decoded||A->ApplyDecodedMotion(*Decoded,At));
                if (Good) O->second.LogicalAge=std::max(O->second.LogicalAge,At);
                H->Post([K,At,Kind,Owner,Good](ClientWorld& C){C.CompleteMotionApplication(K,Kind,At,Owner,Good);},128,true);
            },B.size()*4+2048);
            return !Host.Stopping;
        }
        const auto V=Bytes(B.begin(),B.end());const auto Owner=Record.Grant.OwnerEpoch;
        Host.Publish();Host.Emit([H=&Host,K=Key,T=Info.Token,V,At,Kind,Owner]
        {
            TraceScope Scope(Kind==MessageKind::Motion?"GuLiHybrid_MainPoseApply":
                Kind==MessageKind::ComponentState?"GuLiHybrid_MainPublicStateApply":"GuLiHybrid_MainObjectEventApply");
            auto* A=H->Object(K,T);bool Good=false;
            const auto O=H->Directory.find(K);
            if (A && O!=H->Directory.end() && O->second.Grant.OwnerEpoch==Owner)
            {
                if (Kind==MessageKind::Motion) Good=A->ApplyMotion(V,At);
                else if (Kind==MessageKind::ComponentState) Good=A->ApplyPublicState(V,At);
                else Good=A->ApplyObjectEvent(V,At);
                if (Good) O->second.LogicalAge=std::max(O->second.LogicalAge,At);
            }
            H->Post([K,At,Kind,Owner,Good](ClientWorld& C){C.CompleteMotionApplication(K,Kind,At,Owner,Good);},128,true);
        },V.size()+256);return !Host.Stopping;
    }
    bool DefersMotionResult() const override {return true;}
    bool ApplyMotion(std::span<const std::uint8_t> B,Tick At) override {return Motion(MessageKind::Motion,B,At);}
    bool ApplyPublicState(std::span<const std::uint8_t> B,Tick At) override {return Motion(MessageKind::ComponentState,B,At);}
    bool ApplyObjectEvent(std::span<const std::uint8_t> B,Tick At) override {return Motion(MessageKind::ObjectEvent,B,At);}
    bool MotionFeedback(Id Peer,std::span<const std::uint8_t> B) override
    {
        if (Info.Codec) return Info.Codec->Feedback(Peer,B);
        Bytes V(B.begin(),B.end());Host.Publish();Host.Emit([H=&Host,K=Key,T=Info.Token,Peer,V]
        {if (auto* A=H->Object(K,T)) A->MotionFeedback(Peer,V);},V.size()+128);return !Host.Stopping;
    }
    void ApplyLedger(const ObjectRecord& R,Revision V,bool Effects) override
    {
        if (!Effects && LastLedger && LastLedger->first==V && LastLedger->second==R) return;
        LastLedger=std::pair{V,R};
        Record=R;Host.Publish();Host.Emit([H=&Host,K=Key,T=Info.Token,R,V,Effects]
        {
            TraceScope Scope("GuLiHybrid_MainApplyLedger");const auto O=H->Directory.find(K);
            if (O!=H->Directory.end() && O->second.Grant.OwnerEpoch==R.Grant.OwnerEpoch && O->second.Grant.ControlEpoch==R.Grant.ControlEpoch)
                if (auto* A=H->Object(K,T)) A->ApplyLedger(R,V,Effects);
        },Cost(R)+256);
    }
    void BeginResourceRestore(Revision E) override
    {
        LastLedger.reset();
        Host.Emit([H=&Host,K=Key,T=Info.Token,E]
        {if (auto* A=H->Object(K,T)) A->BeginResourceRestore(E);},128);
    }
    bool Capture(Bytes& B) const override
    {const auto I=Host.Captured.Objects.find(Key);if (I==Host.Captured.Objects.end()||!I->second.Good) return false;B=I->second.Value;return true;}
    bool RestoreValue(std::span<const std::uint8_t> B,RestoreStage Stage,bool Query)
    {
        const auto I=Host.Restored.Objects.find(Key);
        if (Host.Restored.Stage==Stage && I!=Host.Restored.Objects.end()) return I->second;
        return false;
    }
    bool Restore(std::span<const std::uint8_t> B,RestoreStage Stage) override {return RestoreValue(B,Stage,false);}
    bool RestoreQuery(std::span<const std::uint8_t> B,RestoreStage Stage) override {return RestoreValue(B,Stage,true);}
    void Release() override
    {
        Host.Preflight.Objects.erase(Key);Host.Captured.Objects.erase(Key);Host.Restored.Objects.erase(Key);
        const auto I=Host.Proxies.find(Key);if (I!=Host.Proxies.end()&&I->second==this) Host.Proxies.erase(I);
        Host.Publish();Host.Emit([H=&Host,K=Key,T=Info.Token]
        {TraceScope Scope("GuLiHybrid_MainReleaseObject");const auto O=H->MainObjects.find(K);if (O!=H->MainObjects.end()&&O->second.Info.Token==T)
            {O->second.Adapter->Release();H->MainObjectTokens.erase(T);H->MainObjects.erase(O);}},128);
    }
    void ResetControlInput() override
    {
        for (auto I=Host.CommandRequests.begin();I!=Host.CommandRequests.end();)
            if (std::get<1>(I->first)==Key) {I->second.Active->store(false);I=Host.CommandRequests.erase(I);} else ++I;
        Host.Publish();Host.Emit([H=&Host,K=Key,T=Info.Token]{TraceScope Scope("GuLiHybrid_MainResetControlInput");if (auto* A=H->Object(K,T)) A->ResetControlInput();},128);
    }
};

struct AsyncClientWorld::State::SystemProxy final : ISharedStateAdapter
{
    State& Host;std::string Name;std::uint32_t Schema;
    SystemProxy(State& H,std::string N,std::uint32_t V):Host(H),Name(std::move(N)),Schema(V) {}
    std::uint32_t Version() const override {return Schema;}
    bool UsesObject(ObjectKey K) const override
    {const auto I=Host.Captured.Systems.find(Name);return I!=Host.Captured.Systems.end()&&I->second.Uses.contains(K);}
    void Advance(Tick At,double Delta) override
    {
        Host.Publish();Host.SimulationBatch.push_back({{},0,Name,At,Delta});
        if (Host.SimulationBatch.size()>=4096) Host.FlushSimulation();
    }
    bool Capture(const std::vector<ObjectKey>& Keys,Bytes& B) const override
    {const auto I=Host.Captured.Systems.find(Name);if (I==Host.Captured.Systems.end()||!I->second.Good||I->second.Keys!=Keys) return false;B=I->second.Value;return true;}
    bool Validate(const std::vector<PrivateSystemState>& Parts,const std::vector<ObjectKey>& Keys) const override
    {
        for (const auto& [ID,V]:Host.Preflight.Systems)
            if (Parts.size()==1 && V.Part.Name==Name && Parts[0].State==V.Part.State && Keys==V.Part.Objects) return V.Valid;
        return false;
    }
    bool RestoreValue(const std::vector<PrivateSystemState>& Parts,const std::vector<ObjectKey>& Keys,RestoreStage Stage,bool Query)
    {
        const auto I=Host.Restored.Systems.find(Name);if (Host.Restored.Stage==Stage&&I!=Host.Restored.Systems.end()) return I->second;
        // Abort cleanup is ordered after any already accepted restore stage.
        if (Stage!=RestoreStage::Detach) return false;
        std::size_t Size=256;for (const auto& P:Parts) Size+=P.State.size();
        return Host.Emit([H=&Host,N=Name,Parts,Keys,Stage,Query]
        {const auto A=H->MainSystems.find(N);if (A!=H->MainSystems.end()) {if (Query) A->second->RestoreQuery(Parts,Keys,Stage);else A->second->Restore(Parts,Keys,Stage);}},Size);
    }
    bool Restore(const std::vector<PrivateSystemState>& P,const std::vector<ObjectKey>& K,RestoreStage S) override {return RestoreValue(P,K,S,false);}
    bool RestoreQuery(const std::vector<PrivateSystemState>& P,const std::vector<ObjectKey>& K,RestoreStage S) override {return RestoreValue(P,K,S,true);}
};

std::unique_ptr<IObjectAdapter> AsyncClientWorld::State::MakeProxy(const ObjectRecord& R)
{
    ObjectInfo Info;Info.Token=++NextToken;Info.Version=1;
    if (const auto I=Preflight.Objects.find(R.Grant.Object);I!=Preflight.Objects.end()) Info=I->second.first;
    return std::make_unique<ObjectProxy>(*this,R,std::move(Info));
}
void AsyncClientWorld::State::FlushSimulation()
{
    if (SimulationBatch.empty()) return;
    auto Batch=std::exchange(SimulationBatch,{});
    std::size_t Size=256+Batch.size()*sizeof(SimulationEntry);
    for (const auto& E:Batch) Size+=E.System.size();
    Emit([this,Batch=std::move(Batch)]
    {
        const auto Started=Steady::now();TraceScope Scope("GuLiHybrid_MainLogicalBatch");
        const auto Previous=AppliedContext;const auto PreviousTick=AppliedContextTick;
        for (const auto& E:Batch)
        {
            if (!E.System.empty())
            {
                AdvanceShared(E.System,E.At,E.Delta);
                continue;
            }
            AppliedContext=E.Key;AppliedContextTick=E.At;
            auto* ObjectEntry=MainObjectFor(E.Key,E.Token);
            ObjectRecord* Record=ObjectEntry?ObjectEntry->Record:nullptr;
            if (!Record)
            {
                if (const auto O=Directory.find(E.Key);O!=Directory.end()) Record=&O->second;
                // std::map insert/assignment preserves record addresses. This
                // directory retains tombstones until the entire client ends.
                if (ObjectEntry) ObjectEntry->Record=Record;
            }
            if (Record) Record->LogicalAge=E.At;
            const auto Period=ObjectEntry?ObjectEntry->Info.Period:0;
            if (auto* A=ObjectEntry?ObjectEntry->Adapter.get():nullptr;A && Period && E.At%Period==0 && A->NeedsAdvanceOnMain(E.At))
            {TraceScope T("GuLiHybrid_MainSimulation");A->Advance(E.At,E.Delta);}
            AppliedContext=Previous;AppliedContextTick=PreviousTick;
        }
        std::lock_guard Lock(Mutex);Stats.SimulationMilliseconds+=Milliseconds(Started);
    },Size);
}
void AsyncClientWorld::State::AdvanceShared(const std::string& Name,Tick At,double Delta)
{
    const auto I=MainSystems.find(Name);if (I==MainSystems.end()) return;
    // A shared step can run before the next numeric readback. Its flight
    // samples and private events must use this exact step, not that older clock.
    const auto Previous=SharedContextTick;SharedContextTick=At;
    AppliedWorldTick=std::max(AppliedWorldTick,At);
    {TraceScope T("GuLiHybrid_MainSharedSimulation");I->second->Advance(At,Delta);}
    SharedContextTick=Previous;
}
void AsyncClientWorld::State::ApplyDelta(ReadDelta D)
{
    CheckMain();TraceScope Scope("GuLiHybrid_MainReadDelta");
    for (const auto& [K,C]:D.IndexChanges)
    {
        for (const auto& Index:C.Removed)
            if (auto I=CapabilityIndex.find(Index);I!=CapabilityIndex.end())
            {I->second.erase(K);if (I->second.empty()) CapabilityIndex.erase(I);}
        for (const auto& Index:C.Added) CapabilityIndex[Index].insert(K);
        if (!C.RemovedDefinition.empty())
            if (auto I=Definitions.find(C.RemovedDefinition);I!=Definitions.end()&&!--I->second) Definitions.erase(I);
        if (!C.AddedDefinition.empty()) ++Definitions[C.AddedDefinition];
    }
    for (auto& [K,R]:D.Records)
    {
        const auto Previous=Directory.find(K);
        if (Previous==Directory.end() || Previous->second.Grant!=R.Grant) HashDirty=true;
        Directory.insert_or_assign(K,std::move(R));
    }
    auto& R=*ResourceReplica;auto& Change=D.Resources;
    if (Change.Seed) {R={};R.Nodes=std::move(Change.Nodes);}
    R.Configured=Change.Configured;R.Version=Change.Version;R.Processed=Change.Processed;
    for (const auto& [K,V]:Change.Remaining) R.Remaining[K]=V;
    if (Change.OwnersChanged) R.Owners=std::move(Change.Owners);
    for (auto& [K,V]:Change.Miners) R.Miners[K]=std::move(V);
    for (auto& [K,V]:Change.Factories) R.Factories[K]=std::move(V);
    for (const auto K:Change.RemovedMiners) R.Miners.erase(K);
    for (const auto K:Change.RemovedFactories) R.Factories.erase(K);
    Numeric.MiningView.Value=std::move(D.Mining);
    Numeric.Accounts={D.Red,D.Blue};Numeric.ResourceDirectoryRevision=D.ResourceQuery;Numeric.MiningDirectoryRevision=D.MiningQuery;
    if (D.Reservations) Numeric.Reservations=std::move(*D.Reservations);
    for (const auto& [K,V]:D.Versions) Numeric.Versions[K]=V;
    // Retirement is an append-only high-water mark in both execution paths.
    // Replacing the whole history on every readback grows delivery cost with
    // every projectile ever fired, even when no new lifetime has retired.
    for (const auto& [K,V]:D.Retired) Numeric.Retired[K]=V;
    Numeric.BusyObjects=std::move(D.Busy);
    PublishedClock=D.Clock;Paused=D.Paused;Deferred=D.Deferred;OwnershipApplying=D.OwnershipApplying;Preparing=D.Preparing;Frozen=std::move(D.Frozen);
    if (!D.Failure.empty()) Failure=std::move(D.Failure);
    if (QueryObserver) QueryObserver(D.Query);
    for (const auto& [K,Record]:D.Records) if (RecordObserver) RecordObserver(Directory.at(K));
    std::lock_guard Lock(Mutex);++Stats.ReadDeltas;
}
void AsyncClientWorld::State::Publish()
{
    if (!Core || Stopping || (!MetaDirty&&ChangedRecords.empty()&&ChangedVersions.empty())) return;
    TraceScope Scope("GuLiHybrid_WorkerReadDelta");const auto& N=Core->NumericState();
    ReadDelta D;D.Records=std::exchange(ChangedRecords,{});D.Versions=std::exchange(ChangedVersions,{});
    for (const auto& [K,R]:D.Records)
    {
        auto& Previous=PublishedIndexes[K];IndexMetadata Next;ReadDelta::IndexChange C;
        if (R.Grant.State!=Life::Destroyed) for (const auto& [Name,Value]:R.Capabilities) Next.Capabilities.emplace(R.Grant.Faction,Name);
        if (R.Grant.Policy==OwnershipPolicy::CommanderActivity) Next.Definition=R.Definition;
        std::set_difference(Previous.Capabilities.begin(),Previous.Capabilities.end(),Next.Capabilities.begin(),Next.Capabilities.end(),std::back_inserter(C.Removed));
        std::set_difference(Next.Capabilities.begin(),Next.Capabilities.end(),Previous.Capabilities.begin(),Previous.Capabilities.end(),std::back_inserter(C.Added));
        if (Previous.Definition!=Next.Definition) {C.RemovedDefinition=Previous.Definition;C.AddedDefinition=Next.Definition;}
        if (!C.Removed.empty()||!C.Added.empty()||!C.RemovedDefinition.empty()||!C.AddedDefinition.empty()) D.IndexChanges.emplace(K,std::move(C));
        Previous=std::move(Next);
    }
    const auto& R=N.WorldResources().Snapshot();auto& Change=D.Resources;
    Change.Configured=R.Configured;Change.Version=R.Version;Change.Processed=R.Processed;
    if (!ResourcesSeeded || ResourcesConfigured!=R.Configured)
    {
        Change.Seed=true;Change.Nodes=R.Nodes;Change.Remaining=R.Remaining;Change.Miners=R.Miners;Change.Factories=R.Factories;
        Change.Owners=R.Owners;Change.OwnersChanged=true;PreviousRemaining=R.Remaining;PreviousOwners=R.Owners;
        ResourcesSeeded=true;ResourcesConfigured=R.Configured;++ResourceQuery;
    }
    else for (const auto& K:ResourceKeys)
    {
        if (K.Capability=="cargo" || K.Capability=="object")
        {const auto I=R.Miners.find(K.Object);if (I!=R.Miners.end()) Change.Miners[K.Object]=I->second;else Change.RemovedMiners.insert(K.Object);}
        if (K.Capability=="processor" || K.Capability=="object")
        {const auto I=R.Factories.find(K.Object);if (I!=R.Factories.end()) Change.Factories[K.Object]=I->second;else Change.RemovedFactories.insert(K.Object);}
        if (K.Capability.starts_with("ore/"))
        {
            std::uint32_t Node=0;for (std::size_t I=4;I<K.Capability.size()&&K.Capability[I]>='0'&&K.Capability[I]<='9';++I) Node=Node*10+std::uint32_t(K.Capability[I]-'0');
            const auto V=R.Remaining.find(Node);if (V!=R.Remaining.end() && (!PreviousRemaining.contains(Node)||PreviousRemaining[Node]!=V->second))
            {Change.Remaining[Node]=V->second;PreviousRemaining[Node]=V->second;++ResourceQuery;}
        }
        if (K.Capability.starts_with("territory/") && PreviousOwners!=R.Owners)
        {Change.Owners=R.Owners;Change.OwnersChanged=true;PreviousOwners=R.Owners;++ResourceQuery;}
    }
    ResourceKeys.clear();D.ResourceQuery=ResourceQuery;
    D.Query.ResourceSeed=Change.Seed;D.Query.OwnersChanged=Change.OwnersChanged;D.Query.Remaining=Change.Remaining;
    const auto& M=N.MiningPositions().Snapshot();
    if (MiningRestoreDirty || M.Version!=MiningSnapshot->Version || M.Configured!=MiningSnapshot->Configured)
    {
        if (M.Configured!=MiningSnapshot->Configured || M.Leases!=MiningSnapshot->Leases)
        {
            ++MiningQuery;D.Query.MiningChanged=true;
            for (const auto& [K,V]:M.Leases)
                if (const auto I=MiningSnapshot->Leases.find(K);I==MiningSnapshot->Leases.end()||I->second!=V) D.Query.Leases[K]=V;
            for (const auto& [K,V]:MiningSnapshot->Leases) if (!M.Leases.contains(K)) D.Query.RemovedLeases.insert(K);
        }
        MiningSnapshot=std::make_shared<MiningReservationDirectory>(MiningReservationDirectory{M.Configured,M.Version,M.Leases});
    }
    MiningRestoreDirty=false;
    D.Mining=MiningSnapshot;D.MiningQuery=MiningQuery;
    D.Red=N.TeamEconomy().Balance(Team::Red);D.Blue=N.TeamEconomy().Balance(Team::Blue);
    const std::array<Revision,2> AccountVersions{N.Version({TeamAccountObject(Team::Red),"account"}),N.Version({TeamAccountObject(Team::Blue),"account"})};
    if (AccountVersions!=PublishedAccountVersions) {D.Reservations=N.TeamEconomy().Snapshot().Pending;PublishedAccountVersions=AccountVersions;}
    D.Retired=Core->ConsumeRetiredLifeChanges();
    for (const auto& [K,E]:N.LockView()) D.Busy.insert(K.Object);
    D.Frozen=Core->FrozenObjects();for (const auto& [K,T]:Core->PendingApplications()) D.Frozen.insert(K);
    D.Clock=Core->Now();D.Paused=Core->IsPaused();D.Preparing=Core->PendingApplications().size();D.Deferred=Core->IsBootstrapMaterializationDeferred()||D.Preparing;
    D.OwnershipApplying=Core->IsApplyingOwnershipGroup();
    D.Failure=Core->Error();
    std::size_t Bytes=512+D.Retired.size()*64+D.Busy.size()*64+D.Frozen.size()*64;
    if (D.Reservations) Bytes+=D.Reservations->size()*128;
    for (const auto& [K,O]:D.Records) Bytes+=Cost(O);
    for (const auto& [K,C]:D.IndexChanges)
    {
        Bytes+=128+C.AddedDefinition.size()+C.RemovedDefinition.size();
        for (const auto& I:C.Added) Bytes+=128+I.second.size();
        for (const auto& I:C.Removed) Bytes+=128+I.second.size();
    }
    Bytes+=D.Versions.size()*128;MetaDirty=false;
    Bytes+=Change.Nodes.size()*256+Change.Remaining.size()*64+Change.Owners.size()*sizeof(Team)+Change.Miners.size()*256;
    Bytes+=(Change.RemovedMiners.size()+Change.RemovedFactories.size())*64;
    Bytes+=D.Query.Remaining.size()*64+D.Query.Leases.size()*256+D.Query.RemovedLeases.size()*64;
    for (const auto& [K,F]:Change.Factories) Bytes+=sizeof(F)+F.Queue.size()*256;
    Emit([this,D=std::move(D)]() mutable {ApplyDelta(std::move(D));},Bytes);
}
void AsyncClientWorld::State::Deliver(const CommitResult& R,bool LocalRejection,const std::string& Reason)
{
    {std::lock_guard Lock(Mutex);++Stats.ReceiptKinds[(std::uint16_t(R.Kind)<<8)|std::uint8_t(R.Status)];}
    Publish();PreparedCommit Prepared;Prepared.Result=R;Prepared.Encoding=EncodeCommit(R);Prepared.LocalRejection=LocalRejection;
    Prepared.LocalRejectionReason=Reason;
    if (Detailed) {Prepared.ResultHash=Sha256(Prepared.Encoding);Prepared.DecisionHash=Sha256(EncodeCommitDecision(R));}
    std::size_t Bytes=Prepared.Encoding.size()*2+512;for (const auto& O:R.Objects) Bytes+=Cost(O);for (const auto& O:R.Births) Bytes+=Cost(O);
    Emit([this,Prepared=std::move(Prepared)]
    {
        TraceScope Scope("GuLiHybrid_MainTransactionReceipt");
        if (!CurrentResult(Prepared.Result)) return;
        if (Prepared.Result.Status==CommitStatus::Interrupted&&!FirstInterruption(Prepared.Result)) return;
        if (Prepared.Result.Kind!=TransactionKind::AdmitWork&&Prepared.Result.Status!=CommitStatus::Deferred&&Prepared.Result.Status!=CommitStatus::Paused)
        {MainRequests.erase(Prepared.Result.Event);MainCallbackTokens.erase(Prepared.Result.Event);}
        if (Committed) Committed(Prepared);
    },Bytes,false,R.Event);
    ReleaseResult(R.Event);
}
bool AsyncClientWorld::State::ReserveResult(const OwnerWork& Work)
{
    // Reserve the actual result DTO and its encoded delivery before prepare acquires numeric locks.
    std::size_t Size=EncodeCommit(Work.Result).size()*4+4096;
    for (const auto& O:Work.Result.Objects) Size+=Cost(O)*4;
    for (const auto& O:Work.Result.Births) Size+=Cost(O)*4;
    std::lock_guard Lock(Mutex);const auto I=ResultReservations.find(Work.Event);
    if (I!=ResultReservations.end() && I->second.first==Work.Proposal) return true;
    const auto Previous=I==ResultReservations.end()?0:I->second.second;
    if (Size>QueueLimit/2 || ResultReservedBytes-Previous>QueueLimit/2-Size
        || OutputBytes+ResultReservedBytes-Previous>QueueLimit-Size) {++Stats.ResultBackpressure;return false;}
    ResultReservedBytes=ResultReservedBytes-Previous+Size;ResultReservations[Work.Event]={Work.Proposal,Size};
    Stats.ResultReservedBytes=ResultReservedBytes;Stats.OutputBytes=OutputBytes+ResultReservedBytes;Stats.OutputPeak=std::max(Stats.OutputPeak,Stats.OutputBytes);return true;
}
void AsyncClientWorld::State::ReleaseResult(Id Event)
{
    std::lock_guard Lock(Mutex);const auto I=ResultReservations.find(Event);if (I==ResultReservations.end()) return;
    ResultReservedBytes-=I->second.second;ResultReservations.erase(I);Stats.ResultReservedBytes=ResultReservedBytes;Stats.OutputBytes=OutputBytes+ResultReservedBytes;Wake.notify_all();
}
void AsyncClientWorld::State::PublishDiagnostics()
{
    const double Now=Seconds();if (!Core || Now-LastDiagnostic<.2) return;LastDiagnostic=Now;
    auto Transfers=Core->ActiveHandoffs();auto Motions=Core->MotionDiagnostics();auto Issues=Core->HandoffDiagnostics();auto Sizes=Core->HandoffMeasurements();
    auto Snapshots=Core->ResourceSnapshotStatuses(Detailed);
    auto First=Core->FirstReceiveRejections(),Recent=Core->RecentReceiveRejections();auto Waiting=Core->PendingTransactions();auto Work=Core->WorkDiagnostics();auto WorkCounts=Core->WorkCounts();
    for (auto& R:Waiting) {R.CapabilityPayload.clear();R.Admission.clear();}
    auto Open=Core->UnsettledReceipts();const auto OpenCount=Core->UnsettledReceiptCount(),Duplicates=Core->DuplicateResults(),Unknown=Core->RejectedMotionLives(),Bad=Core->RejectedStateImports();
    std::deque<PreparedCommit> OpenPrepared;
    if (Detailed) for (const auto& R:Open)
    {PreparedCommit P;P.Result=R;P.ResultHash=Sha256(EncodeCommit(R));P.DecisionHash=Sha256(EncodeCommitDecision(R));OpenPrepared.push_back(std::move(P));}
    const auto Project=[](CommitResult& R)
    {
        const auto Record=[](ObjectRecord& O){O.Capabilities.clear();O.FrozenWork.clear();O.Upgrades.clear();};
        for (auto& O:R.Objects) Record(O);for (auto& O:R.Births) Record(O);if (R.FinalizedWork) Record(*R.FinalizedWork);
    };
    for (auto& R:Open) Project(R);for (auto& R:OpenPrepared) Project(R.Result);
    auto Why=Core->LastStateImportFailureReason();std::map<ObjectKind,std::size_t> Executions;
    for (std::uint32_t K=0;K<256;++K) {const auto N=Core->ExecutedCommandCount(ObjectKind(K));if (N) Executions[ObjectKind(K)]=N;}
    std::size_t Bytes=4096+Transfers.size()*256+Motions.size()*256+Work.size()*1024;
    for (const auto& [K,V]:Snapshots) Bytes+=512+V.InstalledObjects.size()*32;
    for (const auto& R:Waiting) Bytes+=Cost(R);
    Emit([this,Transfers=std::move(Transfers),Motions=std::move(Motions),Issues=std::move(Issues),Sizes=std::move(Sizes),Snapshots=std::move(Snapshots),First=std::move(First),Recent=std::move(Recent),
        Waiting=std::move(Waiting),Work=std::move(Work),WorkCounts=std::move(WorkCounts),Open=std::move(Open),OpenPrepared=std::move(OpenPrepared),OpenCount,Duplicates,Unknown,Bad,Why=std::move(Why),Executions=std::move(Executions)]() mutable
    {
        TraceScope Scope("GuLiHybrid_MainDiagnosticsSnapshot");
        Handoffs=std::move(Transfers);Motion=std::move(Motions);HandoffIssues=std::move(Issues);HandoffSizes=std::move(Sizes);FirstRejections=std::move(First);RecentRejections=std::move(Recent);
        ResourceStatuses=std::move(Snapshots);
        Pending=std::move(Waiting);Diagnostics=std::move(Work);Counts=std::move(WorkCounts);Unsettled=std::move(Open);UnsettledPrepared=std::move(OpenPrepared);UnsettledCount=OpenCount;DuplicateCount=Duplicates;UnknownLives=Unknown;
        ImportRejected=Bad;ImportFailure=std::move(Why);CommandCounts=std::move(Executions);
    },Bytes);
}
bool AsyncClientWorld::State::TransmitWorker(Id Peer,Message M)
{
    TraceScope Scope("GuLiHybrid_WorkerEncodePack");Bytes Packed;bool HasPacked=false;
    // Packing may use immutable envelope identities; sequence/clock changes do
    // not change that dictionary. Exact body comparison is the authority; a
    // fingerprint collision can only cause a miss/replacement, never stale data.
    const bool Cacheable=ReliableMessage(M.Header.Kind) && M.Body.size()>=512;
    PackedKey Identity;
    auto Existing=PackedCache.end();
    if (Cacheable)
    {
        TraceScope KeyScope("GuLiHybrid_WorkerPackCacheKey");
        Identity={M.Header.Kind,M.Header.Match,M.Header.Sender,M.Header.Event,M.Header.Object,
            Options.FastPackCacheKey?PackCacheFingerprint(M.Body):Sha256(M.Body)};
        Existing=PackedCache.find(Identity);
    }
    if (Existing!=PackedCache.end() && Existing->second.Plain==M.Body)
    {PackedOrder.splice(PackedOrder.end(),PackedOrder,Existing->second.Position);Packed=Existing->second.Packed;HasPacked=Existing->second.Encoded;M.ReferenceBodyBytes=Existing->second.ReferenceBytes;}
    else
    {
        {TraceScope Compression("GuLiHybrid_WorkerPayloadCompression");HasPacked=Compress&&Compress(M,Packed);}
        constexpr std::size_t CacheBudget=8ull*1024*1024;
        const auto Size=M.Body.size()+Packed.size()+384;
        if (Cacheable && Size<=CacheBudget)
        {
            while (!PackedCache.empty() && PackedCacheBytes>CacheBudget-Size)
            {
                const auto Old=PackedCache.find(PackedOrder.front());
                PackedCacheBytes-=Old->second.Plain.size()+Old->second.Packed.size()+384;PackedCache.erase(Old);PackedOrder.pop_front();
            }
            if (const auto Old=PackedCache.find(Identity);Old!=PackedCache.end())
            {PackedCacheBytes-=Old->second.Plain.size()+Old->second.Packed.size()+384;PackedOrder.erase(Old->second.Position);PackedCache.erase(Old);}
            PackedOrder.push_back(Identity);PackedCache[Identity]={M.Body,Packed,HasPacked,std::prev(PackedOrder.end()),M.ReferenceBodyBytes};PackedCacheBytes+=Size;
        }
    }
    Publish();const auto BytesValue=M.Body.size()+Packed.size()+256;
    return Emit([this,Peer,M=std::move(M),Packed=std::move(Packed),HasPacked]() mutable
    {TraceScope Scope("GuLiHybrid_MainOutboundEnqueue");OutboundBytes+=M.Body.size()+Packed.size()+256;Outbound.emplace_back(Peer,std::move(M),std::move(Packed),HasPacked);},BytesValue,true);
}
void AsyncClientWorld::State::DispatchComputes()
{
    while (!Stopping&&!WaitingComputes.empty())
    {
        const auto Job=WaitingComputes.front();
        if (!Options.GameplayExecutor || !Job->Work->CanRunParallel())
        {
            const auto Start=Steady::now();Job->QueueMilliseconds=(Seconds()-Job->Submitted)*1000.;
            {TraceScope Scope("GuLiHybrid_WorkerGameplayCompute");Job->Work->Compute();}
            Job->ComputeMilliseconds=Milliseconds(Start);Job->Done=true;WaitingComputes.pop_front();
            continue;
        }
        {std::lock_guard Lock(Mutex);++ActiveCalculations;}
        const bool Accepted=Options.GameplayExecutor->TrySubmit([this,Job]
        {
            const auto Start=Steady::now();const auto Delay=(Seconds()-Job->Submitted)*1000.;
            if (!Stopping)
            {
                {TraceScope Scope("GuLiHybrid_WorkerGameplayCompute");Job->Work->Compute();}
                const auto Elapsed=Milliseconds(Start);
                // No worker-pool task accesses ClientWorld or an application callback.
                Post([Job,Delay,Elapsed](ClientWorld&)
                {Job->QueueMilliseconds=Delay;Job->ComputeMilliseconds=Elapsed;Job->Done=true;},128,true);
            }
            {std::lock_guard Lock(Mutex);--ActiveCalculations;Wake.notify_all();}
        },Job->InputSize);
        if (!Accepted)
        {
            {std::lock_guard Lock(Mutex);--ActiveCalculations;}
            break; // Accepted input stays charged; retry without blocking the owner.
        }
        WaitingComputes.pop_front();
    }
}
void AsyncClientWorld::State::CompleteComputes()
{
    // Independent calculations may finish out of order. Their application order
    // stays the admission order, and the group is retained until application ACK.
    while (!Stopping&&!ComputeOrder.empty()&&ComputeOrder.front()->Done)
    {
        auto Job=std::move(ComputeOrder.front());ComputeOrder.pop_front();Publish();
        Emit([this,Job]
        {
            TraceScope Scope("GuLiHybrid_MainGameplayApply");
            if (TimingObserver)
            {
                TimingObserver("HybridGameplayCompute",Job->ComputeMilliseconds);
                TimingObserver("HybridGameplayQueueWait",Job->QueueMilliseconds);
            }
            const auto I=GameplayApplications.find(Job->Token);
            if (I!=GameplayApplications.end())
            {
                auto Application=std::move(I->second);GameplayApplications.erase(I);
                Application.Apply();GameplayGroups.erase(Application.Group);
            }
            Post([this,Job](ClientWorld&)
            {
                PendingGameplay.erase(Job->Token);std::lock_guard Lock(Mutex);
                GameplayInputBytes-=Job->InputSize;InputBytes-=Job->InputSize;Stats.InputBytes=InputBytes;Wake.notify_all();
            },128,true);
        },Job->ResultSize,false,Job->Reservation);
        ReleaseResult(Job->Reservation);
    }
}
void AsyncClientWorld::State::Run()
{
    assert(std::this_thread::get_id()!=MainThread);
    if (InitializeWorker) InitializeWorker();
    Core=std::make_unique<ClientWorld>(Match,Player,MatchEpoch,[this](const ObjectRecord& R){return MakeProxy(R);},[this](Id Peer,Message M)
    {return TransmitWorker(Peer,std::move(M));});
    Core->SetPeerTransactionDeferral(Options.DeferPeerTransactions);
    Core->SetResourceValidationSnapshotReuse(Options.ReuseResourceValidationSnapshot);
    Core->SetRecordObserver([this](const ObjectRecord& R){ChangedRecords[R.Grant.Object]=R;ResourceKeys.insert({R.Grant.Object,"object"});++ObjectRevisions[R.Grant.Object];});
    Core->SetFragmentObserver([this](std::span<const StateFragment> Fs)
    {
        for (const auto& F:Fs)
        {
            const auto V=Core->NumericState().Version(F.Key);if (V!=F.Version) continue;++ObjectRevisions[F.Key.Object];
            // A complete recovery cut may change values while retaining an
            // already seen revision. Invalidate from numeric fragments once per
            // batch, including groups with no materialized object adapters.
            if (F.Key.Capability.starts_with("leases/")) MiningRestoreDirty=true;
            if (F.Key.Capability=="account") PublishedAccountVersions={UINT64_MAX,UINT64_MAX};
            if (F.Key.Capability=="object"||F.Key.Capability=="cargo"||F.Key.Capability=="processor"||F.Key.Capability.starts_with("ore/")||F.Key.Capability.starts_with("territory/")) ResourceKeys.insert(F.Key);
            if (Detailed || (!F.Key.Capability.starts_with("event/")&&!F.Key.Capability.starts_with("attack/"))) ChangedVersions[F.Key]=V;
        }
    });
    Core->SetCommitCallback([this](const CommitResult& R){Deliver(R);});
    Core->SetApplicationResultBudget([this](const OwnerWork& W){return ReserveResult(W);},[this](Id E){ReleaseResult(E);});
    Core->SetCommandReceiptCallback([this](const CommandReceipt& R)
    {
        Publish();Emit([this,R]
        {if (Receipts.size()>=16384&&!Receipts.contains(R.Event)) Receipts.erase(Receipts.begin());Receipts[R.Event]=R;if (CommandCompleted) CommandCompleted(R);},R.Members.size()*128+256);
    });
    Core->SetHandoffAdmission([this](Id Peer){const auto I=SendAdmission.find(Peer);return I!=SendAdmission.end()&&I->second;});
    Core->SetApplicationBatches([this](auto O,auto S){return PrepareBatch(O,S);},[this](auto K,bool Current){return CaptureBatch(K,Current);},
        [this](const auto& S,auto K,const auto& Systems,auto Stage){return RestoreBatch(S,K,Systems,Stage);});
    Core->SetLogicalStepBoundary([this]
    {
        FlushSimulation();
        // A step may contain several batches separated by lifecycle fences.
        // Wake only after the whole following step, including shared systems.
        if (std::exchange(WakeNextCommandSimulation,false)) NotifyCommandReady();
    });
    Core->SetApplicationProgressRevision([this](ObjectKey K)
    {const auto I=ApplicationProgress.find(K);return I==ApplicationProgress.end()?0:I->second;});
    Core->SetApplicationCancellation([this](ObjectKey K)
    {
        ++ApplicationProgress[K];
        if (const auto I=ApplicationGenerations.find(K);I!=ApplicationGenerations.end()) ++*I->second;
        for (auto I=PreflightScopes.begin();I!=PreflightScopes.end();)
            if (I->second.contains(K)) {PreflightRequests.erase(I->first);I=PreflightScopes.erase(I);} else ++I;
        for (auto I=RestoreScopes.begin();I!=RestoreScopes.end();)
            if (I->second.contains(K)) {RestoreRequests.erase(I->first);I=RestoreScopes.erase(I);} else ++I;
        Preflight.Objects.erase(K);Restored={};
    });
    Core->SetCommandApplicationCancellation([this](const FrozenCommand& C)
    {
        for (const auto& M:C.Members) for (auto I=CommandRequests.begin();I!=CommandRequests.end();)
            if (std::get<0>(I->first)==C.Event&&std::get<1>(I->first)==M.Object)
            {I->second.Active->store(false);I=CommandRequests.erase(I);} else ++I;
    });
    Core->SetApplicationCommands([this](const FrozenCommand& Command,std::span<const CommandMember> Members)
    {
        struct Entry {CommandMember Member;std::uint64_t Token;Tick At;CommandRequest Request;};
        std::vector<Entry> Missing;bool Ready=true;
        for (const auto& M:Members)
        {
            const CommandKey K{Command.Event,M.Object,M.OwnerEpoch,M.ControlEpoch};
            auto [I,New]=CommandRequests.try_emplace(K);
            if (New)
            {
                I->second={std::make_shared<Reply<bool>>(),std::make_shared<std::atomic<bool>>(true)};
                const auto P=Proxies.find(M.Object);
                Missing.push_back({M,P==Proxies.end()?0:P->second->Info.Token,Core->ObjectNow(M.Object),I->second});
            }
            Ready&=I->second.Result->Ready;
        }
        if (!Missing.empty())
        {
            if (Options.CommandTiming) Options.CommandTiming("application-prepare",Command.Event,Missing.size());
            const auto Size=Command.Arguments.size()+Missing.size()*256+1024;
            Publish();const bool Queued=Emit([this,Command,Missing=std::move(Missing)]
            {
                if (Options.CommandTiming) Options.CommandTiming("application-start",Command.Event,Missing.size());
                TraceScope Scope("GuLiHybrid_MainCommandBatch");std::vector<std::pair<std::shared_ptr<Reply<bool>>,bool>> Results;
                for (const auto& E:Missing)
                {
                    const auto& M=E.Member;const auto Old=AppliedContext;const auto OldTick=AppliedContextTick;
                    AppliedContext=M.Object;AppliedContextTick=E.At;
                    auto* A=Object(M.Object,E.Token);const auto O=Directory.find(M.Object);
                    const bool Current=E.Request.Active->load()&&O!=Directory.end()&&O->second.Grant.OwnerEpoch==M.OwnerEpoch
                        &&O->second.Grant.ControlEpoch==M.ControlEpoch&&O->second.Grant.State==Life::Alive&&!O->second.Grant.Dormant;
                    Results.emplace_back(E.Request.Result,Current&&A&&A->Command(Command,M));AppliedContext=Old;AppliedContextTick=OldTick;
                }
                const auto Bytes=Results.size()*64+128;
                Post([Results=std::move(Results)](ClientWorld&)
                {for (const auto& [R,Value]:Results) {R->Value=Value;R->Ready=true;}},Bytes,true);
            },Size,false,{},true);
            // Wake once more for the first following logical step. It uses
            // the original elapsed time and FIFO, without adding a sim step.
            if (Queued && Options.CommandApplicationReady) WakeNextCommandSimulation=true;
            if (Options.CommandTiming) Options.CommandTiming("application-emitted",Command.Event,Members.size());
        }
        if (!Ready) return ClientWorld::ApplicationStatus::Pending;
        return Stopping?ClientWorld::ApplicationStatus::Rejected:ClientWorld::ApplicationStatus::Ready;
    });
    Core->SetCommandTimingObserver(Options.CommandTiming);
    while (!Stopping)
    {
        {
            std::unique_lock Lock(Mutex);
            if (Inputs.empty()&&Controls.empty() && !(DeferredAdvanceSeconds>0 && CaptureBarriers.empty())
                && !Core->HasPendingNetworkRetryBatch())
                Wake.wait_for(Lock,std::chrono::milliseconds(10),[&]{return Stopping||!Inputs.empty()||!Controls.empty();});
            if (Stopping) break;
            if (!QueueFailure.empty()) Core->Cancel(QueueFailure);
        }
        const auto Started=Steady::now();const auto PreviousWait=WorkerWaitMilliseconds;MetaDirty=true;
        constexpr unsigned MaximumInputs=256;
        std::vector<double> InputDelays;InputDelays.reserve(MaximumInputs);
        {
            TraceScope Scope("GuLiHybrid_TransactionWorker");
            // The original runtime polls workflows once after a frame's ordered
            // input. Preserve that order while amortizing polling/readback over
            // a bounded batch; one poll per packet otherwise overwhelms the
            // worker during bootstrap and retained-flight bursts.
            // A burst of cheap pose/ACK inputs should use the existing two-ms
            // quantum before re-running every handoff workflow. Causal input
            // order, the time bound and all simulation steps remain unchanged.
            for (unsigned Count=0;Count<MaximumInputs && !Stopping;++Count)
            {
                Input Task;
                {
                    std::lock_guard Lock(Mutex);
                    auto* Q=Inputs.empty()?&Controls:Controls.empty()?&Inputs:Inputs.front().Sequence<Controls.front().Sequence?&Inputs:&Controls;
                    if (Q->empty()) break;
                    Task=std::move(Q->front());Q->pop_front();
                    if (Q==&Inputs) {if (!Task.Retained) InputBytes-=Task.Bytes;}else ControlBytes-=Task.Bytes;
                    if (Q==&Inputs) Stats.QueuedInputBytes-=Task.Bytes;
                    auto& Profile=Stats.InputKinds[static_cast<std::size_t>(Task.Kind)];
                    --Profile.QueuedCount;Profile.QueuedBytes-=Task.Bytes;
                    Stats.InputBytes=InputBytes;Stats.ControlBytes=ControlBytes;++Stats.Inputs;
                }
                Captured={};
                const auto Delay=(Seconds()-Task.Enqueued)*1000.;
                InputDelays.push_back(Delay);
                {std::lock_guard Lock(Mutex);Stats.MaximumQueueMilliseconds=std::max(Stats.MaximumQueueMilliseconds,Delay);}
                const auto TaskStarted=Steady::now();const auto WaitBefore=WorkerWaitMilliseconds;
                {TraceScope InputScope(AsyncInputNames[static_cast<std::size_t>(Task.Kind)]);Task.Run(*Core);}
                {
                    std::lock_guard Lock(Mutex);auto& Profile=Stats.InputKinds[static_cast<std::size_t>(Task.Kind)];
                    ++Profile.Processed;Profile.ProcessedBytes+=Task.Bytes;
                    Profile.WorkMilliseconds+=std::max(0.,Milliseconds(TaskStarted)-(WorkerWaitMilliseconds-WaitBefore));
                    Profile.WaitMilliseconds+=Delay;Profile.MaximumWaitMilliseconds=std::max(Profile.MaximumWaitMilliseconds,Delay);
                }
                if (Milliseconds(Started)-(WorkerWaitMilliseconds-PreviousWait)>=2.) break;
            }
            if (!Stopping)
            {
                for (auto I=CommandRequests.begin();I!=CommandRequests.end();)
                {
                    const auto& [Event,K,Owner,Control]=I->first;const auto O=Core->Objects().find(K);
                    if (O==Core->Objects().end()||O->second.Grant.OwnerEpoch!=Owner||O->second.Grant.ControlEpoch!=Control
                        ||O->second.Grant.State!=Life::Alive||O->second.Grant.SimOwner!=Player||O->second.Grant.Dormant)
                    {I->second.Active->store(false);I=CommandRequests.erase(I);} else ++I;
                }
                // Resumptions run only on the owner thread. A pending application
                // never blocks transport maintenance or an independent input.
                const auto Count=PendingBooleans.size();
                for (std::size_t I=0;I<Count;++I)
                {
                    auto P=std::move(PendingBooleans.front());PendingBooleans.pop_front();
                    Core->ResetApplicationPending();const bool Value=P.Run(*Core);
                    if (!Value && Core->LastApplicationPending()) PendingBooleans.push_back(std::move(P));
                    else {Publish();Emit([R=P.Result,Value]{R->Value=Value;R->Ready=true;});}
                }
                for (auto I=PendingRebuilds.begin();I!=PendingRebuilds.end();)
                {
                    Core->ResetApplicationPending();const bool Ready=Core->RebuildFromReplica(*I);
                    if (Ready||!Core->LastApplicationPending()) I=PendingRebuilds.erase(I);else ++I;
                }
                DispatchComputes();CompleteComputes();
                {TraceScope Poll("GuLiHybrid_WorkerNetworkMaintenance");Core->PollNetwork(Seconds()+TimeOffset,Options.NetworkRetryBatchLimit);}
                AdvanceCaptures();AdvanceAfterCaptureBarrier(true);
            }
            MetaDirty=true;Publish();PublishDiagnostics();
        }
        const auto WorkTime=std::max(0.,Milliseconds(Started)-(WorkerWaitMilliseconds-PreviousWait));
        {std::lock_guard Lock(Mutex);Stats.WorkerMilliseconds+=WorkTime;}
        const auto TimingBytes=128+InputDelays.size()*sizeof(double);
        Emit([this,WorkTime,InputDelays=std::move(InputDelays)]
        {TraceScope Scope("GuLiHybrid_MainInputTiming");if (TimingObserver) {TimingObserver("HybridWorkerCompute",WorkTime);for (const auto Delay:InputDelays) TimingObserver("HybridInputQueueWait",Delay);}},TimingBytes);
    }
    Core.reset();
}
void AsyncClientWorld::State::Pump()
{
    CheckMain();
    if (Pumping) {CommandWakeDeferred=true;return;}
    Pumping=true;
    const auto Started=Steady::now();const auto PreviousSimulation=Stats.SimulationMilliseconds;std::deque<Application> Ready;
    std::size_t ReleasedBytes=0,AppliedCount=0;double MaximumDelay=0;
    const auto ReleaseBatch=[&]
    {
        if (!AppliedCount&&!ReleasedBytes) return;
        TraceScope Scope("GuLiHybrid_MainReleaseCredit");
        {std::lock_guard Lock(Mutex);OutputBytes-=ReleasedBytes;Stats.OutputBytes=OutputBytes+ResultReservedBytes;
            Stats.Applications+=AppliedCount;Stats.LastQueueMilliseconds=std::max(Stats.LastQueueMilliseconds,MaximumDelay);
            Stats.MaximumQueueMilliseconds=std::max(Stats.MaximumQueueMilliseconds,MaximumDelay);}
        ReleasedBytes=AppliedCount=0;MaximumDelay=0;Wake.notify_all();
    };
    {
        std::lock_guard Lock(Mutex);Ready.swap(Applications);Stats.LastQueueMilliseconds=0;
        if (!QueueFailure.empty()&&Failure.empty()) Failure=QueueFailure;
    }
    {TraceScope Scope("GuLiHybrid_MainApplyBatch");for (auto& Job:Ready)
    {
        ++AppliedSequence;assert(Job.Sequence==AppliedSequence);
        double Delay;
        {TraceScope OutputTiming("GuLiHybrid_MainOutputTiming");Delay=(Seconds()-Job.Enqueued)*1000.;if (TimingObserver) TimingObserver("HybridOutputQueueWait",Delay);}
        if (Job.Room==Match && Job.Epoch==MatchEpoch && Job.Generation==Generation)
        {TraceScope DispatchScope("GuLiHybrid_MainDispatchJob");Job.Run();}
        if (!Job.RetainedOutbound) ReleasedBytes+=Job.Bytes;
        ++AppliedCount;MaximumDelay=std::max(MaximumDelay,Delay);
        if (AppliedCount>=64) ReleaseBatch();
    }}
    ReleaseBatch();
    // Keep a failed enqueue and all later messages to that same peer in order.
    // Independent destinations have separate reliable channels: one unavailable
    // peer must not hold their bootstrap/receipts behind its transport credit.
    {
        std::set<Id> BlockedPeers;decltype(Outbound) Remaining;
        for (auto& Packet:Outbound)
        {
            auto& [Peer,M,Packed,HasPacked]=Packet;
            if (BlockedPeers.contains(Peer) || !Transmit(Peer,M,Packed,HasPacked))
            {BlockedPeers.insert(Peer);Remaining.push_back(std::move(Packet));continue;}
            const auto Released=M.Body.size()+Packed.size()+256;OutboundBytes-=Released;ReleasedBytes+=Released;
        }
        Outbound.swap(Remaining);
    }
    ReleaseBatch();
    std::map<Id,bool> Admission;for (const auto& [ID,M]:MainMembers) if (M.Online&&M.Ready) Admission[ID]=OutboundBytes<QueueLimit&&(!HandoffAdmission||HandoffAdmission(ID));
    if (Admission!=LastSendAdmission)
    {
        const auto Copy=Admission;
        if (Post([this,Admission=std::move(Admission)](ClientWorld&) mutable {SendAdmission=std::move(Admission);},256,true)) LastSendAdmission=Copy;
    }
    bool Notify=false;
    {
        std::lock_guard Lock(Mutex);Stats.MainApplyMilliseconds+=std::max(0.,Milliseconds(Started)-(Stats.SimulationMilliseconds-PreviousSimulation));
        if (CommandWakeDeferred && CommandWakeSequence>AppliedSequence && !CommandWakeQueued && !Stopping
            && Options.CommandApplicationReady)
        {CommandWakeQueued=true;Notify=true;++Stats.CommandWakeRequests;}
    }
    CommandWakeDeferred=false;Pumping=false;
    if (Notify) Options.CommandApplicationReady(Generation);
}
void AsyncClientWorld::State::PumpCommands(std::uint64_t WorldGeneration)
{
    CheckMain();
    if (WorldGeneration!=Generation) return;
    {
        std::lock_guard Lock(Mutex);CommandWakeQueued=false;
        if (Stopping || CommandWakeSequence<=AppliedSequence) return;
        if (Pumping) {CommandWakeDeferred=true;return;}
        ++Stats.CommandWakePumps;
    }
    // Consume the existing ordered queue, including all causal predecessors.
    // No command is extracted out of order and an already consumed wake is free.
    TraceScope Scope("GuLiHybrid_CommandApplicationWake");Pump();
}
void AsyncClientWorld::State::Shutdown()
{
    CheckMain();{std::lock_guard Lock(Mutex);Stopping=true;Wake.notify_all();}
    GameplayApplications.clear();GameplayGroups.clear();
    if (Worker.joinable()) Worker.join();
    // Only shutdown waits. Pool tasks contain values and can finish without GT
    // work; their completion posting sees Stopping and cannot revive this world.
    {std::unique_lock Lock(Mutex);Wake.wait(Lock,[&]{return ActiveCalculations==0;});}
    WaitingComputes.clear();ComputeOrder.clear();
    Applications.clear();Inputs.clear();Controls.clear();Outbound.clear();Callbacks.clear();
    PendingGameplay.clear();ResultReservations.clear();
    InputBytes=OutputBytes=ControlBytes=GameplayInputBytes=ResultReservedBytes=OutboundBytes=0;
    Stats.InputBytes=Stats.OutputBytes=Stats.ControlBytes=Stats.ResultReservedBytes=Stats.QueuedInputBytes=0;
    for (auto& Kind:Stats.InputKinds) Kind.QueuedBytes=Kind.QueuedCount=0;
    for (auto& [K,A]:MainObjects) A.Adapter->Release();MainObjectTokens.clear();MainObjects.clear();MainSystems.clear();
}
ClientWorld::ApplicationStatus AsyncClientWorld::State::PrepareBatch(std::span<const PrivateObjectState> Values,std::span<const PrivateSystemState> Shared)
{
    TraceScope Trace("GuLiHybrid_WorkerPrepareBatch");
    using Status=ClientWorld::ApplicationStatus;
    Preflight={};if (Values.empty()&&Shared.empty()) return Status::Ready;
    std::vector<std::string> Completed;
    std::map<ObjectKey,const PrivateObjectState*> ByKey;for (const auto& P:Values) if (!ByKey.emplace(P.Record.Grant.Object,&P).second) return Status::Rejected;
    std::vector<const PrivateObjectState*> Ordered;std::set<ObjectKey> Visiting,Done;
    std::function<bool(ObjectKey)> Visit=[&](ObjectKey K)
    {
        if (Done.contains(K)) return true;if (!Visiting.insert(K).second) return false;
        const auto* P=ByKey.at(K);if (ByKey.contains(P->Record.Parent)&&!Visit(P->Record.Parent)) return false;
        Visiting.erase(K);Done.insert(K);Ordered.push_back(P);return true;
    };
    for (const auto& [K,P]:ByKey) if (!Visit(K)) return Status::Rejected;
    // Private cuts are bounded independently. Split application requests so their queue never contains an oversized aggregate.
    std::vector<PrivateObjectState> Chunk;std::size_t Bytes=1024;
    const auto Flush=[&]()
    {
        if (Chunk.empty()) return Status::Ready;
        const auto Expected=std::count_if(Chunk.begin(),Chunk.end(),[](const auto& P){return P.Record.Materialized;});
        ObjectHandoffState Identity;Identity.Objects=Chunk;const auto Key=Sha256(EncodeHandoffState(Identity));
        std::vector<std::pair<std::shared_ptr<std::atomic<std::uint64_t>>,std::uint64_t>> Generations;
        for (const auto& P:Chunk)
        {
            const auto K=P.Record.Grant.Object;PreflightScopes[Key].insert(K);
            auto& G=ApplicationGenerations[K];if (!G) G=std::make_shared<std::atomic<std::uint64_t>>(0);
            Generations.emplace_back(G,G->load());
        }
        const auto* Results=RequestMain<PreflightCache>(PreflightRequests[Key],[this,Objects=std::move(Chunk),Generations]
        {
            TraceScope Scope("GuLiHybrid_MainPrepareBatch");PreflightCache Result;
            for (const auto& [G,V]:Generations) if (G->load()!=V) return Result;
            std::map<ObjectKey,const PrivateObjectState*> PendingObjects;for (const auto& O:Objects) PendingObjects[O.Record.Grant.Object]=&O;
            std::set<ObjectKey> Visiting;
            std::function<bool(ObjectKey)> PrepareOne=[&](ObjectKey K)
            {
                if (Result.Objects.contains(K)) return true;
                if (!Visiting.insert(K).second) return false;
                const auto& P=*PendingObjects.at(K);
                if (PendingObjects.contains(P.Record.Parent)&&!PrepareOne(P.Record.Parent)) return false;
                if (P.Record.Materialized)
                {
                    ObjectInfo Info;
                    if (const auto I=MainObjects.find(K);I!=MainObjects.end()&&I->second.Info.Prepared) Info=I->second.Info;
                    else Info=PrepareMain(P.Record,P.State.empty()?nullptr:&P.State);
                    Validation V;V.Prepared=Info.Prepared;V.BytesValue=P.State;
                    if (auto* A=Object(K,Info.Token)) {V.Valid=P.State.empty()||A->Validate(P.State);if (!P.State.empty()) V.Dependencies=A->RestoreDependencies(P.State);}
                    Result.Objects[K]={std::move(Info),std::move(V)};
                }
                Visiting.erase(K);return true;
            };
            for (const auto& [K,P]:PendingObjects) if (!PrepareOne(K)) return PreflightCache{};
            return Result;
        },Bytes,&PreflightScopes[Key]);
        if (!Results) return Status::Pending;
        if (Results->Objects.size()!=std::size_t(Expected)) {PreflightRequests.erase(Key);PreflightScopes.erase(Key);return Status::Rejected;}
        for (const auto& [K,V]:Results->Objects)
        {
            if (!V.first.Token || !V.second.Prepared || !V.second.Valid) {PreflightRequests.erase(Key);PreflightScopes.erase(Key);return Status::Rejected;}
            Preflight.Objects[K]=V;
            if (const auto I=Proxies.find(K);I!=Proxies.end()) I->second->Info=V.first;
        }
        Completed.push_back(Key);Chunk.clear();Bytes=1024;return Stopping?Status::Rejected:Status::Ready;
    };
    for (const auto* Value:Ordered)
    {
        const auto& P=*Value;
        const auto Size=Cost(P.Record)+P.State.size()+512;
        if (Bytes+Size>32ull*1024*1024) {const auto ChunkStatus=Flush();if (ChunkStatus!=Status::Ready) return ChunkStatus;}
        if (Size>QueueLimit-1024) return Status::Rejected;Chunk.push_back(P);Bytes+=Size;
    }
    {const auto ChunkStatus=Flush();if (ChunkStatus!=Status::Ready) return ChunkStatus;}
    // There are few shared systems; keep each source shard's validation distinct.
    for (const auto& P:Shared)
    {
        ObjectHandoffState Identity;Identity.Systems={P};const auto Key=Sha256(EncodeHandoffState(Identity));
        PreflightScopes[Key].insert(P.Objects.begin(),P.Objects.end());
        const auto* Result=RequestMain<PreflightCache>(PreflightRequests[Key],[this,P,Key]
        {
            const auto I=MainSystems.find(P.Name);PreflightCache R;
            R.Systems[Key]={P,I!=MainSystems.end()&&I->second->Version()==P.AdapterVersion&&I->second->Validate({P},P.Objects)};return R;
        },P.State.size()+P.Objects.size()*64+256,&PreflightScopes[Key]);
        if (!Result) return Status::Pending;
        if (!Result->Systems.at(Key).Valid) {PreflightRequests.erase(Key);PreflightScopes.erase(Key);return Status::Rejected;}
        Preflight.Systems[Key]=Result->Systems.at(Key);Completed.push_back(Key);
    }
    for (const auto& Key:Completed) {PreflightRequests.erase(Key);PreflightScopes.erase(Key);}
    return Stopping?Status::Rejected:Status::Ready;
}
ClientWorld::CaptureApplicationStatus AsyncClientWorld::State::CaptureBatch(std::span<const ObjectKey> Scope,bool CurrentOnly)
{
    // Private capture observes applied gameplay, never an unacknowledged candidate.
    if (!PendingGameplay.empty() || !CommandRequests.empty()) return ClientWorld::CaptureApplicationStatus::Pending;
    using Status=ClientWorld::CaptureApplicationStatus;
    const std::vector<ObjectKey> Keys(Scope.begin(),Scope.end());
    const auto Existing=CaptureBarriers.find(Keys);
    // A pending GT cut cannot use a revision comparison yet. Network retries
    // may revisit this same group thousands of times while its application is
    // queued; do not rebuild two ordered maps for every member on each retry.
    // Revalidate every revision after the actual application acknowledgment.
    if ((Existing!=CaptureBarriers.end() && !Existing->second->Ready)
        || (Existing==CaptureBarriers.end() && !CaptureBarriers.empty())) return Status::Pending;
    std::map<ObjectKey,std::pair<std::uint64_t,Tick>> Revisions,Context;
    for (const auto K:Keys)
    {
        Revisions[K]={ObjectRevisions[K],Core->ObjectNow(K)};
        if (const auto P=Proxies.find(K);P!=Proxies.end()) Context[K]={P->second->Info.Token,Core->ObjectNow(K)};
    }
    if (Existing!=CaptureBarriers.end())
    {
        auto Cut=Existing->second;
        CaptureBarriers.erase(Existing);
        CaptureAdvancePending=Options.YieldBetweenCaptureCuts;
        if (Cut->Revisions==Revisions&&Cut->CurrentOnly==CurrentOnly)
        {
            if (Cut->TooLarge) return Status::TooLarge;
            Captured=std::move(Cut->Value);
            for (const auto& [K,C]:Captured.Objects) if (!C.Good) return Status::Invalid;
            for (const auto& [N,C]:Captured.Systems) if (!C.Keys.empty()&&!C.Good) return Status::Invalid;
            return Status::Ready;
        }
        // Earlier engine applications generated inputs or committed values.
        // Their control acknowledgment is ordered after those inputs; take a fresh cut.
    }
    // Complete the current cut before advancing, but let queued simulation run
    // before acquiring another cut. Otherwise a chain of unrelated handoffs can
    // keep the world's clock fenced for many frames and deliver all steps at once.
    if (!CaptureBarriers.empty() || (CaptureAdvancePending && DeferredAdvanceSeconds>0)) return Status::Pending;
    auto Cut=std::make_shared<CaptureBarrier>();Cut->Keys=Keys;Cut->Revisions=std::move(Revisions);Cut->CurrentOnly=CurrentOnly;CaptureBarriers[Keys]=Cut;
    Publish();constexpr std::size_t Reservation=32ull*1024*1024;
    if (!Emit([this,Keys,Context,Cut]
    {
        TraceScope Scope("GuLiHybrid_MainCaptureBarrier");CaptureCache Result;std::map<std::string,std::vector<ObjectKey>> Shared;
        if (!GameplayGroups.empty())
        {
            Post([this,Keys](ClientWorld&){CaptureBarriers.erase(Keys);},128,true);
            // A control acknowledgment must not own output capacity that the
            // worker needs in order to reach that same acknowledgment.
            {std::lock_guard Lock(Mutex);OutputBytes-=Reservation;Stats.OutputBytes=OutputBytes+ResultReservedBytes;Wake.notify_all();}
            return;
        }
        std::size_t Size=1024;bool TooLarge=false;
        {
            TraceScope ObjectsScope("GuLiHybrid_MainCaptureObjects");
            for (const auto K:Keys)
            {
                const auto I=Context.find(K);if (I==Context.end()) continue;auto* A=Object(K,I->second.first);if (!A) continue;
                const auto Previous=AppliedContext;const auto PreviousTick=AppliedContextTick;AppliedContext=K;AppliedContextTick=I->second.second;
                auto& Out=Result.Objects[K];Out.Good=A->Capture(Out.Value);AppliedContext=Previous;AppliedContextTick=PreviousTick;
                Size+=Out.Value.size()+256;if (Size>MaxPrivateStateBytes) {TooLarge=true;break;}
                for (const auto& Name:A->RequiredSystems()) Shared[Name].push_back(K);
            }
        }
        if (!TooLarge)
        {
            TraceScope MembershipScope("GuLiHybrid_MainCaptureSystemMembership");
            for (const auto& [Name,A]:MainSystems) for (const auto K:Keys) if (A->UsesObject(K))
            {Result.Systems[Name].Uses.insert(K);if (std::find(Shared[Name].begin(),Shared[Name].end(),K)==Shared[Name].end()) Shared[Name].push_back(K);}
        }
        if (!TooLarge)
        {
            TraceScope SystemsScope("GuLiHybrid_MainCaptureSystems");
            for (const auto& [Name,Ks]:Shared)
            {
                auto& Out=Result.Systems[Name];Out.Keys=Ks;const auto A=MainSystems.find(Name);
                Out.Good=A!=MainSystems.end()&&(Cut->CurrentOnly?A->second->CaptureCurrent(Ks,Out.Value):A->second->Capture(Ks,Out.Value));
                Size+=Out.Value.size()+Ks.size()*128+256;if (Size>MaxPrivateStateBytes) {TooLarge=true;break;}
            }
        }
        if (TooLarge) Result={};
        Post([this,Cut,Result=std::move(Result),TooLarge](ClientWorld&) mutable
        {
            // The reusable result's lease starts when this ordered acknowledgment
            // is applied. A busy GT/input queue must not expire a valid cut before
            // its first consumer can observe it. Revisions are still checked.
            Cut->Value=std::move(Result);Cut->TooLarge=TooLarge;Cut->ReadyAt=Seconds();Cut->Ready=true;
            std::lock_guard Lock(Mutex);++Stats.ApplyAcks;
        },TooLarge?256:Size,true);
        // Ownership transfers to the independently budgeted input lane now,
        // not when the worker eventually executes the continuation.
        {std::lock_guard Lock(Mutex);OutputBytes-=Reservation;Stats.OutputBytes=OutputBytes+ResultReservedBytes;Wake.notify_all();}
    },Reservation,true))
    {CaptureBarriers.erase(Keys);return Status::Invalid;}
    return Status::Pending;
}
void AsyncClientWorld::State::AdvanceCaptures()
{
    for (auto I=CaptureRequests.begin();I!=CaptureRequests.end();)
    {
        auto& Request=I->second;CaptureResult Result;
        Result.Good=Core->CaptureObjects(Request.Keys,Request.Target,Request.Transfer,Result.Value);
        if (!Result.Good && Core->LastCapturePending() && Core->Error().empty()) {++I;continue;}
        if (Result.Good)
        {
            Result.Encoding=EncodeHandoffState(Result.Value);Message M;M.Header.Kind=MessageKind::HandoffState;M.Body=Result.Encoding;
            if (!Compress||!Compress(M,Result.Staged)) Result.Staged=Result.Encoding;
        }
        auto Size=Cost(Result.Value)+Result.Encoding.size()+Result.Staged.size()+256;
        // A public capture can be split by its existing caller. Do not let a large private cut consume other accepted results' credits.
        if (Size>QueueLimit/2 && ResultReservedBytes) {Result={};Size=256;}
        const auto Reply=Request.Result;Publish();
        Emit([Reply,Result=std::move(Result)]() mutable {Reply->Value=std::move(Result);Reply->Ready=true;},Size);
        I=CaptureRequests.erase(I);
    }
}
void AsyncClientWorld::State::AdvanceAfterCaptureBarrier(bool AfterPolling)
{
    // Keep the numeric clock at the cut while its preceding UE applications are
    // acknowledged. All capture consumers get one ordered poll after that ACK.
    // A completed cut left over after polling has no ready consumer (for example
    // its scope changed after a birth). It cannot lease the whole world's clock
    // for another 500 ms. A later request takes a new revision-checked cut.
    if (AfterPolling) for (auto I=CaptureBarriers.begin();I!=CaptureBarriers.end();)
        if (I->second->Ready) I=CaptureBarriers.erase(I);else ++I;
    // A real membership pause must not accrue production/cooldown catch-up time.
    if (Core->IsPaused()) {DeferredAdvanceSeconds=0;CaptureAdvancePending=false;return;}
    if (!CaptureBarriers.empty() || DeferredAdvanceSeconds<=0) return;
    // BattleClock may already hold a fractional tick. Leave one step of headroom
    // under its four-step accumulator cap; a full four-step slice would silently
    // discard that fraction on every catch-up batch.
    const auto Delta=std::min(DeferredAdvanceSeconds,3.0/TicksPerSecond);
    DeferredAdvanceSeconds=std::max(0.,DeferredAdvanceSeconds-Delta);
    const bool Yielded=CaptureAdvancePending;
    const auto Steps=Core->Advance(Delta);CaptureAdvancePending=false;Publish();
    Emit([this,Steps,Yielded]{std::lock_guard Lock(Mutex);Stats.SimulationSteps+=Steps;Stats.CaptureYieldAdvances+=Yielded;},128);
}
ClientWorld::ApplicationStatus AsyncClientWorld::State::RestoreBatch(const ObjectHandoffState& S,std::span<const ObjectKey> Ordered,
    const std::map<std::string,std::vector<ObjectKey>>& Shared,RestoreStage Stage)
{
    // Numeric state and transaction history were validated on the owner thread.
    // The four engine stages use only these private adapter pieces. Retaining
    // the complete numeric history in each GT closure both copies it repeatedly
    // and charges output capacity for data that no engine callback reads.
    ObjectHandoffState Private;Private.Version=S.Version;Private.Match=S.Match;
    Private.Source=S.Source;Private.Target=S.Target;Private.Transfer=S.Transfer;
    Private.CurrentOnly=S.CurrentOnly;Private.ResourceGroup=S.ResourceGroup;Private.ResourceEpoch=S.ResourceEpoch;
    Private.Objects=S.Objects;Private.Systems=S.Systems;
    const std::vector<ObjectKey> Keys(Ordered.begin(),Ordered.end());std::map<ObjectKey,std::uint64_t> Tokens;
    std::vector<std::pair<std::shared_ptr<std::atomic<std::uint64_t>>,std::uint64_t>> Generations;
    for (const auto& P:S.Objects)
    {
        auto& G=ApplicationGenerations[P.Record.Grant.Object];if (!G) G=std::make_shared<std::atomic<std::uint64_t>>(0);
        Generations.emplace_back(G,G->load());
    }
    for (const auto K:Keys) if (const auto P=Proxies.find(K);P!=Proxies.end()) Tokens[K]=P->second->Info.Token;
    const auto Key=Sha256(EncodeHandoffState(Private))+"/"+std::to_string(unsigned(Stage));
    // Shared systems can restore private state for non-materialized members
    // too. Their replies and cancellations must wake the same retained cut.
    for (const auto& P:S.Objects) RestoreScopes[Key].insert(P.Record.Grant.Object);
    const auto Bytes=Cost(Private)+Keys.size()*128+512;
    const auto* Result=RequestMain<RestoreCache>(RestoreRequests[Key],[this,S=std::move(Private),Keys,Tokens,Shared,Stage,Generations]
    {
        TraceScope Scope("GuLiHybrid_MainRestoreBatch");RestoreCache Result;Result.Stage=Stage;
        for (const auto& [G,V]:Generations) if (G->load()!=V) return Result;
        for (const auto K:Keys)
        {
            const auto P=std::find_if(S.Objects.begin(),S.Objects.end(),[&](const auto& V){return V.Record.Grant.Object==K;});
            const auto T=Tokens.find(K);auto* A=T==Tokens.end()?nullptr:Object(K,T->second);
            const auto Previous=AppliedContext;const auto PreviousTick=AppliedContextTick;AppliedContext=K;AppliedContextTick=P==S.Objects.end()?0:P->Record.LogicalAge;
            const bool Good=A&&P!=S.Objects.end()&&(S.Transfer?A->Restore(P->State,Stage):A->RestoreQuery(P->State,Stage));
            AppliedContext=Previous;AppliedContextTick=PreviousTick;Result.Objects[K]=Good;if (!Good) return Result;
            if (S.CurrentOnly&&Stage==RestoreStage::Link) A->ResetResourceRequests(S.ResourceEpoch);
        }
        for (const auto& [Name,Ks]:Shared)
        {
            const auto P=std::find_if(S.Systems.begin(),S.Systems.end(),[&](const auto& V){return V.Name==Name;});const auto A=MainSystems.find(Name);
            const bool Good=P!=S.Systems.end()&&A!=MainSystems.end()&&(S.Transfer?A->second->Restore({*P},Ks,Stage):A->second->RestoreQuery({*P},Ks,Stage));
            Result.Systems[Name]=Good;if (!Good) return Result;
            if (S.CurrentOnly&&Stage==RestoreStage::Link) A->second->ResetResourceRequests(Ks,S.ResourceEpoch);
        }
        return Result;
    },Bytes,&RestoreScopes[Key]);
    if (!Result) return ClientWorld::ApplicationStatus::Pending;
    Restored=*Result;RestoreRequests.erase(Key);RestoreScopes.erase(Key);
    if (Stopping || Restored.Objects.size()!=Keys.size() || Restored.Systems.size()!=Shared.size()) return ClientWorld::ApplicationStatus::Rejected;
    for (const auto& [K,Good]:Restored.Objects) if (!Good) return ClientWorld::ApplicationStatus::Rejected;
    for (const auto& [N,Good]:Restored.Systems) if (!Good) return ClientWorld::ApplicationStatus::Rejected;
    return ClientWorld::ApplicationStatus::Ready;
}
bool AsyncClientWorld::State::Boolean(const std::string& Name,std::function<bool(ClientWorld&)> Fn,std::size_t Bytes)
{
    CheckMain();OperationPending=false;auto I=BooleanOperations.find(Name);
    if (I==BooleanOperations.end())
    {
        auto R=std::make_shared<Reply<bool>>();
        if (!Post([this,R,Fn=std::move(Fn)](ClientWorld& C)
        {
            C.ResetApplicationPending();const bool Value=Fn(C);
            if (!Value && C.LastApplicationPending()) PendingBooleans.push_back({R,Fn});
            else {Publish();Emit([R,Value]{R->Value=Value;R->Ready=true;});}
        },Bytes)) return false;
        I=BooleanOperations.emplace(Name,Operation<bool>{R,false}).first;
    }
    if (!I->second.Result->Ready) {OperationPending=true;return false;}
    const bool Value=I->second.Result->Value;BooleanOperations.erase(I);return Value;
}

AsyncClientWorld::AsyncClientWorld(Id M,Id P,Revision E,Factory F,Send SendValue,Pack PackValue,bool Evidence,std::function<void()> Init,double Origin,AsyncExecutionOptions Options)
    :S(std::make_unique<State>(M,P,E,std::move(F),std::move(SendValue),std::move(PackValue),Evidence,std::move(Init),Origin,Options))
{S->Worker=std::thread([H=S.get()]{H->Run();});}
AsyncClientWorld::~AsyncClientWorld() {Stop();}
void AsyncClientWorld::Stop() {if (S) S->Shutdown();}
void AsyncClientWorld::PumpApplications() {S->Pump();}
void AsyncClientWorld::PumpCommandApplications(std::uint64_t WorldGeneration) {S->PumpCommands(WorldGeneration);}
bool AsyncClientWorld::ConfirmGameplay(std::shared_ptr<IGameplayCalculation> Confirmation,std::size_t Bytes)
{S->CheckMain();return Confirmation&&S->Post([Confirmation=std::move(Confirmation)](ClientWorld&){Confirmation->Compute();},Bytes,true);}
bool AsyncClientWorld::HasPendingGameplay(const std::string& Group) const
{S->CheckMain();return S->GameplayGroups.contains(Group);}
bool AsyncClientWorld::SubmitGameplay(std::string Group,std::shared_ptr<IGameplayCalculation> Work,
    std::size_t InputSize,std::size_t ResultSize,std::function<void()> Apply)
{
    S->CheckMain();
    if (!Work || !Apply || S->Stopping || S->GameplayGroups.contains(Group)
        || InputSize>QueueLimit-512 || ResultSize>QueueLimit/2-512) return false;
    InputSize+=512;ResultSize+=512;
    const auto Token=++S->NextToken;
    const auto Reservation=StableId("GuLiGameplayCalculation/v1",S->Match.ToString()+"/"+std::to_string(S->Generation)+"/"+std::to_string(Token));
    {
        std::lock_guard Lock(S->Mutex);
        if (S->GameplayInputBytes>QueueLimit-InputSize || S->OutputBytes+S->ResultReservedBytes>QueueLimit-ResultSize)
        {++S->Stats.ResultBackpressure;return false;}
        S->GameplayInputBytes+=InputSize;
        S->ResultReservations.emplace(Reservation,std::pair{Reservation,ResultSize});S->ResultReservedBytes+=ResultSize;
    }
    S->GameplayGroups[Group]=Token;S->GameplayApplications.emplace(Token,State::GameplayApplication{Group,std::move(Apply)});
    if (!S->Post([H=S.get(),Work=std::move(Work),Token,Reservation,InputSize,ResultSize](ClientWorld&) mutable
    {
        H->PendingGameplay.insert(Token);
        auto Job=std::make_shared<State::GameplayCompute>();Job->Work=std::move(Work);Job->Token=Token;
        Job->Reservation=Reservation;Job->InputSize=InputSize;Job->ResultSize=ResultSize;Job->Submitted=Seconds();
        H->ComputeOrder.push_back(Job);H->WaitingComputes.push_back(std::move(Job));
    },InputSize,false,true,AsyncInputKind::Gameplay))
    {
        {std::lock_guard Lock(S->Mutex);S->GameplayInputBytes-=InputSize;}
        S->GameplayGroups.erase(Group);S->GameplayApplications.erase(Token);S->ReleaseResult(Reservation);return false;
    }
    return true;
}
bool AsyncClientWorld::Configure(const RuntimeManifest& M,std::string& Error)
{
    Error.clear();const bool Queued=S->Post([H=S.get(),M](ClientWorld& C)
    {std::string Why;if (!C.Configure(M,Why)) C.Cancel("Runtime configuration failed: "+Why);},MaxPrivateStateBytes);
    if (!Queued) Error="Transaction input queue cannot accept configuration";return Queued;
}
void AsyncClientWorld::SetMembers(std::map<Id,Member> M)
{S->CheckMain();S->MainMembers=M;S->Post([M=std::move(M)](ClientWorld& C) mutable {C.SetMembers(std::move(M));},256,true);}
void AsyncClientWorld::SetResourceGroups(std::map<Id,ResourceGroupState> G)
{
    S->CheckMain();S->MainResourceGroups=G;
    std::vector<PreparedCommit> Interrupted;
    for (auto I=S->MainRequests.begin();I!=S->MainRequests.end();)
    {
        const auto Group=G.find(I->second.Header.ResourceGroup);
        if (Group==G.end()||Group->second.RequestEpoch<=I->second.Header.ResourceEpoch) {++I;continue;}
        PreparedCommit P;auto& V=P.Result;V.Event=I->first;V.Kind=I->second.Kind;V.Target=I->second.Target;
        V.StateDomain=I->second.Header.Object;V.Faction=I->second.Faction;V.ResourceGroup=Group->first;V.ResourceEpoch=Group->second.RequestEpoch;
        V.Status=CommitStatus::Interrupted;P.LocalRejection=true;P.LocalRejectionReason="Connection interrupted; restored snapshot is authoritative";
        P.Encoding=EncodeCommit(V);if (S->Detailed) {P.ResultHash=Sha256(P.Encoding);P.DecisionHash=Sha256(EncodeCommitDecision(V));}
        if (S->FirstInterruption(V)) Interrupted.push_back(std::move(P));I=S->MainRequests.erase(I);
    }
    for (const auto& P:Interrupted)
    {
        if (const auto Token=S->MainCallbackTokens.find(P.Result.Event);Token!=S->MainCallbackTokens.end())
        {
            const auto C=S->Callbacks.find(Token->second);S->MainCallbackTokens.erase(Token);
            if (C!=S->Callbacks.end()) {auto Fn=std::move(C->second);S->Callbacks.erase(C);Fn(P.Result);}
        }
        if (S->Committed) S->Committed(P);
    }
    const auto Size=G.size()*256+128;
    S->Post([G=std::move(G)](ClientWorld& C) mutable {C.SetResourceGroups(std::move(G));},Size,true);
}
const std::map<Id,ResourceGroupState>& AsyncClientWorld::ResourceGroups() const {return S->MainResourceGroups;}
bool AsyncClientWorld::StampObjectPublication(Envelope& H) const
{
    S->CheckMain();ObjectKey K=H.Object;std::set<ObjectKey> Seen;Id Group;
    while (K.Valid()&&Seen.size()<64&&Seen.insert(K).second)
    {
        const auto O=S->Directory.find(K);if (O==S->Directory.end()) break;
        if (O->second.Grant.OwnershipGroup.Valid()) {Group=O->second.Grant.OwnershipGroup;break;}
        if (O->second.Grant.Policy!=OwnershipPolicy::ParentWork) break;K=O->second.Parent;
    }
    const auto G=S->MainResourceGroups.find(Group);
    if (G==S->MainResourceGroups.end()) return !H.ResourceGroup.Valid()&&!H.ResourceEpoch;
    if (G->second.Recovery) return false;
    if (!H.ResourceGroup.Valid()&&!H.ResourceEpoch) {H.ResourceGroup=Group;H.ResourceEpoch=G->second.RequestEpoch;}
    return H.ResourceGroup==Group&&H.ResourceEpoch==G->second.RequestEpoch;
}
bool AsyncClientWorld::HasResourceRecovery() const
{return std::any_of(S->MainResourceGroups.begin(),S->MainResourceGroups.end(),[](const auto& P){return P.second.Recovery!=0;});}
bool AsyncClientWorld::IsCurrentResourceResult(const CommitResult& R) const {return S->CurrentResult(R);}
bool AsyncClientWorld::State::StampRequest(TransactionRequest& R) const
{
    Id Key;
    for (auto K:{Numeric.ExecutionDomain(R),R.Header.Object,R.Target,R.Related})
    {
        std::set<ObjectKey> Seen;
        while (K.Valid()&&Seen.size()<64&&Seen.insert(K).second)
        {const auto O=Directory.find(K);if (O==Directory.end()) break;if (O->second.Grant.OwnershipGroup.Valid()) {Key=O->second.Grant.OwnershipGroup;break;}K=O->second.Parent;}
        if (Key.Valid()) break;
    }
    if (!Key.Valid() && (R.Kind==TransactionKind::Credit||R.Kind==TransactionKind::Reserve||R.Kind==TransactionKind::Commit||R.Kind==TransactionKind::Refund
        ||R.Kind==TransactionKind::Spawn||R.Kind==TransactionKind::Destroy||R.Kind==TransactionKind::Capability||R.Kind==TransactionKind::Damage))
    {
        Team F=R.Faction;if (!Playable(F)) if (const auto O=Directory.find(R.Header.Object);O!=Directory.end()) F=O->second.Grant.Faction;
        if (Playable(F)) if (const auto Account=Directory.find(TeamAccountObject(F));Account!=Directory.end()) Key=Account->second.Grant.OwnershipGroup;
    }
    const auto G=MainResourceGroups.find(Key);
    if (G==MainResourceGroups.end()) return !R.Header.ResourceGroup.Valid()&&!R.Header.ResourceEpoch;
    if (G->second.Recovery) return false;
    if (!R.Header.ResourceEpoch&&!R.Header.ResourceGroup.Valid()) {R.Header.ResourceGroup=Key;R.Header.ResourceEpoch=G->second.RequestEpoch;}
    return R.Header.ResourceGroup==Key&&R.Header.ResourceEpoch==G->second.RequestEpoch;
}
Id AsyncClientWorld::ContinuationEvent(const ObjectRecord& O) const
{
    FrozenWorkRecipe R;if (!DecodeFrozenWork(O,R)) return {};
    auto Request=R.Request;Request.Header.ResourceGroup={};Request.Header.ResourceEpoch=0;
    if (!S->StampRequest(Request)) return {};
    return Request.Header.ResourceEpoch==R.Request.Header.ResourceEpoch?R.Request.Header.Event:
        StableId("GuLiResourceContinuation/v1",O.Grant.Object.StableId.ToString()+":"+std::to_string(Request.Header.ResourceEpoch));
}
bool AsyncClientWorld::Install(ObjectRecord R,bool Commit)
{const auto Size=Cost(R)+256;return S->Post([R=std::move(R),Commit](ClientWorld& C) mutable {C.Install(std::move(R),Commit);},Size);}
bool AsyncClientWorld::InstallGrant(const Authority& A,std::uint32_t Ordinal)
{return S->Post([A,Ordinal](ClientWorld& C){C.InstallGrant(A,Ordinal);},256,true);}
bool AsyncClientWorld::InstallDirectoryRecord(const Authority& A,const std::string& Definition,std::uint32_t Ordinal,ObjectKey Parent,bool Recover)
{
    return S->Post([H=S.get(),A,Definition,Ordinal,Parent,Recover](ClientWorld& C)
    {
        if (!C.InstallGrant(A,Ordinal)) {C.Cancel("Invalid ownership directory version");return;}
        const auto I=C.Objects().find(A.Object);if (I==C.Objects().end()) return;
        if (I->second.Definition!=Definition || I->second.Ordinal!=Ordinal)
        {C.Cancel("Object directory identity differs: "+A.Object.StableId.ToString());return;}
        if (!C.Install(I->second,true)) {C.Cancel("Unable to install ownership directory: "+Definition);return;}
        if (I->second.Grant.SimOwner==A.SimOwner && I->second.Grant.State>A.State)
        C.PublishDirectoryLife(A.Object,A.OwnerEpoch);
        if (Recover && C.HasSimulation(A.Object)) H->PendingRebuilds.insert(A.Object);
    },Definition.size()+512,true);
}
bool AsyncClientWorld::RegisterSystem(std::string Name,std::unique_ptr<ISharedStateAdapter> A)
{
    S->CheckMain();if (!A||!A->Version()||Name.empty()||S->MainSystems.contains(Name)) return false;
    const auto V=A->Version();S->MainSystems.emplace(Name,std::move(A));
    return S->Post([H=S.get(),Name,V](ClientWorld& C)
    {if (!C.RegisterSystem(Name,std::make_unique<State::SystemProxy>(*H,Name,V))) C.Cancel("Shared application registration failed: "+Name);},256,true);
}
bool AsyncClientWorld::CanReceivePeer(std::size_t BodyBytes) const
{std::lock_guard Lock(S->Mutex);return !S->Stopping && BodyBytes<=QueueLimit-256 && S->InputBytes<=QueueLimit-BodyBytes-256;}
bool AsyncClientWorld::ReceivePeer(Id Sender,const Message& M)
{return S->Post([Sender,M](ClientWorld& C){C.ReceivePeer(Sender,M);},M.Body.size()+256,false,false,
    MessageLane(M.Header.Kind)==Lane::Motion?AsyncInputKind::PeerMotion:AsyncInputKind::PeerBusiness);}
bool AsyncClientWorld::SendPeer(Id Peer,Message M)
{
    // External bootstrap messages have not passed through ClientWorld's send
    // path. Bind the same identity that the transport will use before packing;
    // its dictionary depends on these fields and cannot be rewritten afterward.
    M.Header.Match=S->Match;M.Header.Sender=S->Player;M.Header.Epoch=S->MatchEpoch;
    const auto Size=M.Body.size()+256;
    return S->Post([H=S.get(),Peer,M=std::move(M)](ClientWorld& C) mutable
        {C.StampPublication(M.Header);H->TransmitWorker(Peer,std::move(M));},Size,false,false,AsyncInputKind::PeerSend);
}
bool AsyncClientWorld::Route(const FrozenCommand& R)
{
    if (!CanPublishFacts()||!R.Event.Valid()||R.Members.empty()) return false;
    if (S->Options.CommandTiming) S->Options.CommandTiming("async-post",R.Event,R.Members.size());
    return S->Post([H=S.get(),R](ClientWorld& C)
    {
        if (H->Options.CommandTiming) H->Options.CommandTiming("worker-route",R.Event,R.Members.size());
        if (C.Route(R))
        {
            if (H->Options.CommandTiming) H->Options.CommandTiming("worker-routed",R.Event,R.Members.size());
            return;
        }
        CommandReceipt Receipt;Receipt.Event=R.Event;Receipt.At=C.Now();for (const auto& M:R.Members) Receipt.Members.push_back({M.Object,M.ControlEpoch,M.OwnerEpoch,false});
        H->Publish();H->Emit([H,Receipt]{H->Receipts[Receipt.Event]=Receipt;if (H->CommandCompleted) H->CommandCompleted(Receipt);},Receipt.Members.size()*128+256);
    },R.Arguments.size()+R.Members.size()*128+512,false,false,AsyncInputKind::LocalRequest);
}
Id AsyncClientWorld::Submit(TransactionRequest R,TransactionCallback Callback,bool PreserveOriginalTime)
{
    if (!S->StampRequest(R)) return {};
    TransactionRequest Tracked;Tracked.Header=R.Header;Tracked.Kind=R.Kind;Tracked.Target=R.Target;Tracked.Faction=R.Faction;
    S->CheckMain();if (!CanPublishFacts()||!R.Header.Event.Valid()||!HasSimulation(R.Header.Object,true)) return {};
    const auto& QueuedSource=S->Directory.at(R.Header.Object).Grant;
    R.Header.OwnerEpoch=QueuedSource.OwnerEpoch;R.Header.ControlEpoch=QueuedSource.ControlEpoch;
    const auto Event=R.Header.Event;const auto Size=Cost(R);const auto Token=Callback?++S->NextCallback:0;
    if (Token) S->Callbacks[Token]=std::move(Callback);
    const bool Queued=S->Post([H=S.get(),R=std::move(R),Token,PreserveOriginalTime](ClientWorld& C) mutable
    {
        H->RecordRequest(R);const auto Original=R;
        const auto CallbackValue=[H,Token](const CommitResult& Result)
        {
            if (!Token) return;H->Publish();H->Emit([H,Token,Result]
            {const auto I=H->Callbacks.find(Token);if (I!=H->Callbacks.end()) {auto Fn=std::move(I->second);H->Callbacks.erase(I);if (H->CurrentResult(Result)) Fn(Result);}},EncodeCommit(Result).size()*2+512);
        };
        // Timestamp the input at its actual engine simulation step, before the asynchronous handoff.
        const auto Source=C.Objects().find(Original.Header.Object);
        const bool Current=Source!=C.Objects().end() && Source->second.Grant.OwnerEpoch==Original.Header.OwnerEpoch
            && Source->second.Grant.ControlEpoch==Original.Header.ControlEpoch;
        const auto Accepted=Current?C.Submit(std::move(R),CallbackValue,true):Id{};
        if (!Accepted.Valid())
        {
            if (C.IsRequestPending(Original.Header.Event))
            {if (Token) H->Emit([H,Token]{H->Callbacks.erase(Token);});return;}
            CommitResult Result;Result.Event=Original.Header.Event;Result.Kind=Original.Kind;Result.StateDomain=Original.Header.Object;Result.Target=Original.Target;
            Result.Faction=Original.Faction;Result.ResourceGroup=Original.Header.ResourceGroup;Result.ResourceEpoch=Original.Header.ResourceEpoch;
            Result.Status=CommitStatus::BadRequest;CallbackValue(Result);
            H->Deliver(Result,true,Current?"Local source no longer admits this request":"Source ownership or control version changed before admission");
        }
        (void)PreserveOriginalTime;
    },Size,false,false,AsyncInputKind::LocalRequest);
    if (!Queued) {S->Callbacks.erase(Token);return {};}
    S->MainRequests.try_emplace(Event,std::move(Tracked));if (Token) S->MainCallbackTokens[Event]=Token;return Event;
}
Id AsyncClientWorld::SubmitAdmitted(TransactionRequest R)
{
    if (!S->StampRequest(R)) return {};
    TransactionRequest Tracked;Tracked.Header=R.Header;Tracked.Kind=R.Kind;Tracked.Target=R.Target;Tracked.Faction=R.Faction;
    if (!CanPublishFacts()||!HasSimulation(R.Header.Object,true)||!R.Header.Event.Valid()) return {};
    const auto& QueuedSource=S->Directory.at(R.Header.Object).Grant;
    R.Header.OwnerEpoch=QueuedSource.OwnerEpoch;R.Header.ControlEpoch=QueuedSource.ControlEpoch;
    const auto E=R.Header.Event;const auto Size=Cost(R);
    if (!S->Post([H=S.get(),R=std::move(R)](ClientWorld& C) mutable
    {
        H->RecordRequest(R);const auto Original=R;const auto Source=C.Objects().find(Original.Header.Object);
        const bool Current=Source!=C.Objects().end() && Source->second.Grant.OwnerEpoch==Original.Header.OwnerEpoch
            && Source->second.Grant.ControlEpoch==Original.Header.ControlEpoch;
        if (!(Current && C.SubmitAdmitted(std::move(R),true).Valid()) && !C.IsRequestPending(StableId("GuLiTransactionAdmission/v1",Original.Header.Event.ToString())))
        {CommitResult V;V.Event=Original.Header.Event;V.Kind=Original.Kind;V.StateDomain=Original.Header.Object;V.Target=Original.Target;V.Faction=Original.Faction;V.Status=CommitStatus::BadRequest;
            V.ResourceGroup=Original.Header.ResourceGroup;V.ResourceEpoch=Original.Header.ResourceEpoch;
            H->Deliver(V,true,Current?"Local source no longer admits this work":"Source ownership or control version changed before work admission");}
    },Size,false,false,AsyncInputKind::LocalRequest)) return {};S->MainRequests.try_emplace(E,std::move(Tracked));return E;
}
Id AsyncClientWorld::SubmitControlled(TransactionRequest R,bool Admit)
{
    if (!S->StampRequest(R)) return {};
    TransactionRequest Tracked;Tracked.Header=R.Header;Tracked.Kind=R.Kind;Tracked.Target=R.Target;Tracked.Faction=R.Faction;
    if (!CanPublishFacts()||!HasControl(R.Header.Object)||!R.Header.Event.Valid()) return {};
    R.Header.ControlEpoch=S->Directory.at(R.Header.Object).Grant.ControlEpoch;
    const auto E=R.Header.Event;const auto Size=Cost(R);const bool Preserve=HasSimulation(R.Header.Object);
    if (!S->Post([H=S.get(),R=std::move(R),Admit,Preserve](ClientWorld& C) mutable
    {
        H->RecordRequest(R);const auto Original=R;const auto Source=C.Objects().find(Original.Header.Object);
        const bool Current=Source!=C.Objects().end() && Source->second.Grant.ControlEpoch==Original.Header.ControlEpoch;
        if (!(Current && C.SubmitControlled(std::move(R),Admit,Preserve).Valid()) && !C.IsRequestPending(Admit?StableId("GuLiTransactionAdmission/v1",Original.Header.Event.ToString()):Original.Header.Event))
        {CommitResult V;V.Event=Original.Header.Event;V.Kind=Original.Kind;V.StateDomain=Original.Header.Object;V.Target=Original.Target;V.Faction=Original.Faction;V.Status=CommitStatus::BadRequest;
            V.ResourceGroup=Original.Header.ResourceGroup;V.ResourceEpoch=Original.Header.ResourceEpoch;
            H->Deliver(V,true,Current?"Controlled request was not admitted":"Control version changed before request admission");}
    },Size,false,false,AsyncInputKind::LocalRequest)) return {};S->MainRequests.try_emplace(E,std::move(Tracked));return E;
}
bool AsyncClientWorld::PublishObjectEvent(ObjectKey K,Id E,Bytes B)
{const auto Size=B.size()+256;return S->Post([K,E,B=std::move(B)](ClientWorld& C) mutable {C.PublishObjectEvent(K,E,std::move(B));},Size,false,false,AsyncInputKind::LocalEvent);}
bool AsyncClientWorld::CommitHandoffs(std::span<const std::pair<ObjectHandoff,Authority>> V)
{
    const std::vector<std::pair<ObjectHandoff,Authority>> Values(V.begin(),V.end());
    return S->Post([H=S.get(),Values](ClientWorld& C)
    {
        const bool Good=C.CommitHandoffs(Values);H->MetaDirty=true;H->Publish();
        H->Emit([H,Values,Good]{if (H->OwnershipCommitted) H->OwnershipCommitted(Values,Good);},Values.size()*512+256);
    },Values.size()*512+256);
}
bool AsyncClientWorld::PrepareHandoff(const ObjectHandoff& T) {return S->Post([T](ClientWorld& C){C.PrepareHandoff(T);},256,true);}
bool AsyncClientWorld::FreezeHandoff(const ObjectHandoff& T) {return S->Post([T](ClientWorld& C){C.FreezeHandoff(T);},256,true);}
void AsyncClientWorld::CancelHandoff(const ObjectHandoff& T) {S->Post([T](ClientWorld& C){C.CancelHandoff(T);},256,true);}
void AsyncClientWorld::ReconcileHandoffs(std::span<const ObjectHandoff> V)
{const std::vector<ObjectHandoff> Values(V.begin(),V.end());S->Post([Values](ClientWorld& C){C.ReconcileHandoffs(Values);},Values.size()*256+256,true);}
bool AsyncClientWorld::CaptureObjects(std::span<const ObjectKey> K,Id Target,Revision Transfer,ObjectHandoffState& Out) const
{
    S->CheckMain();S->OperationPending=false;std::string Name=Target.ToString()+"/"+std::to_string(Transfer);
    for (const auto& Key:K) Name+="/"+std::to_string(unsigned(Key.Kind))+"/"+Key.StableId.ToString()+"/"+std::to_string(Key.Generation);
    auto I=S->Captures.find(Name);
    if (I==S->Captures.end())
    {
        const std::vector<ObjectKey> Keys(K.begin(),K.end());auto R=std::make_shared<State::Reply<State::CaptureResult>>();
        if (!S->Post([H=S.get(),R,Keys,Target,Transfer,Name](ClientWorld&)
        {H->CaptureRequests[Name]={R,Keys,Target,Transfer};},Keys.size()*128+256)) return false;
        I=S->Captures.emplace(Name,State::Operation<State::CaptureResult>{R,false}).first;
    }
    if (!I->second.Result->Ready) {S->OperationPending=true;return false;}
    auto Result=std::move(I->second.Result->Value);S->Captures.erase(I);if (!Result.Good) return false;
    Out=std::move(Result.Value);S->LastCaptureEncoding=std::move(Result.Encoding);S->LastCaptureStaged=std::move(Result.Staged);return true;
}
bool AsyncClientWorld::StageBootstrapDirectory(std::span<const ObjectHandoffState> V)
{
    if (S->BooleanOperations.contains("bootstrap/directory")) return S->Boolean("bootstrap/directory",[](ClientWorld&){return false;});
    auto Parts=std::vector<ObjectHandoffState>(V.begin(),V.end());std::size_t Bytes=512;for (const auto& P:Parts) Bytes+=Cost(P);
    return S->Boolean("bootstrap/directory",[Parts=std::move(Parts)](ClientWorld& C){return C.StageBootstrapDirectory(Parts);},Bytes);
}
bool AsyncClientWorld::ImportBootstrap(std::span<const ObjectHandoffState> V,ClientWorld::BootstrapNumericSource Source)
{
    if (S->BooleanOperations.contains("bootstrap/import")) return S->Boolean("bootstrap/import",[](ClientWorld&){return false;});
    auto Parts=std::vector<ObjectHandoffState>(V.begin(),V.end());std::size_t Bytes=512;for (const auto& P:Parts) Bytes+=Cost(P);
    return S->Boolean("bootstrap/import",[Parts=std::move(Parts),Source=std::move(Source)](ClientWorld& C){return C.ImportBootstrap(Parts,Source);},Bytes);
}
bool AsyncClientWorld::SetBootstrapMaterializationDeferred(bool Value)
{
    if (Value) return S->Post([](ClientWorld& C){C.SetBootstrapMaterializationDeferred(true);},128,true);
    if (S->Preparing) {S->OperationPending=true;return false;}
    return S->Boolean("bootstrap/materialized",[](ClientWorld& C){return C.SetBootstrapMaterializationDeferred(false);});
}
void AsyncClientWorld::FinishBootstrap() {S->Post([](ClientWorld& C){C.FinishBootstrap();},128,true);}
bool AsyncClientWorld::RebuildFromReplica(ObjectKey K) {return S->Post([H=S.get(),K](ClientWorld&){H->PendingRebuilds.insert(K);},256);}
void AsyncClientWorld::SetMembershipPaused(bool V) {S->Post([V](ClientWorld& C){C.SetMembershipPaused(V);},128,true);}
void AsyncClientWorld::PollNetwork(double) {PumpApplications();}
std::uint32_t AsyncClientWorld::Advance(double Delta)
{
    if (!std::isfinite(Delta)||Delta<0) return 0;
    S->Post([H=S.get(),Delta](ClientWorld&)
    {H->DeferredAdvanceSeconds+=Delta;H->AdvanceAfterCaptureBarrier();},128,true,false,AsyncInputKind::Simulation);return 0;
}
void AsyncClientWorld::Cancel(std::string Why)
{S->CheckMain();if (S->Failure.empty()) S->Failure=Why;S->Post([Why=std::move(Why)](ClientWorld& C){C.Cancel(Why);},256,true);}
void AsyncClientWorld::SetRecordObserver(std::function<void(const ObjectRecord&)> Fn) {S->CheckMain();S->RecordObserver=std::move(Fn);}
void AsyncClientWorld::SetQueryObserver(std::function<void(const ClientQueryDelta&)> Fn) {S->CheckMain();S->QueryObserver=std::move(Fn);}
void AsyncClientWorld::SetCommitCallback(CommitCallback Fn) {S->CheckMain();S->Committed=std::move(Fn);}
void AsyncClientWorld::SetCommandReceiptCallback(std::function<void(const CommandReceipt&)> Fn) {S->CheckMain();S->CommandCompleted=std::move(Fn);}
void AsyncClientWorld::SetOwnershipCommitCallback(std::function<void(const std::vector<std::pair<ObjectHandoff,Authority>>&,bool)> Fn)
{S->CheckMain();S->OwnershipCommitted=std::move(Fn);}
void AsyncClientWorld::SetTimingObserver(std::function<void(const char*,double)> Fn) {S->CheckMain();S->TimingObserver=std::move(Fn);}
void AsyncClientWorld::SetHandoffAdmission(std::function<bool(Id)> Fn) {S->CheckMain();S->HandoffAdmission=std::move(Fn);}
bool AsyncClientWorld::IsOperationPending() const {return S->OperationPending;}
const Bytes& AsyncClientWorld::CapturedEncoding() const {return S->LastCaptureEncoding;}
const Bytes& AsyncClientWorld::CapturedStagedEncoding() const {return S->LastCaptureStaged;}
bool AsyncClientWorld::Inspect(const std::string& Name,std::function<std::string(ClientWorld&)> Fn,std::string& Out)
{
    S->CheckMain();S->OperationPending=false;auto I=S->Inspections.find(Name);
    if (I==S->Inspections.end())
    {
        auto R=std::make_shared<State::Reply<std::string>>();
        if (!S->Post([H=S.get(),R,Fn=std::move(Fn)](ClientWorld& C)
        {auto V=Fn(C);H->Publish();const auto Size=V.size()+128;H->Emit([R,V=std::move(V)]() mutable {R->Value=std::move(V);R->Ready=true;},Size);},512,false,false,AsyncInputKind::Inspection)) return false;
        I=S->Inspections.emplace(Name,State::Operation<std::string>{R,false}).first;
    }
    if (!I->second.Result->Ready) {S->OperationPending=true;return false;}Out=std::move(I->second.Result->Value);S->Inspections.erase(I);return true;
}

bool AsyncClientWorld::HasSimulation(ObjectKey K,bool Dead) const
{
    const auto I=S->Directory.find(K);if (!S->Failure.empty()||S->OwnershipApplying||S->Frozen.contains(K)||I==S->Directory.end()) return false;const auto& A=I->second.Grant;
    return !A.Dormant&&A.SimOwner==S->Player&&A.OwnerEpoch&&(A.Policy!=OwnershipPolicy::ControllerPinned||A.Controller==S->Player)
        &&(A.State==Life::Alive||(Dead&&A.State==Life::Dead));
}
bool AsyncClientWorld::HasControl(ObjectKey K) const
{const auto I=S->Directory.find(K);return I!=S->Directory.end()&&I->second.Grant.Controller==S->Player&&I->second.Grant.ControlEpoch&&I->second.Grant.State==Life::Alive&&!I->second.Grant.Dormant;}
bool AsyncClientWorld::IsPaused() const {return S->Paused;}
bool AsyncClientWorld::CanPublishFacts() const {return S->Failure.empty()&&!S->Paused;}
bool AsyncClientWorld::IsBootstrapMaterializationDeferred() const {return S->Deferred;}
Tick AsyncClientWorld::Now() const {return S->SharedContextTick.value_or(std::max(S->PublishedClock,S->AppliedWorldTick));}
Tick AsyncClientWorld::ObjectNow(ObjectKey K) const {if (S->AppliedContext==K) return S->AppliedContextTick;const auto I=S->Directory.find(K);return I==S->Directory.end()?0:I->second.LogicalAge;}
Tick AsyncClientWorld::ContextNow() const {return S->AppliedContext.Valid()?S->AppliedContextTick:Now();}
Revision AsyncClientWorld::Epoch() const {return S->MatchEpoch;}
const std::string& AsyncClientWorld::Error() const {return S->Failure;}
const std::string& AsyncClientWorld::DirectoryHash() const
{
    S->CheckMain();
    if (S->HashDirty)
    {
        // This digest is an explicit review query, not a gameplay dependency.
        // Hash the exact applied directory only when requested. Eagerly hashing
        // every birth/retirement readback makes combat cost grow with history.
        TraceScope Scope("GuLiHybrid_ReviewDirectoryHash");Writer W(MaxPrivateStateBytes);
        for (const auto& [K,O]:S->Directory) WriteAuthority(W,O.Grant);
        S->Hash=W.Good()?Sha256(W.Take()):std::string{};S->HashDirty=false;
    }
    return S->Hash;
}
const std::map<ObjectKey,ObjectRecord>& AsyncClientWorld::Objects() const {return S->Directory;}
const ObjectRecord* AsyncClientWorld::FindObject(ObjectKey K,ObjectReadCursor& Cursor) const
{
    S->CheckMain();
    if (Cursor.Generation==S->Generation && Cursor.Key==K && Cursor.Record) return Cursor.Record;
    Cursor.Generation=S->Generation;Cursor.Key=K;
    const auto I=S->Directory.find(K);
    Cursor.Record=I==S->Directory.end()?nullptr:&I->second;
    // ApplyDelta assigns records in place; retirement retains a tombstone.
    // A different client generation invalidates the address before dereference.
    return Cursor.Record;
}
const ObjectRecord* AsyncClientWorld::FindTeamCapability(Team T,const std::string& Name) const
{const auto I=S->CapabilityIndex.find({T,Name});if (I==S->CapabilityIndex.end()) return nullptr;for (const auto K:I->second) {const auto O=S->Directory.find(K);if (O!=S->Directory.end()) return &O->second;}return nullptr;}
const std::map<std::string,std::size_t>& AsyncClientWorld::CommanderDefinitions() const {return S->Definitions;}
const ClientNumericView& AsyncClientWorld::NumericState() const {return S->Numeric;}
const std::map<ObjectKey,ObjectHandoff>& AsyncClientWorld::ActiveHandoffs() const {return S->Handoffs;}
const std::map<ObjectKey,MotionReceiveStats>& AsyncClientWorld::MotionDiagnostics() const {return S->Motion;}
std::uint64_t AsyncClientWorld::RejectedMotionLives() const {return S->UnknownLives;}
std::uint64_t AsyncClientWorld::RejectedStateImports() const {return S->ImportRejected;}
const std::string& AsyncClientWorld::LastStateImportFailureReason() const {return S->ImportFailure;}
const std::vector<ReceiveRejection>& AsyncClientWorld::FirstReceiveRejections() const {return S->FirstRejections;}
const std::vector<ReceiveRejection>& AsyncClientWorld::RecentReceiveRejections() const {return S->RecentRejections;}
const std::map<std::string,std::uint64_t>& AsyncClientWorld::HandoffDiagnostics() const {return S->HandoffIssues;}
const std::map<std::string,std::uint64_t>& AsyncClientWorld::HandoffMeasurements() const {return S->HandoffSizes;}
const std::map<Id,ResourceSnapshotStatus>& AsyncClientWorld::ResourceSnapshotStatuses() const {return S->ResourceStatuses;}
std::uint64_t AsyncClientWorld::DuplicateResults() const {return S->DuplicateCount;}
std::size_t AsyncClientWorld::ExecutedCommandCount() const {std::size_t N=0;for (const auto& [K,V]:S->CommandCounts) N+=V;return N;}
std::size_t AsyncClientWorld::ExecutedCommandCount(ObjectKind K) const {const auto I=S->CommandCounts.find(K);return I==S->CommandCounts.end()?0:I->second;}
std::optional<CommandReceipt> AsyncClientWorld::CommandResult(Id E) const {const auto I=S->Receipts.find(E);return I==S->Receipts.end()?std::nullopt:std::optional(I->second);}
std::vector<TransactionRequest> AsyncClientWorld::PendingTransactions() const {return S->Pending;}
std::size_t AsyncClientWorld::PendingTransactionCount() const {return S->Pending.size();}
std::map<std::string,std::size_t> AsyncClientWorld::WorkCounts() const {return S->Counts;}
std::vector<WorkDiagnostic> AsyncClientWorld::WorkDiagnostics() const {return S->Diagnostics;}
const std::deque<CommitResult>& AsyncClientWorld::UnsettledReceipts() const {return S->Unsettled;}
const std::deque<PreparedCommit>& AsyncClientWorld::UnsettledEvidence() const {return S->UnsettledPrepared;}
std::uint64_t AsyncClientWorld::UnsettledReceiptCount() const {return S->UnsettledCount;}
AsyncClientStats AsyncClientWorld::Statistics() const
{
    std::lock_guard Lock(S->Mutex);auto Result=S->Stats;Result.GameplayAdmittedBytes=S->GameplayInputBytes;
    const auto Now=Seconds();
    if (!S->Inputs.empty()) Result.OldestInputMilliseconds=(Now-S->Inputs.front().Enqueued)*1000.;
    if (!S->Controls.empty()) Result.OldestControlMilliseconds=(Now-S->Controls.front().Enqueued)*1000.;
    return Result;
}
}
