#pragma once
#include "GuLiNetGameplayTypes.h"
#include <optional>

namespace GuLiNet
{
struct RuntimeManifest;
struct StateKey
{
    ObjectKey Object;
    std::string Capability;
    friend bool operator==(const StateKey&,const StateKey&)=default;
    friend bool operator<(const StateKey& A,const StateKey& B) {return std::tie(A.Object,A.Capability)<std::tie(B.Object,B.Capability);}
};
struct StateFragment
{
    StateKey Key;
    std::uint32_t Schema=1;
    Revision Version=0;
    Bytes Value;
};
struct StateMutation
{
    StateFragment Before,After;
    Revision OwnerVersion=0;
};
struct OwnerWork
{
    Id Event,Initiator,Proposal;
    bool Continuation=false;
    ObjectKey Source;
    TransactionRequest Request;
    CommitResult Result;
    std::vector<StateMutation> Changes;
};
GULINETCORE_API ObjectKey TeamAccountObject(Team Faction);
GULINETCORE_API ObjectKey ResourceNodeObject(std::uint32_t Node);
GULINETCORE_API ObjectKey MiningClusterObject(std::uint32_t Cluster);
GULINETCORE_API ObjectKey TerritoryStateObject(std::uint32_t Territory);
GULINETCORE_API Bytes EncodeOwnerWork(const OwnerWork& Work);
GULINETCORE_API bool DecodeOwnerWork(std::span<const std::uint8_t> Bytes,OwnerWork& Work);
GULINETCORE_API void WriteStateFragment(Writer& Out,const StateFragment& Value);
GULINETCORE_API bool ReadStateFragment(Reader& In,StateFragment& Value);

// Client-side numeric rules. A plan is evaluated on an isolated value copy;
// only the owners of its changed capability fragments may commit those writes.
// The coordinator has no instance of this class.
class GULINETCORE_API OwnerState
{
public:
    OwnerState(Id Match,Id Player,Revision Epoch=1);
    bool Configure(const RuntimeManifest& Manifest,std::string& Error);
    bool ConfigureUpgradeCatalogs(const std::map<std::string,CompiledUpgradeCatalog>& Catalogs);
    bool ConfigureResourceCatalog(std::map<std::uint32_t,OreDefinition> Nodes,std::vector<Team> Owners);
    bool RegisterResourceActorProfile(ResourceInventoryKind Capability,ObjectKind Kind,const std::string& Definition,std::uint32_t Capacity,Tick Period);
    bool RegisterResourceMiner(ObjectKey Key,std::uint32_t Capacity,Tick Period=60);
    bool RegisterResourceFactory(ObjectKey Key,std::uint32_t Capacity,Tick Period=30);
    bool SetRules(std::map<std::string,Rule> Values);
    bool BeginEconomy(Resources Red,Resources Blue);
    bool Install(const ObjectRecord& Record);
    void SetMembers(std::map<Id,Member> Members) {Players=std::move(Members);}
    bool SetAuthority(const Authority& Grant);
    void SetLogicalAge(ObjectKey Object,Tick Age);
    Id Owner(ObjectKey Object) const;
    Id MutationOwner(const OwnerWork& Work,const StateMutation& Mutation) const;
    bool Busy(ObjectKey Object) const;
    ObjectKey ExecutionDomain(const TransactionRequest& Request) const;
    bool FreezeDamage(TransactionRequest& Request) const;
    bool Plan(Id AuthenticatedSender,const TransactionRequest& Request,OwnerWork& Out);
    bool Prepare(Id Planner,const OwnerWork& Work);
    bool Commit(Id Planner,const OwnerWork& Work);
    void Cancel(Id Planner,Id Event,Id Proposal);
    bool Resume(OwnerWork& Work);
    bool Import(Id Owner,const std::vector<StateFragment>& Fragments,bool Handoff=false,
        const std::map<ObjectKey,Authority>* AdmittedBirths=nullptr);
    std::vector<StateFragment> Export(std::span<const ObjectKey> Objects) const;
    std::vector<StateFragment> Published(const OwnerWork& Work) const;
    std::optional<CommitResult> Result(Id Event) const;
    Revision Version(const StateKey& Key) const;
    const std::map<ObjectKey,ObjectRecord>& Objects() const {return Directory;}
    const Economy& TeamEconomy() const {return EconomyLedger;}
    const ResourceLedger& WorldResources() const {return ResourceLedgerValue;}
    const MiningReservations& MiningPositions() const {return MiningReservationsValue;}
private:
    bool IsLiveMember(Id Player) const;
    bool Authorize(Id Sender,const Envelope& Header,Domain Field,bool AllowDead=false) const;
    CommitResult Evaluate(Id Sender,const TransactionRequest& Request,Id ReservationToken);
    CommitResult AdmitWork(Id Sender,const TransactionRequest& Request);
    bool CloseWork(ObjectRecord& Work,CommitResult& Result,Tick At);
    bool Remember(const TransactionRequest& Request,const CommitResult& Result);
    Id AttackIdentity(const TransactionRequest& Request) const;
    ObjectKey AttackDomain(const TransactionRequest& Request) const;
    bool UpgradeStateAllowed(const ObjectRecord& Object) const;
    bool RuleCapabilityAllowed(const ObjectRecord& Object,const Rule& Value) const;
    bool PrepareResourceActor(const ObjectRecord& Object,bool Created,std::optional<ResourceLedger>& Prepared) const;
    void Abort(std::string Reason) {FailureReason=std::move(Reason);}
    Revision NextDecisionVersion() const {return Version({DecisionDomain,"object"})+1;}
    Bytes CaptureFragment(const StateKey& Key) const;
    bool ApplyFragment(const StateFragment& Fragment);
    std::set<StateKey> FragmentKeys(std::span<const ObjectKey> Objects) const;
    std::set<StateKey> ChangedKeys(const TransactionRequest& Request,const CommitResult& Result) const;
    bool RebuildWorkIndex();
    OwnerState Stage(const TransactionRequest* Request=nullptr,const std::vector<StateFragment>* Fragments=nullptr) const;
    const ObjectRecord* FindRecord(ObjectKey Key) const;
    ObjectRecord* EditRecord(ObjectKey Key);
    void IndexRecord(const ObjectRecord& Record);
    const OwnerState& IndexedSource() const {return StageSource?StageSource->IndexedSource():*this;}
    const std::map<ObjectKey,ObjectRecord>& PreviousObjects() const {return StageSource?StageSource->PreviousObjects():Directory;}
    bool ApplyValidated(const std::vector<StateFragment>& Fragments);
    Id Match,Player;
    Revision MatchEpoch=1;
    ObjectKey DecisionDomain;
    const OwnerState* StageSource=nullptr;
    std::string FailureReason;
    std::map<Id,Member> Players;
    std::map<ObjectKey,ObjectRecord> Directory;
    // Retired records remain available to in-flight work and deduplication;
    // frequent simulation queries must not traverse that historical directory.
    std::set<ObjectKey> LivingObjects,ShieldObjects;
    std::map<ObjectKind,std::uint32_t> MaximumOrdinals;
    std::map<std::string,Rule> Rules;
    std::map<std::string,CompiledUpgradeCatalog> UpgradeCatalogs;
    Economy EconomyLedger;
    ResourceLedger ResourceLedgerValue;
    MiningReservations MiningReservationsValue;
    std::map<std::tuple<ObjectKind,std::string,ResourceInventoryKind>,std::pair<std::uint32_t,Tick>> ResourceActorProfiles;
    std::map<Id,CommitResult> Results;
    std::map<Id,Bytes> RequestBodies;
    std::map<Id,ObjectKey> ResultDomains;
    struct Attack
    {
        ObjectKey Source;std::string RuleId;Tick Started=0;std::set<ObjectKey> Targets;
        float Damage=0;Tick Cooldown=0,Lifetime=0;std::uint32_t MaximumTargets=1;
    };
    std::map<Id,Attack> Attacks;
    std::map<std::pair<ObjectKey,std::string>,Tick> LastAttack;
    std::map<ObjectKey,ObjectKey> ActiveWorks;
    std::map<Id,ObjectKey> ActiveWorkEvents;
    std::map<std::uint32_t,std::uint32_t> HeldOre;
    std::map<ObjectKey,std::uint64_t> HeldFactoryCapacity;
    std::map<StateKey,Revision> Versions;
    std::map<StateKey,Id> Locks;
    std::map<Id,std::pair<Id,Bytes>> Prepared;
    std::set<std::pair<Id,Id>> Cancelled;
    std::uint64_t NextProposal=1;
    std::set<Id> Finished;
    std::map<Team,std::set<EconomyKey>> FinalizedAccounts;
    std::map<ObjectKind,std::uint32_t> NextOrdinal;
    std::map<std::string,OwnershipDefinition> OwnershipDefinitions;
};
}
