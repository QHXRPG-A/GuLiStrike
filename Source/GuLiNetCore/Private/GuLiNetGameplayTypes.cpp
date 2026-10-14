#include "GuLiNetGameplayTypes.h"
#include "GuLiNetHash.h"
#include "GuLiNetCapability.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace GuLiNet
{
namespace
{
void WriteReservation(Writer& W, const Reservation& V)
{
    W.Identity(V.Token); W.Identity(V.Key.Account); W.U64(V.Key.Request);
    W.U8(static_cast<std::uint8_t>(V.Faction)); WriteResources(W, V.Cost);
}
bool ReadReservation(Reader& R, Reservation& V)
{
    std::uint8_t TeamValue;
    if (!R.Identity(V.Token) || !R.Identity(V.Key.Account) || !R.U64(V.Key.Request)
        || !R.U8(TeamValue) || TeamValue > 2 || !ReadResources(R, V.Cost)) return false;
    V.Faction = static_cast<Team>(TeamValue); return true;
}
bool ReadCount(Reader& R, std::uint32_t& Count, std::uint32_t Limit = 100000)
{ return R.U32(Count) && Count <= Limit; }

}
Id DeriveEconomyAccount(ObjectKey Source)
{
    if (!Source.Valid()) return {};
    Writer W; W.Text("GuLiEconomyAccount/v1"); W.Key(Source);
    Id Account; Id::Parse(Sha256(W.Take()).substr(0,32),Account); return Account;
}
bool ValidRule(const Rule& V)
{
    const auto Name=[](const std::string& S) { return !S.empty() && S.size()<=256 && S.find('\0')==std::string::npos; };
    if (V.Kind>TransactionKind::Capability || V.Kind==TransactionKind::AdmitWork || !Name(V.SourceDefinition) || !std::isfinite(V.Damage) || V.Damage<0
        || !V.Amount.Nonnegative() || !V.MaxTargets || V.MaxTargets>MaximumEffectTargets
        || V.Cooldown>TicksPerSecond*86400 || V.MaxEffectLifetime>TicksPerSecond*86400
        || V.MinimumAge>TicksPerSecond*86400 || V.MaximumLiveChildren>100000 || V.MaximumFactionObjects>100000
        || V.SpawnControllerSeat>Role::Observer || V.Relation>RelationKind::Parent
        || !std::isfinite(V.SpawnHealth) || V.SpawnHealth<0 || !std::isfinite(V.SpawnShield) || V.SpawnShield<0
        || !std::isfinite(V.SpawnCost) || V.SpawnCost<0 || !std::isfinite(V.SpawnShieldRadius) || V.SpawnShieldRadius<0) return false;
    if (V.RequiredCapabilityCatalog.empty()!=V.RequiredCapability.empty() || V.RequiredCapabilityCatalog.empty()!=V.RequiredCapabilityGroup.empty()
        || (!V.RequiredCapabilityCatalog.empty() && (!Name(V.RequiredCapabilityCatalog) || !Name(V.RequiredCapabilityGroup) || !Name(V.RequiredCapability)))) return false;
    if (V.Kind!=TransactionKind::Capability && (!V.Capability.empty() || !V.CapabilityOperation.empty() || V.CapabilityVersion)) return false;
    if (V.Kind!=TransactionKind::Spawn && V.Kind!=TransactionKind::Capability && (!V.SpawnCapabilities.empty() || !V.SpawnUpgrades.empty())) return false;
    if (V.SpawnCapabilities.size()>MaximumObjectCapabilities || !ValidUpgradeSelections(V.SpawnUpgrades)) return false;
    if (V.Kind==TransactionKind::Upgrade)
        return Name(V.UpgradeCatalog) && V.TargetDefinition==V.SourceDefinition && V.MaxTargets==1
            && V.Relation==RelationKind::None && !V.Amount.Positive() && V.Damage==0 && V.SpawnKind==ObjectKind::None
            && V.SpawnHealth==0 && V.SpawnShield==0 && V.SpawnShieldRadius==0;
    if (!V.UpgradeCatalog.empty()) return false;
    if (V.Kind==TransactionKind::Capability)
    {
        const auto* C=FindCapability(V.Capability);
        return C && C->Version==V.CapabilityVersion && Name(V.CapabilityOperation) && C->ValidateRule(V);
    }
    if (!V.Capability.empty() || !V.CapabilityOperation.empty() || V.CapabilityVersion) return false;
    if (V.Kind==TransactionKind::Spawn)
    {
        if (!Name(V.TargetDefinition) || V.SpawnKind==ObjectKind::None || V.SpawnKind>ObjectKind::BusinessWork
            || V.Relation!=RelationKind::None || V.MaxTargets!=1) return false;
        // Representation kind does not prescribe health or construction state.
        // The authored capability contract validates those gameplay requirements.
        return V.SpawnHealth>0 || (V.SpawnHealth==0 && V.SpawnShield==0);
    }
    if (V.SpawnKind!=ObjectKind::None || V.SpawnHealth!=0 || V.SpawnShield!=0 || V.SpawnShieldRadius!=0) return false;
    if (IsResourceTransaction(V.Kind))
        return V.MaxTargets==1 && V.Relation==RelationKind::None && !V.Amount.Positive() && V.Damage==0
            && (V.Kind==TransactionKind::Deliver ? Name(V.TargetDefinition) : V.TargetDefinition.empty());
    if (IsMiningTransaction(V.Kind))
        return V.MaxTargets==1 && V.Relation==RelationKind::None && V.TargetDefinition.empty() && !V.Amount.Positive() && V.Damage==0;
    if (V.Kind>=TransactionKind::Complete)
        return V.TargetDefinition==V.SourceDefinition && V.MaxTargets==1
            && ((V.Kind==TransactionKind::Relation)==(V.Relation!=RelationKind::None));
    return V.TargetDefinition.empty() && V.Relation==RelationKind::None;
}
Bytes EncodeTransaction(const TransactionRequest& V)
{
    Writer W; W.U8(static_cast<std::uint8_t>(V.Kind)); W.Key(V.Target); W.Identity(V.RootEvent);
    W.U64(V.StartedAt); W.Text(V.RuleId); W.Identity(V.ReservationToken);
    W.Identity(V.ReservationKey.Account); W.U64(V.ReservationKey.Request); W.U8(static_cast<std::uint8_t>(V.Faction));
    W.F64(V.TargetX); W.F64(V.TargetY); W.F64(V.TargetZ); W.F32(V.FacingYaw);
    W.Key(V.Related); W.U64(V.ExpectedRelationVersion); W.U32(V.ResourceNode);
    W.U32(V.MiningCluster); W.U8(V.MiningSample); W.U32(V.MiningTask); W.Identity(V.MiningToken); W.Blob(V.Admission);
    W.Text(V.UpgradeNode);W.U64(V.ExpectedUpgradeVersion);W.Blob(V.CapabilityPayload);W.F32(V.DamageAmount);return W.Take();
}
bool DecodeTransaction(const Envelope& Header, std::span<const std::uint8_t> BytesValue, TransactionRequest& Out)
{
    Reader R(BytesValue); TransactionRequest V; std::uint8_t Kind, Faction;
    if (!R.U8(Kind) || Kind > static_cast<std::uint8_t>(TransactionKind::Capability) || !R.Key(V.Target) || !R.Identity(V.RootEvent) || !R.U64(V.StartedAt)
        || !R.Text(V.RuleId, 256) || !R.Identity(V.ReservationToken) || !R.Identity(V.ReservationKey.Account)
        || !R.U64(V.ReservationKey.Request) || !R.U8(Faction) || Faction > 2
        || !R.F64(V.TargetX) || !R.F64(V.TargetY) || !R.F64(V.TargetZ) || !R.F32(V.FacingYaw)
        || !R.Key(V.Related) || !R.U64(V.ExpectedRelationVersion) || !R.U32(V.ResourceNode)
        || !R.U32(V.MiningCluster) || !R.U8(V.MiningSample) || !R.U32(V.MiningTask) || !R.Identity(V.MiningToken)
        || !R.Blob(V.Admission,2048) || !R.Text(V.UpgradeNode,192) || !R.U64(V.ExpectedUpgradeVersion)
        || !R.Blob(V.CapabilityPayload,MaximumCapabilityBytes)
        || !R.F32(V.DamageAmount) || !std::isfinite(V.DamageAmount) || V.DamageAmount<0
        || (Kind!=static_cast<std::uint8_t>(TransactionKind::Damage) && V.DamageAmount!=0)
        || (Kind!=static_cast<std::uint8_t>(TransactionKind::Capability) && !V.CapabilityPayload.empty())
        || (Kind!=static_cast<std::uint8_t>(TransactionKind::AdmitWork) && !V.Admission.empty())
        || (Kind!=static_cast<std::uint8_t>(TransactionKind::Upgrade) && (!V.UpgradeNode.empty() || V.ExpectedUpgradeVersion)) || !R.Done()) return false;
    V.Header = Header; V.Kind = static_cast<TransactionKind>(Kind); V.Faction = static_cast<Team>(Faction);
    Out = std::move(V); return true;
}
namespace
{
Bytes EncodeCommitWithStatus(const CommitResult& V,CommitStatus Status)
{
    Writer W; W.Identity(V.Event); W.U8(static_cast<std::uint8_t>(V.Kind)); W.U8(static_cast<std::uint8_t>(V.Faction));
    W.U8(static_cast<std::uint8_t>(Status)); W.U64(V.StateVersion);W.Key(V.StateDomain);
    W.Key(V.Target); W.U8(ValidHealth(V.Vitals) ? 1 : 0); if (ValidHealth(V.Vitals)) WriteHealth(W, V.Vitals);
    WriteResources(W, V.Balance); WriteReservation(W, V.Reserved);
    W.F32(V.Damage.Applied); W.F32(V.Damage.Absorbed); W.U8(V.Damage.Killed ? 1 : 0);
    W.Identity(V.DeathEvent); W.Identity(V.RewardEvent); W.U32(static_cast<std::uint32_t>(V.Barriers.size()));
    for (const auto& [Key, Vitals] : V.Barriers) { W.Key(Key); WriteHealth(W, Vitals); }
    W.U32(static_cast<std::uint32_t>(V.Objects.size()));
    for (const auto& Object : V.Objects) WriteObject(W,Object);
    W.U8(V.Resource.Event.Valid()?1:0); if (V.Resource.Event.Valid()) W.Blob(EncodeResourceOutcome(V.Resource));
    W.U8(V.Mining.Event.Valid()?1:0); if (V.Mining.Event.Valid()) W.Blob(EncodeMiningReservationOutcome(V.Mining)); W.U64(V.MiningRevision);
    W.Identity(V.WorkEvent); W.U8(static_cast<std::uint8_t>(V.WorkOperation));
    W.U8(V.FinalizedWork?1:0);if (V.FinalizedWork) WriteObject(W,*V.FinalizedWork);
    W.Text(V.Capability);
    W.U64(V.ResourceRevision);W.U32(static_cast<std::uint32_t>(V.TerritoryOwners.size()));
    for (const auto& [Key,Owner]:V.TerritoryOwners) {W.U32(Key);W.U8(static_cast<std::uint8_t>(Owner));}
    W.U32(static_cast<std::uint32_t>(V.InventoryOwners.size()));
    for (const auto& [Key,Owner]:V.InventoryOwners) {W.Key(Key);W.U8(static_cast<std::uint8_t>(Owner));}
    W.U32(static_cast<std::uint32_t>(V.Births.size()));for (const auto& B:V.Births) WriteObject(W,B);
    W.U32(static_cast<std::uint32_t>(V.RequiredStates.size()));
    for (const auto& S:V.RequiredStates) {W.Key(S.Object);W.Text(S.Capability);W.U64(S.Version);}
    return W.Take();
}
}
Bytes EncodeCommit(const CommitResult& V) {return EncodeCommitWithStatus(V,V.Status);}
Bytes EncodeCommitDecision(const CommitResult& V)
{return EncodeCommitWithStatus(V,V.Status==CommitStatus::Duplicate?CommitStatus::Committed:V.Status);}
bool DecodeCommit(std::span<const std::uint8_t> BytesValue, CommitResult& Out)
{
    Reader R(BytesValue); CommitResult V; std::uint8_t Status, Kind, Faction, HasHealth, Killed; std::uint32_t Count;
    if (!R.Identity(V.Event) || !R.U8(Kind) || Kind>static_cast<std::uint8_t>(TransactionKind::Capability)
        || !R.U8(Faction) || Faction>2 || !R.U8(Status) || Status > static_cast<std::uint8_t>(CommitStatus::Paused)
        || !R.U64(V.StateVersion) || !R.Key(V.StateDomain) || !R.Key(V.Target) || !R.U8(HasHealth) || HasHealth > 1
        || (HasHealth && !ReadHealth(R, V.Vitals)) || !ReadResources(R, V.Balance) || !ReadReservation(R, V.Reserved)
        || !R.F32(V.Damage.Applied) || !R.F32(V.Damage.Absorbed) || !R.U8(Killed) || Killed > 1
        || !R.Identity(V.DeathEvent) || !R.Identity(V.RewardEvent) || !ReadCount(R, Count, 4096)) return false;
    V.Kind=static_cast<TransactionKind>(Kind); V.Faction=static_cast<Team>(Faction);
    V.Status = static_cast<CommitStatus>(Status); V.Damage.Killed = Killed != 0;
    for (std::uint32_t I = 0; I < Count; ++I)
    { ObjectKey Key; Health H; if (!R.Key(Key) || !ReadHealth(R, H)) return false; V.Barriers.emplace_back(Key, H); }
    if (!ReadCount(R,Count,MaximumCapabilityObjects)) return false;
    std::set<ObjectKey> Unique;
    for (std::uint32_t I=0; I<Count; ++I)
    {
        ObjectRecord Object;
        if (!ReadObject(R,Object) || !Object.Grant.Object.Valid() || !Unique.insert(Object.Grant.Object).second) return false;
        V.Objects.push_back(std::move(Object));
    }
    std::uint8_t HasResource;
    if (!R.U8(HasResource) || HasResource>1) return false;
    if (IsResourceTransaction(V.Kind) && (V.Status==CommitStatus::Committed || V.Status==CommitStatus::Duplicate) && !HasResource) return false;
    if (HasResource)
    {
        Bytes Body;
        if (!IsResourceTransaction(V.Kind) || !R.Blob(Body,1024) || !DecodeResourceOutcome(Body,V.Resource)
            || V.Resource.Event!=V.Event || !V.Resource.IsCommitted()
            || (V.Status!=CommitStatus::Committed && V.Status!=CommitStatus::Duplicate)) return false;
    }
    std::uint8_t HasMining=0;
    if (!R.U8(HasMining) || HasMining>1) return false;
    if (IsMiningTransaction(V.Kind) && (V.Status==CommitStatus::Committed || V.Status==CommitStatus::Duplicate) && !HasMining) return false;
    if (HasMining)
    {
        Bytes Body;
        if (!IsMiningTransaction(V.Kind) || !R.Blob(Body,512) || !DecodeMiningReservationOutcome(Body,V.Mining)
            || V.Mining.Event!=V.Event || V.Mining.Operation!=MiningOperation(V.Kind)
            || V.Mining.IsCommitted()!=(V.Status==CommitStatus::Committed || V.Status==CommitStatus::Duplicate)
            || V.Mining.Status==MiningReservationStatus::Deferred || V.Mining.Status==MiningReservationStatus::Conflict) return false;
    }
    std::uint8_t WorkOperation=0,HasWork=0;
    if (!R.U64(V.MiningRevision) || (HasMining && V.Mining.Version>V.MiningRevision)
        || !R.Identity(V.WorkEvent) || !R.U8(WorkOperation) || WorkOperation>static_cast<std::uint8_t>(TransactionKind::Upgrade)
        || !R.U8(HasWork) || HasWork>1) return false;
    V.WorkOperation=static_cast<TransactionKind>(WorkOperation);
    if (V.Kind==TransactionKind::AdmitWork ? ((V.Status==CommitStatus::Committed || V.Status==CommitStatus::Duplicate)
        && (!V.WorkEvent.Valid() || !CanAdmitWork(V.WorkOperation))) : V.WorkEvent.Valid()) return false;
    if (HasWork)
    {
        ObjectRecord Work;FrozenWorkRecipe Recipe;
        if (!ReadObject(R,Work) || !DecodeFrozenWork(Work,Recipe) || Work.Grant.State!=Life::Destroyed
            || V.Kind==TransactionKind::AdmitWork || Recipe.Request.Header.Event!=V.Event || Recipe.Request.Kind!=V.Kind
            || Recipe.Request.Target!=V.Target || !V.StateVersion || V.Status==CommitStatus::Deferred || V.Status==CommitStatus::Paused) return false;
        V.FinalizedWork=std::move(Work);
    }
    if (!R.Text(V.Capability,192)
        || (V.Kind!=TransactionKind::Capability && !V.Capability.empty())
        || (V.Kind==TransactionKind::Capability && (V.Status==CommitStatus::Committed || V.Status==CommitStatus::Duplicate)
            && !FindCapability(V.Capability))) return false;
    if (!R.U64(V.ResourceRevision) || !ReadCount(R,Count,256)) return false;
    for (std::uint32_t I=0;I<Count;++I)
    {
        std::uint32_t Key=0;std::uint8_t Owner=0;
        if (!R.U32(Key) || !R.U8(Owner) || Owner>2 || !V.TerritoryOwners.emplace(Key,static_cast<Team>(Owner)).second) return false;
    }
    if (!ReadCount(R,Count,256)) return false;
    for (std::uint32_t I=0;I<Count;++I)
    {
        ObjectKey Key;std::uint8_t Owner=0;
        if (!R.Key(Key) || !Key.Valid() || !R.U8(Owner) || Owner<1 || Owner>2 || !V.InventoryOwners.emplace(Key,static_cast<Team>(Owner)).second) return false;
    }
    if (!ReadCount(R,Count,MaximumCapabilityObjects)) return false;
    std::set<ObjectKey> Born;
    for (std::uint32_t I=0;I<Count;++I)
    {
        ObjectRecord B;
        if (!ReadObject(R,B) || !Born.insert(B.Grant.Object).second || !B.Grant.SourceLease.Valid()
            || B.Grant.SourceLease!=V.Event || !B.Parent.Valid() || !V.StateVersion
            || (V.Status!=CommitStatus::Committed && V.Status!=CommitStatus::Duplicate)) return false;
        const auto Initial=std::find_if(V.Objects.begin(),V.Objects.end(),[&](const auto& O){return O.Grant.Object==B.Grant.Object;});
        if (Initial==V.Objects.end()) return false;
        Writer A,C;WriteObject(A,B);WriteObject(C,*Initial);if (A.Take()!=C.Take()) return false;
        V.Births.push_back(std::move(B));
    }
    if (!ReadCount(R,Count,4096)) return false;
    for (std::uint32_t I=0;I<Count;++I)
    {
        StateRequirement S;
        if (!R.Key(S.Object) || !S.Object.Valid() || !R.Text(S.Capability,512) || S.Capability.empty() || !R.U64(S.Version)) return false;
        V.RequiredStates.push_back(std::move(S));
    }
    if (!R.Done() || ((!V.TerritoryOwners.empty() || !V.InventoryOwners.empty())
        && (V.Kind!=TransactionKind::Capability || !V.ResourceRevision
            || (V.Status!=CommitStatus::Committed && V.Status!=CommitStatus::Duplicate)))) return false;
    Out = std::move(V); return true;
}
void WriteObject(Writer& W, const ObjectRecord& V)
{
    WriteAuthority(W, V.Grant); W.U8(ValidHealth(V.Vitals) ? 1 : 0);
    if (ValidHealth(V.Vitals)) WriteHealth(W, V.Vitals);
    W.Text(V.Definition); W.U32(V.Ordinal); W.F32(V.FacingYaw);
    W.F64(V.X); W.F64(V.Y); W.F64(V.Z); W.U64(V.BornAt); W.U64(V.DiedAt);W.U64(V.LogicalAge);
    W.U8(V.Phased ? 1 : 0); W.U8(V.Supplied ? 1 : 0); W.F64(V.ShieldRadius);
    W.U8(V.Completed ? 1 : 0); W.Key(V.Parent); W.U64(V.RelationVersion);W.Blob(V.FrozenWork);WriteUpgradeSelections(W,V.Upgrades);WriteCapabilityStates(W,V.Capabilities);W.U8(V.Materialized?1:0);
}
bool ReadObject(Reader& R, ObjectRecord& V)
{
    std::uint8_t HasHealth, Phased, Supplied, Completed, Materialized;
    if (!ReadAuthority(R, V.Grant) || !R.U8(HasHealth) || HasHealth > 1 || (HasHealth && !ReadHealth(R, V.Vitals))
        || !R.Text(V.Definition, 256) || !R.U32(V.Ordinal) || !R.F32(V.FacingYaw)
        || !std::isfinite(V.FacingYaw) || !R.F64(V.X) || !R.F64(V.Y) || !R.F64(V.Z)
        || !std::isfinite(V.X) || !std::isfinite(V.Y) || !std::isfinite(V.Z)
        || !R.U64(V.BornAt) || !R.U64(V.DiedAt) || !R.U64(V.LogicalAge) || !R.U8(Phased) || Phased > 1
        || !R.U8(Supplied) || Supplied > 1 || !R.F64(V.ShieldRadius) || !std::isfinite(V.ShieldRadius) || V.ShieldRadius < 0
        || !R.U8(Completed) || Completed>1 || !R.Key(V.Parent) || !R.U64(V.RelationVersion) || !R.Blob(V.FrozenWork,4096) || !ReadUpgradeSelections(R,V.Upgrades) || !ReadCapabilityStates(R,V.Capabilities)
        || !R.U8(Materialized) || Materialized>1
        || (V.Parent.Kind==ObjectKind::None ? V.Parent.StableId.Valid() || V.Parent.Generation!=0 : !V.Parent.Valid())) return false;
    V.Phased = Phased != 0; V.Supplied = Supplied != 0; V.Completed=Completed!=0;V.Materialized=Materialized!=0;
    if (!ValidateObjectCapabilities(V)) return false;
    FrozenWorkRecipe Recipe;return V.FrozenWork.empty() ? V.Definition!=TransactionWorkDefinition : DecodeFrozenWork(V,Recipe);
}
}
