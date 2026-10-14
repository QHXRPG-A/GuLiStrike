#pragma once
#include <memory>
#include "GuLiNetProtocol.h"
#include <deque>
#include <functional>

namespace GuLiNet
{
enum class Commodity : std::uint8_t { Blue, Red };
// Capabilities are independent of the UE object family. One definition may
// carry cargo and process deliveries through separate registered inventories.
enum class ResourceInventoryKind : std::uint8_t { Cargo=1, Processor=2 };
struct OreDefinition
{
    std::uint32_t Cluster=0, Territory=0;
    Commodity Type=Commodity::Blue;
    std::uint8_t Initial=0;
    friend bool operator==(const OreDefinition&,const OreDefinition&)=default;
};
struct MinerInventory
{
    Team Faction=Team::None;
    std::uint32_t Capacity=0;
    Resources Cargo;
    Tick ExtractionPeriod=60, NextExtraction=0;
    Revision Version=1;
};
struct RawDelivery
{
    Id Event;
    ObjectKey Source;
    Team Beneficiary=Team::None;
    Resources Amount;
};
struct FactoryInventory
{
    Team Faction=Team::None;
    std::uint32_t Capacity=0;
    Tick ProcessingPeriod=30, NextProcessing=0;
    Revision Version=1;
    std::deque<RawDelivery> Queue;
    Resources Processed;
};
enum class ResourceOperation : std::uint8_t { Extract, Deliver, Process };
enum class ResourceStatus : std::uint8_t
{ Committed, Duplicate, MissingSource, Invalid, Conflict, Depleted, Forbidden, Full, TooEarly, Empty, Deferred };
struct ResourceOutcome
{
    Id Event;
    ResourceOperation Operation=ResourceOperation::Extract;
    ResourceStatus Status=ResourceStatus::Invalid;
    Revision Version=0;
    ObjectKey Source, Target;
    std::uint32_t Node=0;
    std::uint8_t Remaining=0;
    Resources Cargo, Credit, RawAmount;
    Team Beneficiary=Team::None;
    // Delivery provenance is independent of the miner's later controller/life.
    Id Delivery;
    bool IsCommitted() const { return Status==ResourceStatus::Committed || Status==ResourceStatus::Duplicate; }
};
struct ResourceDecision
{
    Bytes Fingerprint;
    ResourceOutcome Outcome;
};
struct ResourceLedgerState
{
    bool Configured=false;
    Revision Version=0;
    std::map<std::uint32_t,OreDefinition> Nodes;
    std::map<std::uint32_t,std::uint8_t> Remaining;
    std::vector<Team> Owners;
    std::map<ObjectKey,MinerInventory> Miners;
    std::map<ObjectKey,FactoryInventory> Factories;
    Resources Processed;
    std::map<Id,ResourceDecision> Decisions;
};
struct ResourceDirectory
{
    bool Configured=false;
    Revision Version=0;
    std::map<std::uint32_t,std::uint8_t> Remaining;
    std::vector<Team> Owners;
};

/** Client-owned, data-only extraction/cargo/FIFO settlement. Callers authenticate
 * object grants and check geometry before entry. A receipt, never a send, moves
 * the raw inventory. Credit is committed before its matching queue unit leaves.
 * Inventory fragments move with their owning object. */
class GULINETCORE_API ResourceLedger
{
public:
    bool Configure(std::map<std::uint32_t,OreDefinition> Nodes,std::vector<Team> Owners);
    bool RegisterMiner(ObjectKey Key,Team Faction,std::uint32_t Capacity,Tick Period=60);
    bool RegisterFactory(ObjectKey Key,Team Faction,std::uint32_t Capacity,Tick Period=30);
    bool SetTerritoryOwner(std::uint32_t Territory,Team Owner);
    bool SetFactoryOwner(ObjectKey Factory,Team Owner);
    // Change all inventories declared by this object together. FIFO delivery
    // beneficiaries remain frozen to the faction admitted with the delivery.
    bool SetInventoryOwner(ObjectKey Object,Team Owner);
    ResourceOutcome Preview(ResourceOperation Operation,ObjectKey Source,ObjectKey Target,std::uint32_t Node,Id Event,Tick At,
        const ResourceOutcome* Admitted=nullptr) const;
    ResourceOutcome Extract(ObjectKey Miner,std::uint32_t Node,Id Event,Tick At,const ResourceOutcome* Admitted=nullptr);
    ResourceOutcome Deliver(ObjectKey Miner,ObjectKey Factory,Id Event,Tick At,const ResourceOutcome* Admitted=nullptr);
    ResourceOutcome Process(ObjectKey Factory,Id Event,Tick At,
        const std::function<bool(Team,const Resources&)>& CommitCredit,const ResourceOutcome* Admitted=nullptr);
    const ResourceLedgerState& Snapshot() const { return *Storage; }
    bool Restore(ResourceLedgerState Value);
    static std::uint64_t Queued(const FactoryInventory& Factory);
private:
    ResourceOutcome Begin(ResourceOperation Operation,ObjectKey Source,ObjectKey Target,
        std::uint32_t Node,Id Event,Tick At,Bytes& Fingerprint) const;
    ResourceOutcome Finish(ResourceOutcome Value,Bytes Fingerprint,bool Durable=true);
    // Transaction plans share their read view. Only a resource mutation makes
    // a private copy; unrelated combat/lifecycle plans never copy the ore map.
    ResourceLedgerState& WriteState();
    std::shared_ptr<ResourceLedgerState> Storage=std::make_shared<ResourceLedgerState>();
};
GULINETCORE_API bool ValidResourceLedger(const ResourceLedgerState& State);
GULINETCORE_API Bytes EncodeResourceOutcome(const ResourceOutcome& Value);
GULINETCORE_API bool DecodeResourceOutcome(std::span<const std::uint8_t> Bytes,ResourceOutcome& Out);
GULINETCORE_API Bytes EncodeResourceLedger(const ResourceLedgerState& State);
GULINETCORE_API bool DecodeResourceLedger(std::span<const std::uint8_t> Bytes,ResourceLedgerState& Out);
// Public ore/ownership query data. Private cargo, FIFO deliveries and decisions
// remain in the room ledger and simulation adapters, never this directory.
GULINETCORE_API Bytes EncodeResourceDirectory(const ResourceLedgerState& State);
GULINETCORE_API bool DecodeResourceDirectory(std::span<const std::uint8_t> Bytes,
    const ResourceLedgerState& Catalog,ResourceDirectory& Out);
}
