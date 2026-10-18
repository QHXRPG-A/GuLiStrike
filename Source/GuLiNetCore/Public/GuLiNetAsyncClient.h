#pragma once
#include "GuLiNetClient.h"
#include "GuLiNetGameplayPool.h"
#include <chrono>

namespace GuLiNet
{
/** Immutable ledger values and incrementally applied metadata; no trial/commit API. */
struct GULINETCORE_API ClientNumericView
{
    template<class T> struct ValueView {std::shared_ptr<const T> Value=std::make_shared<T>();const T& Snapshot() const {return *Value;}};
    struct Balances {Resources Red,Blue;Resources Balance(Team T) const {return T==Team::Red?Red:T==Team::Blue?Blue:Resources{};}};
    const ValueView<ResourceLedgerState>& WorldResources() const {return ResourcesView;}
    const ValueView<MiningReservationDirectory>& MiningPositions() const {return MiningView;}
    const Balances& TeamEconomy() const {return Accounts;}
    Revision Version(const StateKey& K) const;
    std::uint32_t LastRetiredLife(ObjectKind Kind,Id Stable) const;
    bool Busy(ObjectKey K) const {return BusyObjects.contains(K);}
    Id Owner(ObjectKey K) const;
    ObjectKey ExecutionDomain(const TransactionRequest& Request) const;
    ValueView<ResourceLedgerState> ResourcesView;
    ValueView<MiningReservationDirectory> MiningView;
    Balances Accounts;
    Revision ResourceDirectoryRevision=0,MiningDirectoryRevision=0;
    std::map<StateKey,Revision> Versions;
    std::map<std::pair<ObjectKind,Id>,std::uint32_t> Retired;
    std::set<ObjectKey> BusyObjects;
    const std::map<ObjectKey,ObjectRecord>* Directory=nullptr;
};
enum class AsyncInputKind : std::uint8_t
{ General,Control,PeerMotion,PeerBusiness,LocalRequest,LocalEvent,PeerSend,Simulation,Gameplay,Inspection,Count };
inline constexpr const char* AsyncInputNames[]={"general","control","peer_motion","peer_business","local_request","local_event","peer_send","simulation","gameplay","inspection"};
struct AsyncInputStats
{
    std::uint64_t Accepted=0,Processed=0,Rejected=0,AcceptedBytes=0,ProcessedBytes=0,QueuedBytes=0,QueuedCount=0;
    double WorkMilliseconds=0,WaitMilliseconds=0,MaximumWaitMilliseconds=0;
};
struct AsyncClientStats
{
    std::uint64_t InputBytes=0,OutputBytes=0,InputPeak=0,OutputPeak=0,InputRejected=0;
    std::uint64_t ControlBytes=0,ControlPeak=0;
    std::uint64_t ResultReservedBytes=0,ResultBackpressure=0;
    std::uint64_t Inputs=0,Applications=0,ApplyAcks=0,ReadDeltas=0;
    double LastQueueMilliseconds=0,MaximumQueueMilliseconds=0;
    double WorkerMilliseconds=0,MainApplyMilliseconds=0,SimulationMilliseconds=0;
    std::uint64_t SimulationSteps=0;
    std::array<AsyncInputStats,static_cast<std::size_t>(AsyncInputKind::Count)> InputKinds{};
    std::uint64_t GameplayAdmittedBytes=0,QueuedInputBytes=0;
    double OldestInputMilliseconds=0,OldestControlMilliseconds=0;
    // Bounded aggregate diagnostics, never a per-event history.
    std::map<std::string,std::uint64_t> RequestRules;
    std::map<std::uint16_t,std::uint64_t> ReceiptKinds;
};
struct PreparedCommit
{
    CommitResult Result;
    Bytes Encoding;
    std::string ResultHash,DecisionHash;
    bool LocalRejection=false;
    std::string LocalRejectionReason;
};

/** Local execution policy; never included in a manifest, handshake or saved state. */
struct AsyncExecutionOptions
{
    bool GameplayWorker=false;
    bool ReducedWork=false;
    bool IncrementalDelivery=false;
    std::shared_ptr<IGameplayTaskExecutor> GameplayExecutor;
};

/** Changed query fields, delivered after the complete read mirror and before callbacks. */
struct ClientQueryDelta
{
    bool ResourceSeed=false,OwnersChanged=false,MiningChanged=false;
    std::map<std::uint32_t,std::uint8_t> Remaining;
    std::map<ObjectKey,MiningPositionLease> Leases;
    std::set<ObjectKey> RemovedLeases;
};

/** Owned plain data only. Compute runs on the transaction owner or its finite compute pool.
 * Implementations must not retain/dereference scene objects, adapters or world containers.
 */
class GULINETCORE_API IGameplayCalculation
{
public:
    virtual ~IGameplayCalculation()=default;
    virtual void Compute()=0;
    // Stateful codecs share baselines with network feedback on the owner.
    // Only isolated snapshots explicitly opt into the shared compute pool.
    virtual bool CanRunParallel() const {return false;}
};

/** Game-thread facade. One ordered worker exclusively owns ClientWorld and OwnerState.
 * Object/system adapters live in the caller's registry. Worker proxies submit
 * data-only application requests; only PumpApplications invokes those adapters.
 */
class GULINETCORE_API AsyncClientWorld final
{
public:
    using Factory=ClientWorld::Factory;
    using TransactionCallback=ClientWorld::TransactionCallback;
    using Send=std::function<bool(Id,Message,Bytes,bool)>;
    using Pack=std::function<bool(Message&,Bytes&)>;
    using CommitCallback=std::function<void(const PreparedCommit&)>;
    AsyncClientWorld(Id Match,Id Player,Revision Epoch,Factory Create,Send Transmit,Pack Compress={},bool Evidence=false,
        std::function<void()> InitializeWorker={},double MonotonicOrigin=0,AsyncExecutionOptions Options={});
    ~AsyncClientWorld();
    AsyncClientWorld(const AsyncClientWorld&)=delete;
    AsyncClientWorld& operator=(const AsyncClientWorld&)=delete;
    void PumpApplications();
    bool SubmitGameplay(std::string DependencyGroup,std::shared_ptr<IGameplayCalculation>,
        std::size_t InputBytes,std::size_t MaximumResultBytes,std::function<void()> Apply);
    bool HasPendingGameplay(const std::string& DependencyGroup) const;
    bool ConfirmGameplay(std::shared_ptr<IGameplayCalculation> Confirmation,std::size_t Bytes);
    void Stop();
    bool Configure(const RuntimeManifest&,std::string& Error);
    void SetMembers(std::map<Id,Member>);
    bool Install(ObjectRecord,bool Commit);
    bool InstallGrant(const Authority&,std::uint32_t Ordinal=0);
    bool CanReceivePeer(std::size_t BodyBytes) const;
    bool InstallDirectoryRecord(const Authority&,const std::string& Definition,std::uint32_t Ordinal,ObjectKey Parent,bool Recover);
    bool RegisterSystem(std::string,std::unique_ptr<ISharedStateAdapter>);
    bool ReceivePeer(Id,const Message&);
    bool SendPeer(Id,Message);
    bool Route(const FrozenCommand&);
    Id Submit(TransactionRequest,TransactionCallback,bool PreserveOriginalTime=false);
    Id SubmitAdmitted(TransactionRequest);
    Id SubmitControlled(TransactionRequest,bool Admit);
    bool PublishObjectEvent(ObjectKey,Id,Bytes);
    bool CommitHandoffs(std::span<const std::pair<ObjectHandoff,Authority>>);
    bool PrepareHandoff(const ObjectHandoff&);
    bool FreezeHandoff(const ObjectHandoff&);
    void CancelHandoff(const ObjectHandoff&);
    void ReconcileHandoffs(std::span<const ObjectHandoff>);
    bool CaptureObjects(std::span<const ObjectKey>,Id,Revision,ObjectHandoffState&) const;
    bool StageBootstrapDirectory(std::span<const ObjectHandoffState>);
    bool ImportBootstrap(std::span<const ObjectHandoffState>,ClientWorld::BootstrapNumericSource);
    bool SetBootstrapMaterializationDeferred(bool);
    void FinishBootstrap();
    bool RebuildFromReplica(ObjectKey);
    void SetMembershipPaused(bool);
    void PollNetwork(double);
    std::uint32_t Advance(double);
    void Cancel(std::string);
    void SetRecordObserver(std::function<void(const ObjectRecord&)>);
    void SetQueryObserver(std::function<void(const ClientQueryDelta&)>);
    void SetCommitCallback(CommitCallback);
    void SetCommandReceiptCallback(std::function<void(const CommandReceipt&)>);
    void SetOwnershipCommitCallback(std::function<void(const std::vector<std::pair<ObjectHandoff,Authority>>&,bool)>);
    void SetTimingObserver(std::function<void(const char*,double)>);
    void SetHandoffAdmission(std::function<bool(Id)>);
    bool IsOperationPending() const;
    const Bytes& CapturedEncoding() const;
    const Bytes& CapturedStagedEncoding() const;
    bool Inspect(const std::string& Key,std::function<std::string(ClientWorld&)>,std::string& Out);

    bool HasSimulation(ObjectKey,bool Dead=false) const;
    bool HasControl(ObjectKey) const;
    bool IsPaused() const;
    bool CanPublishFacts() const;
    bool IsBootstrapMaterializationDeferred() const;
    Tick Now() const;
    Tick ObjectNow(ObjectKey) const;
    Tick ContextNow() const;
    Revision Epoch() const;
    const std::string& Error() const;
    const std::string& DirectoryHash() const;
    const std::map<ObjectKey,ObjectRecord>& Objects() const;
    const ObjectRecord* FindTeamCapability(Team,const std::string&) const;
    const std::map<std::string,std::size_t>& CommanderDefinitions() const;
    const ClientNumericView& NumericState() const;
    const std::map<ObjectKey,ObjectHandoff>& ActiveHandoffs() const;
    const std::map<ObjectKey,MotionReceiveStats>& MotionDiagnostics() const;
    std::uint64_t RejectedMotionLives() const;
    std::uint64_t RejectedStateImports() const;
    const std::string& LastStateImportFailureReason() const;
    const std::vector<ReceiveRejection>& FirstReceiveRejections() const;
    const std::vector<ReceiveRejection>& RecentReceiveRejections() const;
    const std::map<std::string,std::uint64_t>& HandoffDiagnostics() const;
    const std::map<std::string,std::uint64_t>& HandoffMeasurements() const;
    std::uint64_t DuplicateResults() const;
    std::size_t ExecutedCommandCount() const;
    std::size_t ExecutedCommandCount(ObjectKind) const;
    std::optional<CommandReceipt> CommandResult(Id) const;
    std::vector<TransactionRequest> PendingTransactions() const;
    std::size_t PendingTransactionCount() const;
    std::map<std::string,std::size_t> WorkCounts() const;
    std::vector<WorkDiagnostic> WorkDiagnostics() const;
    const std::deque<CommitResult>& UnsettledReceipts() const;
    const std::deque<PreparedCommit>& UnsettledEvidence() const;
    std::uint64_t UnsettledReceiptCount() const;
    AsyncClientStats Statistics() const;
private:
    struct State;
    std::unique_ptr<State> S;
};
}
