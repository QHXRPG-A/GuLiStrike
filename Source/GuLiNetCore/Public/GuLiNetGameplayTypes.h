#pragma once
#include "GuLiNetRoomTypes.h"
#include "GuLiNetResourceLedger.h"
#include "GuLiNetMiningReservations.h"
#include "GuLiNetUpgrade.h"
#include "GuLiNetCapabilityState.h"
#include <deque>
#include <optional>

namespace GuLiNet
{
struct ObjectRecord
{
    Authority Grant;
    Health Vitals;
    std::string Definition;
    // Legacy numeric identity is assigned in the startup/spawn directory, never by a local arrival order.
    std::uint32_t Ordinal = 0;
    float FacingYaw = 0;
    double X = 0, Y = 0, Z = 0;
    Tick BornAt = 0;
    Tick DiedAt = UINT64_MAX;
    Tick LogicalAge = 0;
    bool Phased = false;
    bool Supplied = false;
    double ShieldRadius = 0;
    // Formal state belongs to this object's client owner.
    bool Completed = true;
    ObjectKey Parent;
    Revision RelationVersion = 0;
    // Immutable accepted work; independent from the lifetime of the source entity.
    Bytes FrozenWork;
    // Equipment history is independent of local assembly instances.
    UpgradeSelections Upgrades;
    CapabilityStates Capabilities;
    bool Materialized = true;
    friend bool operator==(const ObjectRecord&,const ObjectRecord&)=default;
};
enum class TransactionKind : std::uint8_t { Damage, Credit, Reserve, Commit, Refund, Spawn, Complete, Destroy, Relation, Extract, Deliver, Process, ClaimMiningPosition, ReleaseMiningPosition, ClaimMiningNode, AdmitWork, Upgrade, Capability };
constexpr bool IsResourceTransaction(TransactionKind Kind) { return Kind>=TransactionKind::Extract && Kind<=TransactionKind::Process; }
constexpr bool IsObjectTransaction(TransactionKind Kind) { return Kind>=TransactionKind::Spawn && Kind<=TransactionKind::Relation; }
constexpr bool IsMiningTransaction(TransactionKind Kind) { return Kind>=TransactionKind::ClaimMiningPosition && Kind<=TransactionKind::ClaimMiningNode; }
constexpr bool CanAdmitWork(TransactionKind Kind)
{ return IsResourceTransaction(Kind) || Kind==TransactionKind::Commit || Kind==TransactionKind::Refund || Kind==TransactionKind::Upgrade; }
inline constexpr const char* TransactionWorkDefinition="work/transaction";
inline constexpr std::uint32_t FrozenWorkSchema=2;
constexpr MiningReservationOperation MiningOperation(TransactionKind Kind)
{ return Kind==TransactionKind::ClaimMiningPosition ? MiningReservationOperation::ClaimPosition
    : Kind==TransactionKind::ClaimMiningNode ? MiningReservationOperation::ClaimNode : MiningReservationOperation::Release; }
enum class RelationKind : std::uint8_t { None, Supply, Phase, Parent };
enum class CommitStatus : std::uint8_t
{
    Committed, Duplicate, Deferred, BadRequest, WrongEpoch, Unauthorized, MissingObject,
    TargetDead, FriendlyFire, TooOld, InvalidRule, InsufficientResources, InvalidReservation, Paused
};
// Per effect/pulse target limit, independent from a client's pending receipt queue.
inline constexpr std::uint32_t MaximumEffectTargets = 4096;
struct Rule
{
    TransactionKind Kind = TransactionKind::Damage;
    std::string SourceDefinition;
    float Damage = 0;
    Resources Amount;
    Tick Cooldown = 0;
    Tick MaxEffectLifetime = 600;
    std::uint32_t MaxTargets = 1;
    // A rule fixes the resulting definition and numbers; the caller only reports a space fact.
    std::string TargetDefinition;
    ObjectKind SpawnKind = ObjectKind::None;
    float SpawnHealth = 0, SpawnShield = 0;
    double SpawnCost = 1, SpawnShieldRadius = 0;
    Tick MinimumAge = 0;
    std::uint32_t MaximumLiveChildren = 0;
    std::uint32_t MaximumFactionObjects = 0;
    Role SpawnControllerSeat = Role::Observer; // Observer means inherit the source controller for an effect.
    bool SpawnCompleted = true;
    RelationKind Relation = RelationKind::None;
    bool RelationValue = false;
    std::string UpgradeCatalog;
    // Generic prerequisite checked when the operation is admitted. Frozen work
    // retains that authorization if the source later dies or changes equipment.
    std::string RequiredCapabilityCatalog,RequiredCapabilityGroup,RequiredCapability;
    std::string Capability,CapabilityOperation;
    std::uint32_t CapabilityVersion=0;
    CapabilityStates SpawnCapabilities;
    UpgradeSelections SpawnUpgrades;
};
struct TransactionRequest
{
    Envelope Header;
    TransactionKind Kind = TransactionKind::Damage;
    ObjectKey Target;
    Id RootEvent;
    Tick StartedAt = 0;
    std::string RuleId;
    Id ReservationToken;
    EconomyKey ReservationKey;
    Team Faction = Team::None;
    // Space is an authenticated client's fact, not a cloud world query.
    double TargetX = 0, TargetY = 0, TargetZ = 0;
    float FacingYaw = 0;
    ObjectKey Related;
    Revision ExpectedRelationVersion = 0;
    std::uint32_t ResourceNode = 0;
    std::uint32_t MiningCluster=0,MiningTask=0;
    std::uint8_t MiningSample=0;
    Id MiningToken;
    // AdmitWork carries an authenticated original transaction. Other kinds leave it empty.
    Bytes Admission;
    std::string UpgradeNode;
    Revision ExpectedUpgradeVersion=0;
    Bytes CapabilityPayload;
    // Frozen by the attacking object's simulator before peer delivery.
    float DamageAmount = 0;
};
struct StateRequirement {ObjectKey Object;std::string Capability;Revision Version=0;};
struct CommitResult
{
    Id Event;
    TransactionKind Kind = TransactionKind::Damage;
    Team Faction = Team::None;
    CommitStatus Status = CommitStatus::BadRequest;
    Revision StateVersion = 0;
    ObjectKey StateDomain;
    ObjectKey Target;
    Health Vitals;
    Resources Balance;
    Reservation Reserved;
    DamageDelta Damage;
    Id DeathEvent;
    Id RewardEvent;
    std::vector<std::pair<ObjectKey, Health>> Barriers;
    // Absolute committed records, applied once without republishing a request.
    std::vector<ObjectRecord> Objects;
    // Immutable initial records, only populated at the atomic creation commit.
    // These are not inferred from an object's current lifecycle during restore.
    std::vector<ObjectRecord> Births;
    ResourceOutcome Resource;
    MiningReservationOutcome Mining;
    Revision MiningRevision=0;
    Id WorkEvent;
    TransactionKind WorkOperation=TransactionKind::Damage;
    // Settlement/rejection and work closure share one ledger revision.
    std::optional<ObjectRecord> FinalizedWork;
    std::string Capability;
    std::map<std::uint32_t,Team> TerritoryOwners;
    std::map<ObjectKey,Team> InventoryOwners;
    Revision ResourceRevision=0;
    std::vector<StateRequirement> RequiredStates;
};
struct FrozenWorkRecipe
{
    TransactionRequest Request;
    ResourceOutcome Resource;
    Reservation Reserved;
};
GULINETCORE_API Bytes EncodeTransaction(const TransactionRequest& Value);
GULINETCORE_API bool ValidRule(const Rule& Value);
GULINETCORE_API Id DeriveEconomyAccount(ObjectKey Source);
GULINETCORE_API bool DecodeTransaction(const Envelope& Header, std::span<const std::uint8_t> Bytes, TransactionRequest& Out);
GULINETCORE_API Bytes EncodeCommit(const CommitResult& Value);
// Compare the complete immutable decision, independently of whether this
// delivery is the first receipt or a replay. The wire status remains intact.
GULINETCORE_API Bytes EncodeCommitDecision(const CommitResult& Value);
GULINETCORE_API bool DecodeCommit(std::span<const std::uint8_t> Bytes, CommitResult& Out);
GULINETCORE_API void WriteObject(Writer& Out, const ObjectRecord& Value);
GULINETCORE_API bool ReadObject(Reader& In, ObjectRecord& Value);
GULINETCORE_API Bytes EncodeFrozenWork(const FrozenWorkRecipe& Value);
GULINETCORE_API bool DecodeFrozenWork(const ObjectRecord& Work,FrozenWorkRecipe& Out);
}
