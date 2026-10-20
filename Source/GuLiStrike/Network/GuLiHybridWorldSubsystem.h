#pragma once
#include "CoreMinimal.h"
#include "Network/GuLiHybridObjectKey.h"
#include "Subsystems/WorldSubsystem.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Gameplay/Economy/GuLiEconomyTypes.h"
#include "Network/GuLiHybridSessionSubsystem.h"
#include <memory>
#include <functional>
#include "GuLiHybridWorldSubsystem.generated.h"

struct FGuLiHybridComponentBindingContext;
class FJsonObject;
namespace GuLiNet {struct PreparedCommit;struct ClientQueryDelta;class IGameplayCalculation;class IObjectMotionCodec;struct IObjectMotionOutput;}

struct FGuLiHybridOwnershipMetrics
{
    uint64 Committed=0,ReceivedPayloadBytes=0,QueuedPayloadBytes=0;
    double LastMilliseconds=0;
};

struct FGuLiHybridUpgradeSelection
{
    uint64 CatalogRevision=0,Revision=0;
    TArray<FName> Chosen;
};
// A projection of the two independent inventory capabilities, shared by Actor
// and Mass hosts. Versions belong to these inventories, never to a work event.
struct FGuLiHybridInventoryView
{
    struct FDelivery { FGuid Event; FGuLiHybridObjectKey Source; EGuLiTeam Beneficiary=EGuLiTeam::Unassigned; FGuLiResourceAmounts Amount; };
    FGuLiResourceAmounts Cargo;
    TArray<FDelivery> Queue;
    uint64 CargoVersion=0,ProcessingVersion=0;
    uint64 NextExtraction=0,NextProcessing=0;
};
struct FGuLiHybridCapabilityState
{
    uint32 Version=0;
    TArray<uint8> Data;
};
struct GULISTRIKE_API FGuLiHybridObjectState
{
    FGuLiHybridObjectKey Key;
    FGuid Controller, SimulationOwner;
    FGuid SpawnEvent, OwnershipGroup;
    uint64 GroupEpoch=0;
    uint64 ControlEpoch = 0, OwnerEpoch = 0;
    uint64 BornAt=0, LogicalAge=0;
    uint8 OwnershipPolicy=0, ControllerSeat=3;
    bool bRetireOnVacancy=false;
    EGuLiTeam Team = EGuLiTeam::Unassigned;
    uint8 LifeState = 0;
    FName Definition;
    uint32 Ordinal = 0;
    float FacingYawDegrees = 0;
    FVector Location = FVector::ZeroVector;
    FGuLiCombatHealthState Health;
    bool bCompleted=true, bSupplied=false, bPhased=false;
    bool bDormant=false;
    FGuid StateCustodian;
    FGuLiHybridObjectKey Parent;
    uint64 RelationRevision=0;
    TArray<uint8> FrozenWork;
    TMap<FName,FGuLiHybridUpgradeSelection> Upgrades;
    TMap<FName,FGuLiHybridCapabilityState> Capabilities;
    EGuLiTeam Affiliation=EGuLiTeam::Unassigned;
};
struct FGuLiHybridCommandMember
{
    FGuLiHybridObjectKey Key;
    uint64 ControlEpoch = 0, OwnerEpoch = 0;
    uint32 Slot = 0;
    FVector Destination = FVector::ZeroVector;
};
struct FGuLiHybridCommand
{
    FGuid Event;
    uint64 LogicalTick = 0;
    FName Operation;
    TArray<uint8> Arguments;
    TArray<FGuLiHybridCommandMember> Members;
};
struct FGuLiHybridCommandResult
{
    FGuid Event;
    uint64 LogicalTick=0;
    struct FMember { FGuLiHybridObjectKey Key; uint64 ControlEpoch=0,OwnerEpoch=0; bool bAccepted=false; };
    TArray<FMember> Members;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiHybridCommandCompleted,const FGuLiHybridCommandResult&);

UENUM(BlueprintType)
enum class EGuLiHybridTransactionStatus : uint8
{ Committed, Duplicate, Deferred, BadRequest, WrongEpoch, Unauthorized, MissingObject,
  TargetDead, FriendlyFire, TooOld, InvalidRule, InsufficientResources, InvalidReservation, Paused, Interrupted };

UENUM(BlueprintType)
enum class EGuLiHybridEconomyOperation : uint8 { None, Credit, Reserve, Commit, Refund };

UENUM(BlueprintType)
enum class EGuLiHybridObjectOperation : uint8 { None, Spawn, Complete, Destroy, Relation };

UENUM(BlueprintType)
enum class EGuLiHybridResourceOperation : uint8 { None, Extract, Deliver, Process };

UENUM(BlueprintType)
enum class EGuLiHybridMiningOperation : uint8 { None, ClaimPosition, ClaimNode, Release };

UENUM(BlueprintType)
enum class EGuLiHybridMiningStatus : uint8 { Committed, Duplicate, Invalid, Conflict, Occupied, MissingLease, Deferred };

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiHybridMiningLease
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FGuLiHybridObjectKey Source;
    UPROPERTY(BlueprintReadOnly) int32 ClusterId=0;
    UPROPERTY(BlueprintReadOnly) uint8 Sample=0;
    UPROPERTY(BlueprintReadOnly) int64 Task=0;
    UPROPERTY(BlueprintReadOnly) FGuid Token;
    UPROPERTY(BlueprintReadOnly) int32 NodeId=0;
    UPROPERTY(BlueprintReadOnly) int64 Revision=0;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiHybridMiningRequest
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuLiHybridObjectKey Source;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuid Event;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Rule;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) EGuLiHybridMiningOperation Operation=EGuLiHybridMiningOperation::None;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 ClusterId=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) uint8 Sample=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int64 Task=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuid Token;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 NodeId=0;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiHybridResourceRequest
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuLiHybridObjectKey Source;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuLiHybridObjectKey Target;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuid Event;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Rule;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) EGuLiHybridResourceOperation Operation=EGuLiHybridResourceOperation::None;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 NodeId=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuid MiningToken;
};

/** Result numbers/relations come from the verified manifest rule. Coordinates are a simulation fact. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiHybridObjectRequest
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuLiHybridObjectKey Source;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuLiHybridObjectKey Target;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuid Event;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Rule;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) EGuLiHybridObjectOperation Operation=EGuLiHybridObjectOperation::None;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector Location=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float FacingYaw=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuLiHybridObjectKey Related;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int64 ReservationRequest=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuid ReservationToken;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int64 ExpectedRelationRevision=0;
    /** Frozen source-object age for surviving effects; zero starts at admission. Never a world/controller clock. */
    uint64 StartedAt=0;
    /** Immutable admission payload, interpreted by the spawned capability family. */
    TArray<uint8> CapabilityPayload;
    TOptional<uint64> OriginalAt;
};

/** Amounts are resolved by a manifest rule; callers cannot submit an arbitrary price or credit. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiHybridEconomyRequest
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGuLiHybridObjectKey Source;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGuid Event;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Rule;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EGuLiHybridEconomyOperation Operation=EGuLiHybridEconomyOperation::Reserve;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int64 ReservationRequest=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGuid ReservationToken;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiHybridUpgradeRequest
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuLiHybridObjectKey Source;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FGuid Event;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Rule;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Node;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int64 ExpectedSelectionRevision=0;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiHybridTransactionResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FGuid Event;
    UPROPERTY(BlueprintReadOnly) EGuLiHybridTransactionStatus Status=EGuLiHybridTransactionStatus::BadRequest;
    UPROPERTY(BlueprintReadOnly) EGuLiTeam Team=EGuLiTeam::Unassigned;
    UPROPERTY(BlueprintReadOnly) FGuLiResourceAmounts Balance;
    UPROPERTY(BlueprintReadOnly) FGuid ReservationToken;
    UPROPERTY(BlueprintReadOnly) FGuLiResourceAmounts ReservedCost;
    UPROPERTY(BlueprintReadOnly) int64 LedgerRevision=0;
    UPROPERTY(BlueprintReadOnly) bool bBusinessEffects=false;
    UPROPERTY(BlueprintReadOnly) EGuLiHybridObjectOperation ObjectOperation=EGuLiHybridObjectOperation::None;
    UPROPERTY(BlueprintReadOnly) FGuLiHybridObjectKey Target;
    UPROPERTY(BlueprintReadOnly) uint8 LifeState=0;
    UPROPERTY(BlueprintReadOnly) bool bCompleted=false;
    UPROPERTY(BlueprintReadOnly) bool bSupplied=false;
    UPROPERTY(BlueprintReadOnly) bool bPhased=false;
    UPROPERTY(BlueprintReadOnly) FGuLiHybridObjectKey Parent;
    UPROPERTY(BlueprintReadOnly) int64 RelationRevision=0;
    UPROPERTY(BlueprintReadOnly) EGuLiHybridResourceOperation ResourceOperation=EGuLiHybridResourceOperation::None;
    UPROPERTY(BlueprintReadOnly) FGuLiHybridObjectKey ResourceSource;
    UPROPERTY(BlueprintReadOnly) int32 ResourceNode=0;
    UPROPERTY(BlueprintReadOnly) int32 RemainingRaw=0;
    UPROPERTY(BlueprintReadOnly) FGuLiResourceAmounts Cargo;
    UPROPERTY(BlueprintReadOnly) FGuLiResourceAmounts RawAmount;
    UPROPERTY(BlueprintReadOnly) FGuLiResourceAmounts Credited;
    UPROPERTY(BlueprintReadOnly) EGuLiTeam Beneficiary=EGuLiTeam::Unassigned;
    UPROPERTY(BlueprintReadOnly) FGuid DeliveryEvent;
    UPROPERTY(BlueprintReadOnly) int64 ResourceRevision=0;
    UPROPERTY(BlueprintReadOnly) EGuLiHybridMiningOperation MiningOperation=EGuLiHybridMiningOperation::None;
    UPROPERTY(BlueprintReadOnly) EGuLiHybridMiningStatus MiningStatus=EGuLiHybridMiningStatus::Invalid;
    UPROPERTY(BlueprintReadOnly) FGuLiHybridMiningLease MiningLease;
    UPROPERTY(BlueprintReadOnly) int64 MiningRevision=0;
    bool IsCommitted() const { return Status==EGuLiHybridTransactionStatus::Committed || Status==EGuLiHybridTransactionStatus::Duplicate; }
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGuLiHybridTransactionCompleted, const FGuLiHybridTransactionResult&, Result);
enum class EGuLiHybridRestoreStage : uint8 { Detach, Create, Link, Activate };
struct FGuLiHybridSharedStatePiece
{
    FName Name;
    FGuid Player;
    uint32 Version=0;
    TArray<FGuLiHybridObjectKey> Objects;
    TArray<uint8> Bytes;
};
class GULISTRIKE_API IGuLiHybridSharedStateAdapter
{
public:
    virtual ~IGuLiHybridSharedStateAdapter() = default;
    virtual UObject* LifetimeOwner() const = 0;
    virtual uint32 Version() const = 0;
    virtual bool UsesObject(const FGuLiHybridObjectKey&) const { return false; }
    virtual void ResetResourceRequests(const TArray<FGuLiHybridObjectKey>&,uint64) {}
    virtual void Advance(uint64 LogicalTick,double DeltaSeconds) {}
    virtual bool Capture(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Bytes) const = 0;
    virtual bool CaptureCurrent(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Bytes) const {return Capture(Objects,Bytes);}
    virtual bool Validate(const TArray<FGuLiHybridSharedStatePiece>& Pieces,
        const TArray<FGuLiHybridObjectKey>& LocalObjects) const = 0;
    virtual bool Restore(const TArray<FGuLiHybridSharedStatePiece>& Pieces,
        const TArray<FGuLiHybridObjectKey>& LocalObjects,EGuLiHybridRestoreStage Stage) = 0;
    virtual bool RestoreQuery(const TArray<FGuLiHybridSharedStatePiece>&,
        const TArray<FGuLiHybridObjectKey>&,EGuLiHybridRestoreStage) {return true;}


};

/** One stable object, with explicit private-state version and no engine-handle serialization. */
class GULISTRIKE_API IGuLiHybridSimulationAdapter
{
public:
    virtual ~IGuLiHybridSimulationAdapter() = default;
    virtual UObject* LifetimeOwner() const = 0;
    virtual uint32 Version() const = 0;
    virtual TArray<FName> RequiredSharedStateSystems() const { return {}; }
    virtual TArray<FGuLiHybridObjectKey> RestoreDependencies(TConstArrayView<uint8>) const { return {}; }
    virtual bool Prepare(const FGuLiHybridObjectState& State) = 0;
    virtual bool PrepareRestore(const FGuLiHybridObjectState& State,TConstArrayView<uint8> PrivateState) {return Prepare(State);}
    virtual void SetAuthority(const FGuLiHybridObjectState& State, bool bLocalSimulation) = 0;
    virtual void Advance(uint64 LogicalTick, double DeltaSeconds) = 0;
    virtual uint32 AdvancePeriod() const {return 1;}
    virtual std::shared_ptr<GuLiNet::IObjectMotionCodec> CreateMotionCodec() {return {};}
    virtual bool ApplyDecodedMotion(const GuLiNet::IObjectMotionOutput&,uint64) {return false;}
    virtual bool Execute(const FGuLiHybridCommand& Command, const FGuLiHybridCommandMember& Member) = 0;
    virtual bool ApplyMotion(TConstArrayView<uint8> Bytes, uint64 LogicalTick) = 0;


    virtual bool ApplyMotionFeedback(const FGuid& Peer,TConstArrayView<uint8> Bytes) { return false; }
    virtual void ApplyLedger(const FGuLiHybridObjectState& State, uint64 LedgerRevision, bool bBusinessEffects) = 0;
    virtual bool Capture(TArray<uint8>& Bytes) const = 0;
    virtual bool Validate(TConstArrayView<uint8> Bytes) const = 0;


    virtual bool Restore(TConstArrayView<uint8> Bytes, EGuLiHybridRestoreStage Stage) = 0;
    virtual bool RestoreQuery(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) { return false; }
    virtual void Release() = 0;
    virtual void ResetControlInput() {}
    virtual void ResetResourceRequests(uint64) {}
};
using FGuLiHybridAdapterFactory = TFunction<TSharedPtr<IGuLiHybridSimulationAdapter>(const FGuLiHybridObjectState&)>;
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiHybridWorldFailure, const FString&);
DECLARE_MULTICAST_DELEGATE(FGuLiHybridWorldReady);
DECLARE_MULTICAST_DELEGATE(FGuLiHybridResourceDirectoryChanged);
DECLARE_MULTICAST_DELEGATE_TwoParams(FGuLiHybridResourceNodeChanged,uint32,uint8);
DECLARE_MULTICAST_DELEGATE(FGuLiHybridMiningReservationsChanged);
DECLARE_MULTICAST_DELEGATE_OneParam(FGuLiHybridObjectChanged,const FGuLiHybridObjectKey&);

/** Per-World authority, logical time and state. Session callbacks are revoked on travel. */
UCLASS()
class GULISTRIKE_API UGuLiHybridWorldSubsystem final : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    UGuLiHybridWorldSubsystem();
    virtual ~UGuLiHybridWorldSubsystem() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    bool SubscribeObjectReceipt(UObject* Listener,const FGuLiHybridObjectKey& Key,TFunction<void(const FGuLiHybridTransactionResult&)> Callback);
    void UnsubscribeObjectReceipt(UObject* Listener);
    bool SubmitGameplayCalculation(FName DependencyGroup,std::shared_ptr<GuLiNet::IGameplayCalculation> Work,
        uint64 InputBytes,uint64 MaximumResultBytes,std::function<void()> Apply);
    bool HasPendingGameplayCalculation(FName DependencyGroup) const;
    bool ConfirmGameplayCalculation(std::shared_ptr<GuLiNet::IGameplayCalculation> Work,uint64 Bytes);
    void ApplyLateGameplay(UWorld* World,ELevelTick TickType,float DeltaSeconds);
    bool RegisterFactory(EGuLiHybridObjectKind Kind, UObject& LifetimeOwner, FGuLiHybridAdapterFactory Factory);
    bool RegisterSharedStateSystem(FName Name,TSharedPtr<IGuLiHybridSharedStateAdapter> Adapter);
    void UnregisterFactories(const UObject& LifetimeOwner);
    bool BindActor(const FGuLiHybridObjectKey& Key,AActor& Actor,uint32 Ordinal=0);
    void UnbindActor(const FGuLiHybridObjectKey& Key,const AActor& Actor);
    uint32 GetActorTaskGeneration(uint32 Ordinal) const;
    AActor* FindBoundActor(const FGuLiHybridObjectKey& Key) const;
    uint64 GetActorBindingRevision() const;
    bool BindTarget(const FGuLiTargetHandle& Target, const FGuLiHybridObjectKey& Key);
    bool ResolveTarget(const FGuLiTargetHandle& Target, FGuLiHybridObjectKey& Out) const;
    bool ResolveObjectTarget(const FGuLiHybridObjectKey& Key,FGuLiTargetHandle& Out) const;
    bool GetObject(const FGuLiHybridObjectKey& Key, FGuLiHybridObjectState& Out) const;
    /** Lightweight applied facts; excludes capability, upgrade and private-work payloads. */
    bool GetObjectView(const FGuLiHybridObjectKey& Key,FGuLiHybridObjectState& Out) const;
    bool GetTeamCapabilityObject(EGuLiTeam Team,FName Capability,FGuLiHybridObjectState& Out) const;
    FGuLiHybridObjectKey GetTeamCapabilityKey(EGuLiTeam Team,FName Capability) const;
    void BeginHUDQueryScope();
    void EndHUDQueryScope();
    void GetCommanderDefinitions(TArray<FName>& Out) const;
    FGuLiHybridObjectChanged OnObjectChanged;
    /** Current object or authenticated exact-life retired metadata; never grants an entity or simulation. */
    bool GetObjectMetadata(const FGuLiHybridObjectKey& Key, FGuLiHybridObjectState& Out) const;
    uint32 GetLatestRetiredLife(const FGuLiHybridObjectKey& Stable) const;
    void GetObjects(TArray<FGuLiHybridObjectState>& Out) const;
    /** Development readback: count authoritative live work without copying capability records. */
    TSharedRef<FJsonObject> GetSimulationCensus() const;
    bool HasSimulation(const FGuLiHybridObjectKey& Key, bool bRetainedSource = false) const;
    bool HasControl(const FGuLiHybridObjectKey& Key) const;
    uint64 GetObjectLogicalTick(const FGuLiHybridObjectKey& Key) const;
    bool RouteCommand(const FGuLiHybridCommand& Command);
    UFUNCTION(BlueprintCallable,Category="Hybrid|Ownership") void SetCommanderSelection(const TArray<FGuLiHybridObjectKey>& Objects);
    UFUNCTION(BlueprintCallable,Category="Hybrid|Ownership") void ReportDirectOperation(const TArray<FGuLiHybridObjectKey>& Objects);
    FGuLiHybridOwnershipMetrics GetOwnershipMetrics() const;
    TSharedRef<FJsonObject> GetTransactionCaptureSnapshot() const;
    FGuLiHybridCommandCompleted OnCommandCompleted;
    bool PublishMotion(const FGuLiHybridObjectKey& Key, const TArray<uint8>& Body);
    /** Shared 10 Hz public-capability pump, including families without movement. */
    void PublishComponentMotionIfDue(const FGuLiHybridObjectKey& Key,FGuLiHybridComponentBindingContext* Context=nullptr);
    /** Reliable simulator-owned event for any registered object family. The
     * family decodes its payload; the transport does not classify content. */
    bool PublishEffect(const FGuLiHybridObjectKey& Key,const FGuid& Event,const TArray<uint8>& Body);
    bool PublishMotionTo(const FGuLiHybridObjectKey& Key,const FGuid& Peer,const TArray<uint8>& Body,uint64 CapturedTick=MAX_uint64);
    bool PublishMotionAcknowledgment(const FGuLiHybridObjectKey& Key,const TArray<uint8>& Body);
    FGuid SubmitDamage(const FGuLiDamageRequest& Request, uint64 StartedAt,
        TFunction<void(const FGuLiDamageCommitResult&)> Callback = {},const FGuLiHybridObjectKey* EffectAuthority=nullptr,TOptional<uint64> OriginalAt={});
    bool FindDamageResult(const FGuid& Event,FGuLiDamageCommitResult& Out) const;
    UFUNCTION(BlueprintCallable,Category="Hybrid|Transactions") FGuid SubmitResourceOperation(const FGuLiHybridResourceRequest& Request);
    FGuid SubmitResourceOperationNative(const FGuLiHybridResourceRequest& Request,TFunction<void(const FGuLiHybridTransactionResult&)> Callback);
    FGuid SubmitAdmittedWork(const FGuLiHybridObjectKey& Work);
    bool GetResourceNodeRemaining(uint32 Node,uint8& Remaining) const;
    bool GetCommittedInventory(const FGuLiHybridObjectKey& Key,FGuLiHybridInventoryView& Out,
        uint64 AfterCargoVersion=0,uint64 AfterProcessingVersion=0) const;
    EGuLiTeam GetResourceTerritoryOwner(uint32 Territory) const;
    bool GetResourceQuerySummary(int32& Nodes,int64& Remaining,uint64& Revision) const;
    bool GetResourceDirectoryMetadata(FString& LayoutHash,int32& Nodes,uint64& Revision) const;
    FGuLiHybridResourceDirectoryChanged OnResourceDirectoryChanged;
    FGuLiHybridResourceNodeChanged OnResourceNodeChanged;
    UFUNCTION(BlueprintCallable,Category="Hybrid|Transactions") FGuid SubmitMiningOperation(const FGuLiHybridMiningRequest& Request);
    FGuid SubmitMiningOperationNative(const FGuLiHybridMiningRequest& Request,TFunction<void(const FGuLiHybridTransactionResult&)> Callback);
    UFUNCTION(BlueprintPure,Category="Hybrid|Resources") bool GetMiningLease(const FGuLiHybridObjectKey& Source,FGuLiHybridMiningLease& Lease) const;
    bool GetMiningReservationSummary(int32& Leases,uint64& Revision) const;
    bool IsMiningPositionAvailable(const FGuLiHybridObjectKey& Source,int32 Cluster,uint8 Sample) const;
    bool IsMiningNodeAvailable(const FGuLiHybridObjectKey& Source,uint32 Node) const;
    FGuLiHybridMiningReservationsChanged OnMiningReservationsChanged;
    UFUNCTION(BlueprintCallable, Category="Hybrid|Transactions") FGuid SubmitEconomy(const FGuLiHybridEconomyRequest& Request);
    FGuid SubmitEconomyNative(const FGuLiHybridEconomyRequest& Request, TFunction<void(const FGuLiHybridTransactionResult&)> Callback);
    UFUNCTION(BlueprintCallable, Category="Hybrid|Transactions") FGuid SubmitObjectOperation(const FGuLiHybridObjectRequest& Request);
    FGuid SubmitObjectOperationNative(const FGuLiHybridObjectRequest& Request,TFunction<void(const FGuLiHybridTransactionResult&)> Callback);
    FGuid SubmitCapability(const FGuLiHybridObjectKey& Source,FGuid Event,FName Rule,uint64 ExpectedRevision,
        TConstArrayView<uint8> Payload,EGuLiTeam Faction=EGuLiTeam::Unassigned);
    UFUNCTION(BlueprintCallable,Category="Hybrid|Transactions") FGuid SubmitUpgrade(const FGuLiHybridUpgradeRequest& Request);
    FGuid SubmitUpgradeNative(const FGuLiHybridUpgradeRequest& Request,TFunction<void(const FGuLiHybridTransactionResult&)> Callback);
    UFUNCTION(BlueprintPure, Category="Hybrid|Transactions") FGuLiResourceAmounts GetTeamBalance(EGuLiTeam Team) const;
    uint64 GetResourceRequestEpoch(const FGuLiHybridObjectKey& Source) const;
    bool GetCommittedReservation(const FGuLiHybridObjectKey& Source,uint64 Request,FGuid& Token) const;
    UPROPERTY(BlueprintAssignable, Category="Hybrid|Transactions") FGuLiHybridTransactionCompleted OnTransactionCompleted;
    UFUNCTION(BlueprintPure,Category="Hybrid|Transactions") bool FindTransactionResult(const FGuid& Event,FGuLiHybridTransactionResult& Out) const;
    UFUNCTION(BlueprintPure,Category="Hybrid|Transactions") bool IsRequestPending(FGuid Event) const;
    bool FindCommandResult(FGuid Event,FGuLiHybridCommandResult& Out) const;
    /** Current object's age during its callback; otherwise this client's battle clock. */
    uint64 GetLogicalTick() const;
    /** Receipt/freshness time on this client, independent of the active object and its owner history. */
    uint64 GetLocalLogicalTick() const;
    double GetLogicalSeconds() const;
    /** Shared pool/presentation time; never switches to an active producer's object age. */
    double GetLocalLogicalSeconds() const;
    bool IsBattlePaused() const;
    /** Only membership transitions pause battle; networking remains active. */
    bool CanPublishFacts() const;
    bool IsWorldReady() const;
    TSharedRef<FJsonObject> GetReviewEvidenceSnapshot() const;
    TSharedRef<FJsonObject> GetReviewCommandResult(const FGuid& Event) const;
    FString GetReviewEvidenceJson() const;
    bool SetReviewSimulationRate(double Rate);
    /** Replay a recorded resource request through its real owner, retaining the original body and logical time. */
    bool ReplayReviewBusinessRequest(FGuid Event,int32 Count,TSharedPtr<FJsonObject>& Evidence,FString& Error);
    const FString& GetFailure() const;
    void Suspend(const FString& Reason);
    FGuLiHybridWorldFailure OnFailure;
    FGuLiHybridWorldReady OnReady;
    // Object handoff or membership state has been applied. UI rebinds by request identity.
    FGuLiHybridWorldReady OnOwnershipStateApplied;
private:
    struct FState;
    struct FStateDeleter { void operator()(FState* Value) const; };
    TUniquePtr<FState,FStateDeleter> State;
    void HandleControl(const FGuLiHybridMessage& Message);
    bool HandlePeer(const FGuid& Sender, const FGuLiHybridMessage& Message);
    bool CanAdmitPeer(uint16 Kind,uint64 BodyBytes) const;
    void HandleFailure(const FString& Error);
    void TryWorldReady();
    void SynchronizeGameplayRoster();
    void PumpMembership();
    void ApplyQueryDelta(const GuLiNet::ClientQueryDelta& Delta);
    void HandlePreparedCommit(const GuLiNet::PreparedCommit& Commit);
    void ApplyPreparedCommit(const GuLiNet::PreparedCommit& Commit);
    void HandleReleasedCommit(const FGuLiHybridMessage& Message);
};

class FGuLiHybridHUDReadScope
{
    UGuLiHybridWorldSubsystem* World;
public:
    explicit FGuLiHybridHUDReadScope(UGuLiHybridWorldSubsystem* InWorld):World(InWorld) {if (World) World->BeginHUDQueryScope();}
    ~FGuLiHybridHUDReadScope() {if (World) World->EndHUDQueryScope();}
};
