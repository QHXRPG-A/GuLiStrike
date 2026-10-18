#pragma once
#include "GuLiNetOwnerState.h"
#include "GuLiNetOwnership.h"
#include <functional>
#include <memory>

namespace GuLiNet
{
enum class RestoreStage : std::uint8_t { Detach, Create, Link, Activate };
struct PrivateSystemState
{
    std::string Name;
    Id Player;
    std::uint32_t AdapterVersion = 0;
    std::vector<ObjectKey> Objects;
    Bytes State;
};
// Shared task fragments contain only the requested object subset. Import must
// preserve tasks belonging to other locally simulated objects.
class GULINETCORE_API ISharedStateAdapter
{
public:
    virtual ~ISharedStateAdapter() = default;
    virtual std::uint32_t Version() const = 0;
    // Auxiliary state follows only objects claimed by the capability.
    virtual bool UsesObject(ObjectKey) const { return false; }
    // Runs independently on this client; it has no peer-clock precondition.
    virtual void Advance(Tick At,double DeltaSeconds) {}
    virtual bool Capture(const std::vector<ObjectKey>& Objects, Bytes& State) const = 0;
    virtual bool Validate(const std::vector<PrivateSystemState>& Shards,
        const std::vector<ObjectKey>& LocalObjects) const = 0;
    virtual bool Restore(const std::vector<PrivateSystemState>& Shards,
        const std::vector<ObjectKey>& LocalObjects, RestoreStage Stage) = 0;
    virtual bool RestoreQuery(const std::vector<PrivateSystemState>&,
        const std::vector<ObjectKey>&,RestoreStage) {return true;}

};
struct CommandMember
{
    ObjectKey Object;
    Revision ControlEpoch = 0;
    Revision OwnerEpoch = 0;
    std::uint32_t Slot = 0;
    double X = 0, Y = 0, Z = 0;
};
struct FrozenCommand
{
    Id Event;
    Tick At = 0;
    std::string Operation;
    Bytes Arguments;
    std::vector<CommandMember> Members;
};
struct CommandOutcome
{
    Tick At = 0;
    bool Accepted = false;
};
struct CommandReceiptMember
{
    ObjectKey Object;
    Revision ControlEpoch = 0, OwnerEpoch = 0;
    bool Accepted = false;
};
struct CommandReceipt
{
    Id Event;
    Tick At = 0;
    std::vector<CommandReceiptMember> Members;
};
struct ReceiveRejection
{
    Envelope Header;
    Tick ReceivedAt=0;
    std::size_t BodyBytes=0;
    bool ObjectKnown=false,Materialized=false;
    Life State=Life::Registered;
};
struct WorkDiagnostic
{
    Id Event;
    std::string Stage;
    ObjectKey Source,Target;
    CommitStatus Status=CommitStatus::Deferred;
    std::size_t Participants=0,Prepared=0,Committed=0;
    StateKey Missing;
    Revision RequiredVersion=0,CurrentVersion=0;
    Id RequestSender,NumericSourceOwner;
    Revision RequestOwnerVersion=0,NumericSourceOwnerVersion=0;
    Tick RequestAt=0,StartedAt=0,NumericSourceAge=0;
    bool NumericSourceKnown=false;
    Life NumericSourceLife=Life::Registered;
    std::string Rule,WaitReason;
};
GULINETCORE_API Bytes EncodeCommandReceipt(const CommandReceipt& Value);
GULINETCORE_API bool DecodeCommandReceipt(std::span<const std::uint8_t> Data, CommandReceipt& Out);

// Implemented in a world-specific adapter. No engine type crosses this boundary.
struct GULINETCORE_API IObjectMotionOutput {virtual ~IObjectMotionOutput()=default;};
/** Plain-data codec exclusively accessed by the existing room worker. */
class GULINETCORE_API IObjectMotionCodec
{
public:
    virtual ~IObjectMotionCodec()=default;
    virtual std::shared_ptr<IObjectMotionOutput> Decode(std::span<const std::uint8_t>,Revision Owner,Revision Match,bool& Valid)=0;
    virtual bool Feedback(Id,std::span<const std::uint8_t>)=0;
};
class GULINETCORE_API IObjectAdapter
{
public:
    virtual ~IObjectAdapter() = default;
    virtual std::uint32_t Version() const = 0;
    virtual std::vector<std::string> RequiredSystems() const { return {}; }
    // References needed by Create/Link. Dependencies are validated before any
    // entity is detached and restored in deterministic topological order.
    virtual std::vector<ObjectKey> RestoreDependencies(std::span<const std::uint8_t>) const { return {}; }
    // Prepare allocates a dormant local entity or a query/presentation proxy.
    virtual bool Prepare(const ObjectRecord& Record) = 0;
    virtual bool IsPreparationPending() const {return false;}
    virtual void RetryPreparation() {}
    virtual void PreparationCompleted(bool,std::uint32_t,const std::vector<std::string>&) {}
    virtual bool DefersMotionResult() const {return false;}
    // Restore preparation can read immutable provenance absent from a current
    // controller grant. It allocates the same dormant/query entity only.
    virtual bool PrepareRestore(const ObjectRecord& Record,std::span<const std::uint8_t> PrivateState) {return Prepare(Record);}
    virtual void SetAuthority(const Authority& Grant, bool LocalSimulation) = 0;
    virtual void Advance(Tick At, double DeltaSeconds) = 0;
    virtual std::uint32_t AdvancePeriod() const {return 1;} // Internal eligibility; zero means no periodic work.
    virtual std::shared_ptr<IObjectMotionCodec> CreateMotionCodec() {return {};}
    virtual bool ApplyDecodedMotion(const IObjectMotionOutput&,Tick) {return false;}
    virtual bool Command(const FrozenCommand& Command, const CommandMember& Member) = 0;
    virtual bool ApplyMotion(std::span<const std::uint8_t> Body, Tick At) = 0;
    virtual bool ApplyObjectEvent(std::span<const std::uint8_t> Body,Tick At) { return ApplyMotion(Body,At); }
    virtual bool ApplyPublicState(std::span<const std::uint8_t>,Tick) { return false; }
    virtual bool MotionFeedback(Id Peer, std::span<const std::uint8_t> Body) { return false; }
    virtual void ApplyLedger(const ObjectRecord& Record, Revision Ledger, bool BusinessEffects) = 0;
    virtual bool Capture(Bytes& PrivateState) const = 0;
    virtual bool Validate(std::span<const std::uint8_t> PrivateState) const = 0;
    virtual bool Restore(std::span<const std::uint8_t> PrivateState, RestoreStage Stage) = 0;
    virtual bool RestoreQuery(std::span<const std::uint8_t> PrivateState, RestoreStage Stage) { return true; }
    virtual void Release() = 0;
    virtual void ResetControlInput() {}
};
struct PrivateObjectState
{
    ObjectRecord Record;
    std::uint32_t AdapterVersion=0;
    Bytes State;
};
struct ObjectHandoffState
{
    std::uint32_t Version=HandoffVersion;
    Id Match,Source,Target;
    Revision Transfer=0;
    std::vector<PrivateObjectState> Objects;
    std::vector<PrivateSystemState> Systems;
    std::vector<StateFragment> Numeric;
    std::map<std::pair<Id,ObjectKey>,CommandOutcome> ExecutedCommands;
    std::vector<Message> IncomingCommands;
    std::vector<TransactionRequest> Pending;
};
GULINETCORE_API Bytes EncodeCommand(const FrozenCommand& Value);
GULINETCORE_API bool DecodeCommand(std::span<const std::uint8_t> Bytes,FrozenCommand& Out);
GULINETCORE_API Bytes EncodeHandoffState(const ObjectHandoffState& Value);
GULINETCORE_API bool DecodeHandoffState(std::span<const std::uint8_t> Bytes,ObjectHandoffState& Out);
GULINETCORE_API Bytes EncodeHandoff(const ObjectHandoff& Value);
GULINETCORE_API bool DecodeHandoff(std::span<const std::uint8_t> Bytes,ObjectHandoff& Out);
struct MotionReceiveStats
{
    std::uint64_t Received=0,Applied=0,AuthorityRejected=0,Stale=0,InvalidState=0;
    Tick LastReceived=0,LastApplied=0;
    Revision ReceivedOwnerEpoch=0,ExpectedOwnerEpoch=0;
    Id Sender,ExpectedSender;
};
class GULINETCORE_API ClientWorld
{
public:
    using Factory=std::function<std::unique_ptr<IObjectAdapter>(const ObjectRecord&)>;
    using Send=std::function<bool(Id Recipient,Message)>;
    using TransactionCallback=std::function<void(const CommitResult&)>;
    ClientWorld(Id Match,Id Player,Revision Epoch,Factory Create,Send Transmit);
    ~ClientWorld();
    ClientWorld(const ClientWorld&)=delete;
    ClientWorld& operator=(const ClientWorld&)=delete;
    bool Configure(const RuntimeManifest& Manifest,std::string& Error);
    void SetMembers(std::map<Id,Member> Members);
    bool Install(ObjectRecord Record,bool Commit);
    // Cold joins receive numeric births before their complete private restore
    // directory. Ordinary active-room materialization remains strict.
    bool SetBootstrapMaterializationDeferred(bool Value);
    bool IsBootstrapMaterializationDeferred() const {return BootstrapMaterializationDeferred;}
    bool InstallGrant(const Authority& Grant,std::uint32_t Ordinal=0);
    bool CommitHandoffs(std::span<const std::pair<ObjectHandoff,Authority>> Group);
    const std::map<ObjectKey,ObjectHandoff>& ActiveHandoffs() const {return Transfers;}
    bool RegisterSystem(std::string Name,std::unique_ptr<ISharedStateAdapter> Adapter);
    bool HasSimulation(ObjectKey Object,bool AllowDead=false) const;
    bool HasControl(ObjectKey Object) const;
    bool ReceivePeer(Id AuthenticatedSender,const Message& Value);
    bool Route(const FrozenCommand& Command);
    void SetCommandReceiptCallback(std::function<void(const CommandReceipt&)> Callback) {OnCommandReceipt=std::move(Callback);}
    void SetHandoffAdmission(std::function<bool(Id)> Ready) {HandoffAdmission=std::move(Ready);}
    void SetCommitCallback(TransactionCallback Callback) {OnCommit=std::move(Callback);}
    const std::deque<CommitResult>& UnsettledReceipts() const {return UnsettledReceiptEvidence;}
    std::uint64_t UnsettledReceiptCount() const {return UnsettledReceiptTotal;}
    Id Submit(TransactionRequest Request,TransactionCallback Callback,bool PreserveOriginalTime=false);
    Id SubmitAdmitted(TransactionRequest Request,bool PreserveOriginalTime=false);
    Id SubmitControlled(TransactionRequest Request,bool Admit,bool PreserveOriginalTime=false);
    bool PublishObjectEvent(ObjectKey Object,Id Event,Bytes Body);
    bool ApplyCommit(const CommitResult& Result);
    // Only initial readiness and membership join/leave use this world pause.
    void SetMembershipPaused(bool Value);
    void PollNetwork(double MonotonicSeconds);
    std::uint32_t Advance(double WallDeltaSeconds);
    bool PrepareHandoff(const ObjectHandoff& Transfer);
    bool FreezeHandoff(const ObjectHandoff& Transfer);
    bool InstallHandoff(Id Sender,const ObjectHandoffState& State);
    bool CommitHandoff(const ObjectHandoff& Transfer,const Authority& Grant);
    void CancelHandoff(const ObjectHandoff& Transfer);
    void ReconcileHandoffs(std::span<const ObjectHandoff> Active);
    std::optional<CommandReceipt> CommandResult(Id Event) const;
    void FinishBootstrap();
    bool CaptureObjects(std::span<const ObjectKey> Keys,Id Target,Revision Transfer,ObjectHandoffState& Out) const;
    bool ImportObjects(const ObjectHandoffState& State);
    bool ImportBootstrap(std::span<const ObjectHandoffState> Parts);
    using BootstrapNumericSource=std::function<bool(Id,const OwnerState::FragmentVisitor&)>;
    bool ImportBootstrap(std::span<const ObjectHandoffState> Parts,const BootstrapNumericSource& ReadNumeric);
    bool StageBootstrapDirectory(std::span<const ObjectHandoffState> Parts);
    bool RebuildFromReplica(ObjectKey Object);
    void Cancel(std::string Reason);
    const std::string& Error() const {return Failure;}
    Tick Now() const {return Clock.Now();}
    Tick ObjectNow(ObjectKey Object) const;
    Tick ContextNow() const {return ActiveObject.Valid()?ObjectNow(ActiveObject):Now();}
    Revision Epoch() const {return Clock.GetEpoch();}
    bool IsPaused() const {return MembershipPaused;}
    bool ExplicitlyPaused() const {return MembershipPaused;}
    bool CanPublishFacts() const {return Failure.empty() && !MembershipPaused;}
    const std::map<ObjectKey,ObjectRecord>& Objects() const {return Directory;}
    const ObjectRecord* FindTeamCapability(Team Faction,const std::string& Capability) const;
    const std::map<std::string,std::size_t>& CommanderDefinitions() const {return CommanderDefinitionCounts;}
    void SetRecordObserver(std::function<void(const ObjectRecord&)> Observer) {OnRecord=std::move(Observer);}
    void SetFragmentObserver(std::function<void(std::span<const StateFragment>)> Observer) {OnFragments=std::move(Observer);}
    enum class ApplicationStatus : std::uint8_t {Pending,Ready,Rejected};
    using ApplicationPreflight=std::function<ApplicationStatus(std::span<const PrivateObjectState>,std::span<const PrivateSystemState>)>;
    using ApplicationRestore=std::function<ApplicationStatus(const ObjectHandoffState&,std::span<const ObjectKey>,
        const std::map<std::string,std::vector<ObjectKey>>&,RestoreStage)>;
    enum class CaptureApplicationStatus : std::uint8_t {Pending,Ready,Invalid,TooLarge};
    void SetApplicationBatches(ApplicationPreflight Prepare,std::function<CaptureApplicationStatus(std::span<const ObjectKey>)> Capture,ApplicationRestore Restore)
    {PrepareApplications=std::move(Prepare);CaptureApplications=std::move(Capture);RestoreApplications=std::move(Restore);}
    void SetApplicationCommands(std::function<ApplicationStatus(const FrozenCommand&,std::span<const CommandMember>)> Apply) {PrepareCommands=std::move(Apply);}
    void SetCommandApplicationCancellation(std::function<void(const FrozenCommand&)> Cancel) {CancelCommands=std::move(Cancel);}
    bool LastApplicationPending() const {return ApplicationPending;}
    void ResetApplicationPending() {ApplicationPending=false;}
    void SetLogicalStepBoundary(std::function<void()> Callback) {LogicalStepBoundary=std::move(Callback);}
    void SetApplicationCancellation(std::function<void(ObjectKey)> Callback) {ApplicationCancelled=std::move(Callback);}
    bool IsRequestPending(Id Event) const {return Pending.contains(Event)||PlayerIntents.contains(Event);}
    bool PublishDirectoryLife(ObjectKey,Revision OwnerVersion);
    bool LastCapturePending() const {return CapturePending;}
    bool IsApplyingOwnershipGroup() const {return CommittingOwnershipGroup;}
    void SetApplicationResultBudget(std::function<bool(const OwnerWork&)> Reserve,std::function<void(Id)> Release)
    {ReserveApplicationResult=std::move(Reserve);ReleaseApplicationResult=std::move(Release);}
    const std::set<ObjectKey>& FrozenObjects() const {return Frozen;}
    const std::map<ObjectKey,double>& PendingApplications() const {return PreparingApplications;}
    void CompleteApplication(ObjectKey,bool,std::uint32_t,const std::vector<std::string>&);
    void CompleteMotionApplication(ObjectKey,MessageKind,Tick,Revision,bool);
    const std::string& DirectoryHash() const;
    const OwnerState& NumericState() const {return Numbers;}
    void SetReducedWork(bool Enabled) {Numbers.SetReducedWork(Enabled);}
    auto ConsumeRetiredLifeChanges() {return Numbers.ConsumeRetiredLifeChanges();}
    const std::map<ObjectKey,MotionReceiveStats>& MotionDiagnostics() const {return MotionStats;}
    std::uint64_t RejectedMotionLives() const {return UnknownMotionLives;}
    std::uint64_t RejectedStateImports() const {return FailedStateImports;}
    const std::string& LastStateImportFailureReason() const {return LastStateImportFailure;}
    const std::vector<ReceiveRejection>& FirstReceiveRejections() const {return ReceiveRejections;}
    const std::vector<ReceiveRejection>& RecentReceiveRejections() const {return RecentRejections;}
    const std::map<std::string,std::uint64_t>& HandoffDiagnostics() const {return HandoffIssues;}
    const std::map<std::string,std::uint64_t>& HandoffMeasurements() const {return HandoffSizes;}
    std::uint64_t DuplicateResults() const {return DuplicateResultCount;}
    std::size_t ExecutedCommandCount() const {return CommandExecutions;}
    std::size_t ExecutedCommandCount(ObjectKind Kind) const;
    std::vector<TransactionRequest> PendingTransactions() const;
    std::map<std::string,std::size_t> WorkCounts() const;
    std::vector<WorkDiagnostic> WorkDiagnostics() const;
private:
    bool PrepareAdapterRecord(const ObjectRecord& Record,const PrivateObjectState* Private=nullptr);
    bool ImportObjectsImpl(const ObjectHandoffState& State,bool NumericPrepared);
    mutable std::map<std::string,std::uint64_t> HandoffIssues;
    mutable std::map<std::string,std::uint64_t> HandoffSizes;
    void MeasureHandoff(const std::string& Part,std::uint64_t Bytes) const
    {++HandoffSizes[Part+"/count"];HandoffSizes[Part+"/bytes"]+=Bytes;auto& Maximum=HandoffSizes[Part+"/maximum_bytes"];if (Bytes>Maximum) Maximum=Bytes;}
    bool HandoffIssue(const std::string& Stage) const {++HandoffIssues[Stage];return false;}
    std::uint64_t DuplicateResultCount=0;
    bool ValidatePeer(Id Sender,const Envelope& Header,Domain Field,bool Dead=false) const;
    bool ApplyCommand(Id Sender,const Envelope& Header,const FrozenCommand& Command);
    void DrainCommands();
    void FlushCommandOutbox();
    void CompleteCommandReceipt(const CommandReceipt& Receipt);
    void QueueHandoffCommand(ObjectKey Object,Message Value);
    bool StartWork(Id Sender,const TransactionRequest& Request);
    void AdvanceWork(Id Event);
    bool ReceiveWork(Id Sender,const Message& Value);
    bool SendMessage(Id Peer,MessageKind Kind,Id Event,ObjectKey Object,Bytes Body);
    bool Broadcast(MessageKind Kind,Id Event,ObjectKey Object,const Bytes& Body);
    void FlushPublications();
    void RefreshRecords(std::span<const StateFragment> Fragments,bool BusinessEffects=false);
    void StoreRecord(const ObjectRecord& Record);
    bool ImportPeerState(Id Sender,const Message& Value);
    void RetryPeerStates();
    Id Match,Player;
    Factory Create;
    Send Transmit;
    BattleClock Clock;
    OwnerState Numbers;
    mutable ObjectKey ActiveObject;
    bool MembershipPaused=true;
    std::string Failure;
    double NetworkTime=0;
    std::uint64_t Sequence=1,UnknownMotionLives=0;
    std::uint64_t FailedStateImports=0;
    struct DeferredPeerState {Id Sender;Message Value;double Expires=0;};
    std::deque<DeferredPeerState> DeferredPeerStates;
    std::size_t DeferredPeerStateBytes=0;
    std::vector<ReceiveRejection> ReceiveRejections;
    std::vector<ReceiveRejection> RecentRejections;
    void RecordReceiveRejection(const Message& Value);
    std::map<Id,Member> Members;
    std::map<ObjectKey,ObjectRecord> Directory;
    std::map<ObjectKey,std::set<ObjectKey>> LiveParentWork;
    std::map<std::pair<Team,std::string>,std::set<ObjectKey>> CapabilityObjects;
    std::map<std::string,std::size_t> CommanderDefinitionCounts;
    std::function<void(const ObjectRecord&)> OnRecord;
    std::function<void(std::span<const StateFragment>)> OnFragments;
    ApplicationPreflight PrepareApplications;
    std::function<CaptureApplicationStatus(std::span<const ObjectKey>)> CaptureApplications;
    mutable bool CapturePending=false;
    std::function<bool(const OwnerWork&)> ReserveApplicationResult;
    std::function<void(Id)> ReleaseApplicationResult;
    ApplicationRestore RestoreApplications;
    std::function<ApplicationStatus(const FrozenCommand&,std::span<const CommandMember>)> PrepareCommands;
    std::function<void(const FrozenCommand&)> CancelCommands;
    bool ApplicationPending=false;
    std::function<void()> LogicalStepBoundary;
    std::function<void(ObjectKey)> ApplicationCancelled;
    struct ImportContinuation
    {
        std::vector<ObjectKey> Keys,Order;
        std::map<std::string,std::vector<ObjectKey>> Required;
        std::vector<PrivateSystemState> SharedState;
        std::uint8_t Stage=0;
    };
    std::map<std::string,ImportContinuation> ImportContinuations;
    struct BootstrapContinuation {bool NumericReady=false;std::size_t Part=0;};
    std::map<std::string,BootstrapContinuation> BootstrapContinuations;
    std::map<ObjectKey,double> PreparingApplications;
    std::map<std::pair<ObjectKey,MessageKind>,Tick> PendingMotion;
    bool AwaitingApplications(const CommitResult& Result) const;
    bool CommitDependenciesReady(const CommitResult& Result) const;
    mutable std::string CachedDirectoryHash;
    mutable bool DirectoryHashDirty=true;
    std::set<ObjectKey> SimulationCandidates;
    std::map<ObjectKey,std::unique_ptr<IObjectAdapter>> Adapters;
    bool BootstrapMaterializationDeferred=false;
    std::set<ObjectKey> DeferredMaterialization;
    std::map<std::string,std::unique_ptr<ISharedStateAdapter>> Systems;
    std::map<ObjectKey,Tick> MotionApplied,PublicStateApplied;
    std::map<ObjectKey,MotionReceiveStats> MotionStats;
    std::map<std::pair<Id,ObjectKey>,CommandOutcome> Commands;
    std::size_t CommandExecutions=0;
    std::map<ObjectKind,std::size_t> CommandExecutionsByKind;
    std::deque<Message> IncomingCommands;
    struct OutgoingCommand {FrozenCommand Value;Bytes Identity;std::size_t ByteCost=0;double RetryAt=0;};
    std::map<Id,OutgoingCommand> CommandOutbox;
    std::size_t CommandOutboxBytes=0;
    std::map<ObjectKey,FrozenCommand> LatestCommands;
    std::map<ObjectKey,std::tuple<Revision,Revision,Id>> RebuildApplications;
    struct Publication {Message Value;std::string Identity;std::size_t Bytes=0;};
    std::map<Id,std::deque<Publication>> Publications;
    std::set<std::pair<Id,std::string>> PublicationIdentities;
    std::size_t PublicationBytes=0;
    std::function<void(const CommandReceipt&)> OnCommandReceipt;
    std::function<bool(Id)> HandoffAdmission;
    TransactionCallback OnCommit;
    std::deque<CommitResult> UnsettledReceiptEvidence;
    std::uint64_t UnsettledReceiptTotal=0;
    struct PendingTransaction {TransactionRequest Request;TransactionCallback Callback;double RetryAt=0;};
    std::map<Id,PendingTransaction> Pending;
    struct PlayerIntent {TransactionRequest Request;bool Admit=false;double RetryAt=0;};
    std::map<Id,PlayerIntent> PlayerIntents;
    std::map<ObjectKey,Life> PublishedLife;
    std::set<Id> ObservedCommits;
    std::map<Id,CommitResult> WaitingResults;
    struct WorkFlow
    {
        OwnerWork Work;
        Bytes Encoding;
        Id Requester;
        std::set<Id> Participants,Prepared,Committed;
        bool Decision=false;
        double RetryAt=0;
    };
    std::map<Id,WorkFlow> WorkFlows;
    struct WorkEvidence {OwnerWork Work;bool Decided=false;};
    std::map<Id,WorkEvidence> KnownWork;
    double NextWorkRetry=0;
    double NextStateRepair=0;
    std::string LastStateImportFailure;
    std::map<Id,std::pair<Id,TransactionRequest>> RetryWork;
    std::map<Id,std::string> PlanWaitReasons;
    std::map<ObjectKey,ObjectHandoff> Transfers;
    bool CommittingOwnershipGroup=false;
    // A bounded group cut is stored once even though every member retains its
    // own transfer acknowledgment and owner version.
    struct IncomingHandoff {ObjectHandoffState State;bool Detached=false;};
    struct OutgoingHandoff {Bytes State;Revision Identity=0;bool Detached=false;};
    std::map<Revision,std::shared_ptr<IncomingHandoff>> IncomingStates;
    std::map<ObjectKey,ObjectHandoff> CompletedTransfers;
    std::map<Revision,std::pair<double,ObjectHandoffState>> EarlyHandoffStates;
    std::set<ObjectKey> Frozen,FreezeRequested;
    std::map<ObjectKey,std::shared_ptr<OutgoingHandoff>> OutgoingStates;
    std::map<Id,std::size_t> OutgoingCaptureLimits;
    std::map<ObjectKey,Authority> PendingGrants;
    std::map<ObjectKey,ObjectHandoff> PendingHandoffReadiness;
    std::map<ObjectKey,std::vector<Message>> HandoffCommands;
};
}
