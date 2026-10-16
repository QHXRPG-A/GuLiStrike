#include "GuLiNetOwnerState.h"
#include "GuLiNetHash.h"
#include "GuLiNetCapability.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace GuLiNet
{
namespace
{
Bytes RequestFingerprint(const TransactionRequest& V)
{
    // An ownership handoff may change delivery metadata, never the original work identity.
    Writer W; W.Key(V.Header.Object); W.U64(V.Header.At); W.Blob(EncodeTransaction(V)); return W.Take();
}
}
bool OwnerState::ConfigureUpgradeCatalogs(const std::map<std::string,CompiledUpgradeCatalog>& Values)
{
    if (Values.empty()) return UpgradeCatalogs.empty()
        && std::none_of(Rules.begin(),Rules.end(),[](const auto& R){return R.second.Kind==TransactionKind::Upgrade || !R.second.RequiredCapabilityCatalog.empty();});
    if (Values.size()>16) return false;
    std::map<std::string,CompiledUpgradeCatalog> Compiled;std::string Error;
    for (const auto& [Id,C]:Values)
    {
        if (Id.empty() || Id.size()>192 || Id.find('\0')!=std::string::npos || !C.Valid
            || !CompileUpgradeCatalog(C.Data,Compiled[Id],Error)) return false;
    }
    if (!UpgradeCatalogs.empty())
    {
        if (UpgradeCatalogs.size()!=Compiled.size()) return false;
        for (const auto& [Id,C]:Compiled)
        {
            const auto I=UpgradeCatalogs.find(Id);
            if (I==UpgradeCatalogs.end() || EncodeUpgradeCatalog(I->second.Data)!=EncodeUpgradeCatalog(C.Data)) return false;
        }
        return true;
    }
    for (const auto& [_,Rule]:Rules)
    {
        if (Rule.Kind==TransactionKind::Upgrade && !Compiled.contains(Rule.UpgradeCatalog)) return false;
        if (!Rule.RequiredCapabilityCatalog.empty())
        {
            const auto Catalog=Compiled.find(Rule.RequiredCapabilityCatalog);bool Found=false;
            if (Catalog!=Compiled.end()) for (const auto& Group:Catalog->second.Data.Groups)
                if (Group.Configuration==Rule.RequiredCapabilityGroup && Group.Executable) for (const auto& Capability:Group.Capabilities) Found|=Capability.Id==Rule.RequiredCapability;
            if (!Found) return false;
        }
    }
    UpgradeCatalogs=std::move(Compiled);
    for (const auto& [_,O]:Directory) if (!UpgradeStateAllowed(O)) {UpgradeCatalogs.clear();return false;}
    return true;
}
bool OwnerState::UpgradeStateAllowed(const ObjectRecord& O) const
{
    if (!ValidUpgradeSelections(O.Upgrades)) return false;
    for (const auto& [Id,S]:O.Upgrades)
    {
        const auto C=UpgradeCatalogs.find(Id);UpgradeResolved Resolved;std::string Error;
        if (C==UpgradeCatalogs.end() || S.CatalogVersion!=C->second.Data.Version
            || !ResolveUpgradeHistory(C->second,S.Chosen,Resolved,Error)) return false;
        for (const auto& NodeId:S.Chosen)
            if (!C->second.Data.Groups.at(C->second.GroupIndices.at(C->second.Data.Nodes.at(C->second.NodeIndices.at(NodeId)).Group)).Executable) return false;
    }
    return true;
}
bool OwnerState::RuleCapabilityAllowed(const ObjectRecord& O,const Rule& V) const
{
    if (V.RequiredCapabilityCatalog.empty()) return V.RequiredCapability.empty();
    const auto Catalog=UpgradeCatalogs.find(V.RequiredCapabilityCatalog);
    const auto History=O.Upgrades.find(V.RequiredCapabilityCatalog);UpgradeResolved Resolved;std::string Error;
    return Catalog!=UpgradeCatalogs.end() && History!=O.Upgrades.end()
        && History->second.CatalogVersion==Catalog->second.Data.Version
        && ResolveUpgradeHistory(Catalog->second,History->second.Chosen,Resolved,Error)
        && std::find(Resolved.Groups.begin(),Resolved.Groups.end(),V.RequiredCapabilityGroup)!=Resolved.Groups.end()
        && Resolved.Context.Installed.contains(V.RequiredCapability);
}
bool OwnerState::ConfigureResourceCatalog(std::map<std::uint32_t,OreDefinition> Nodes,std::vector<Team> Owners)
{
    if (ResourceLedgerValue.Snapshot().Configured || MiningReservationsValue.Snapshot().Configured) return false;
    ResourceLedger Resources; MiningReservations Mining; std::map<std::uint32_t,std::uint32_t> NodeClusters;
    std::map<std::uint32_t,ObjectKey> ClusterKeys;
    std::map<ObjectKey,std::vector<std::uint32_t>> ByCluster;
    for (const auto& [Node,V]:Nodes)
    {
        NodeClusters.emplace(Node,V.Cluster);
        auto I=ClusterKeys.find(V.Cluster);
        if (I==ClusterKeys.end()) I=ClusterKeys.emplace(V.Cluster,MiningClusterObject(V.Cluster)).first;
        ByCluster[I->second].push_back(Node);
    }
    if (!Resources.Configure(std::move(Nodes),std::move(Owners)) || !Mining.Configure(std::move(NodeClusters))) return false;
    ResourceLedgerValue=std::move(Resources); MiningReservationsValue=std::move(Mining);
    ResourceClusterNodes=std::move(ByCluster);return true;
}
bool OwnerState::RegisterResourceActorProfile(ResourceInventoryKind Capability,ObjectKind Kind,const std::string& Definition,std::uint32_t Capacity,Tick Period)
{
    if (!ResourceLedgerValue.Snapshot().Configured
        || Kind==ObjectKind::None || Kind>ObjectKind::BusinessWork
        || (Capability!=ResourceInventoryKind::Cargo && Capability!=ResourceInventoryKind::Processor)
        || Definition.empty() || Definition.size()>192 || Definition.find('\0')!=std::string::npos
        || !Capacity || Capacity>(Capability==ResourceInventoryKind::Cargo?1000000u:10000000u) || !Period || Period>60*86400) return false;
    const auto [It,Inserted]=ResourceActorProfiles.emplace(std::tuple{Kind,Definition,Capability},std::pair{Capacity,Period});
    return Inserted || It->second==std::pair{Capacity,Period};
}
bool OwnerState::RegisterResourceMiner(ObjectKey K,std::uint32_t Capacity,Tick Period)
{
    const auto It=Directory.find(K);
    if (It==Directory.end()) return false;
    const auto Profile=std::tuple{K.Kind,It->second.Definition,ResourceInventoryKind::Cargo}; const auto Value=std::pair{Capacity,Period};
    const auto Old=ResourceActorProfiles.find(Profile);
    if ((Old!=ResourceActorProfiles.end() && Old->second!=Value)
        || !ResourceLedgerValue.RegisterMiner(K,It->second.Grant.Faction,Capacity,Period)) return false;
    ResourceActorProfiles.emplace(Profile,Value); return true;
}
bool OwnerState::RegisterResourceFactory(ObjectKey K,std::uint32_t Capacity,Tick Period)
{
    const auto It=Directory.find(K);
    if (It==Directory.end()) return false;
    const auto Profile=std::tuple{K.Kind,It->second.Definition,ResourceInventoryKind::Processor}; const auto Value=std::pair{Capacity,Period};
    const auto Old=ResourceActorProfiles.find(Profile);
    if ((Old!=ResourceActorProfiles.end() && Old->second!=Value)
        || !ResourceLedgerValue.RegisterFactory(K,It->second.Grant.Faction,Capacity,Period)) return false;
    ResourceActorProfiles.emplace(Profile,Value); return true;
}
bool OwnerState::PrepareResourceActor(const ObjectRecord& O,bool Created,std::optional<ResourceLedger>& PreparedResources) const
{
    const auto K=O.Grant.Object;bool HasInventory=false;
    for (const auto Capability:{ResourceInventoryKind::Cargo,ResourceInventoryKind::Processor})
    {
        const auto Profile=ResourceActorProfiles.find({K.Kind,O.Definition,Capability});
        if (Profile==ResourceActorProfiles.end()) continue;
        HasInventory=true;
        if (!PreparedResources) PreparedResources=ResourceLedgerValue;
        const auto [Capacity,Period]=Profile->second;const auto& S=PreparedResources->Snapshot();
        if (Created)
        {
            const bool Registered=Capability==ResourceInventoryKind::Cargo
                ? PreparedResources->RegisterMiner(K,O.Grant.Faction,Capacity,Period)
                : PreparedResources->RegisterFactory(K,O.Grant.Faction,Capacity,Period);
            if (!Registered) return false;
        }
        else if (Capability==ResourceInventoryKind::Cargo)
        {
            const auto I=S.Miners.find(K);
            if (I==S.Miners.end() || I->second.Capacity!=Capacity || I->second.ExtractionPeriod!=Period) return false;
        }
        else
        {
            const auto I=S.Factories.find(K);
            if (I==S.Factories.end() || I->second.Capacity!=Capacity || I->second.ProcessingPeriod!=Period) return false;
        }
    }
    return !HasInventory || PreparedResources->SetInventoryOwner(K,O.Grant.Faction);
}
bool OwnerState::SetRules(std::map<std::string, Rule> Values)
{
    if (!Directory.empty()) return false;
    for (const auto& [IdValue, V] : Values)
        if (IdValue.empty() || IdValue.size()>256 || !ValidRule(V)) return false;
    Rules = std::move(Values); return true;
}
bool OwnerState::BeginEconomy(Resources Red, Resources Blue)
{ return EconomyLedger.Begin(Red, Blue) && EconomyLedger.Open(); }
bool OwnerState::Remember(const TransactionRequest& Request, const CommitResult& Result)
{
    if ((StageSource?StageSource->Results.size():Results.size()) >= 1000000) { FailureReason = "Owner event capacity exhausted"; return false; }
    Results.emplace(Request.Header.Event, Result); RequestBodies.emplace(Request.Header.Event, RequestFingerprint(Request));
    IndexResultDomain(Request.Header.Event,DecisionDomain);return true;
}
CommitResult OwnerState::Evaluate(Id Sender, const TransactionRequest& Delivery, Id NewToken)
{
    const auto& Previous=PreviousObjects();
    TransactionRequest R=Delivery;
    CommitResult Out; Out.Event = R.Header.Event; Out.Target = R.Target; Out.Kind=R.Kind; Out.Faction=R.Faction;Out.StateDomain=DecisionDomain;
    bool FinalDecision=false;
    ObjectRecord* ClosingWork=nullptr;FrozenWorkRecipe Admitted;
    if (R.Kind==TransactionKind::AdmitWork)
    {
        Message M;TransactionRequest Original;
        if (DecodeMessage(R.Admission,M) && DecodeTransaction(M.Header,M.Body,Original))
        {Out.WorkEvent=M.Header.Event;Out.WorkOperation=Original.Kind;}
    }
    auto Reject = [&](CommitStatus Status)
    {
        Out.Status=Status;
        Out.MiningRevision=MiningReservationsValue.Snapshot().Version;
        // Final results retain their event identity. Busy work remains pending.
        if (FinalDecision && Status!=CommitStatus::Paused && Status!=CommitStatus::Deferred)
        {
            if (ClosingWork && !CloseWork(*ClosingWork,Out,Delivery.Header.At))
            {Abort("Admitted work closure invariant failed");Out.Status=CommitStatus::Paused;return Out;}
            Out.StateVersion=NextDecisionVersion();
            Remember(Delivery,Out);
        }
        return Out;
    };
    if (R.Header.Epoch != MatchEpoch || R.Header.Match != Match) return Reject(CommitStatus::WrongEpoch);
    if (!R.Header.Event.Valid() || !R.Header.Sequence || !R.RootEvent.Valid() || R.StartedAt > R.Header.At)
        return Reject(CommitStatus::BadRequest);
    auto* SourceGrant=EditRecord(R.Header.Object);
    const bool RepeatingDestroyed=Results.contains(R.Header.Event) && SourceGrant!=nullptr
        && SourceGrant->Grant.State==Life::Destroyed && IsLiveMember(Sender) && R.Header.Sender==Sender
        && SourceGrant->Grant.SimOwner==Sender && SourceGrant->Grant.OwnerEpoch==R.Header.OwnerEpoch;
    if (!Authorize(Sender, R.Header, Domain::Simulation, true) && !RepeatingDestroyed) return Reject(CommitStatus::Unauthorized);
    if (const auto Work=ActiveWorkEvents.find(R.Header.Event);Work!=ActiveWorkEvents.end() && Work->second!=R.Header.Object)
        return Reject(CommitStatus::Unauthorized);
    if (const auto Work=ActiveWorkEvents.find(R.RootEvent);Work!=ActiveWorkEvents.end() && Work->second!=R.Header.Object)
        return Reject(CommitStatus::Unauthorized);
    if (auto Old = Results.find(R.Header.Event); Old != Results.end())
    {
        if (RequestBodies.at(R.Header.Event) != RequestFingerprint(R)) return Reject(CommitStatus::BadRequest);
        Out = Old->second; if (Out.Status == CommitStatus::Committed) Out.Status = CommitStatus::Duplicate; return Out;
    }
    if ((StageSource?StageSource->Results.size():Results.size())>=1000000) return Reject(CommitStatus::Deferred);
    FinalDecision=true;
    if (R.Kind==TransactionKind::AdmitWork) return AdmitWork(Sender,R);
    if (!R.Admission.empty()) return Reject(CommitStatus::BadRequest);
    if (SourceGrant->Definition==TransactionWorkDefinition)
    {
        if (SourceGrant->Grant.State!=Life::Alive || !DecodeFrozenWork((*SourceGrant),Admitted)
            || Delivery.Header.Event!=Admitted.Request.Header.Event || Delivery.Header.At<SourceGrant->BornAt
            || EncodeTransaction(Delivery)!=EncodeTransaction(Admitted.Request))
        {FinalDecision=false;return Reject(CommitStatus::BadRequest);}
        ClosingWork=SourceGrant;R=Admitted.Request;
    }
    else if (ActiveWorks.contains(R.Header.Object)
        && (CanAdmitWork(R.Kind) || R.Kind==TransactionKind::Reserve || R.ReservationToken.Valid()))
        return Reject(CommitStatus::Unauthorized);
    auto* Source=EditRecord(R.Header.Object);
    if (!Source) return Reject(CommitStatus::MissingObject);
    auto RuleIt = Rules.find(R.RuleId);
    if (RuleIt == Rules.end() || RuleIt->second.Kind != R.Kind || RuleIt->second.SourceDefinition != Source->Definition)
        return Reject(CommitStatus::InvalidRule);
    Rule RuleValue=RuleIt->second;
    if (R.Kind==TransactionKind::Damage) RuleValue.Damage=R.DamageAmount;
    const auto AttackId=AttackIdentity(R);const auto AttackOwner=AttackDomain(R);
    const auto FrozenAttack=Attacks.find(AttackId);
    if (FrozenAttack==Attacks.end())
    {
        // The attacker freezes the authorized damage. The receiving owner
        // accumulates its own health and target-local event identity; it never
        // writes or waits for the attacker's cooldown and target list.
        if (R.Kind!=TransactionKind::Damage && !ResolveCapabilityRule((*Source),Previous,R.RuleId,RuleValue)) return Reject(CommitStatus::InvalidRule);
    }
    else
    {
        RuleValue.Damage=FrozenAttack->second.Damage;RuleValue.Cooldown=FrozenAttack->second.Cooldown;
        RuleValue.MaxEffectLifetime=FrozenAttack->second.Lifetime;RuleValue.MaxTargets=FrozenAttack->second.MaximumTargets;
    }
    if (!ClosingWork && R.Kind!=TransactionKind::Damage && !RuleCapabilityAllowed((*Source),RuleValue)) return Reject(CommitStatus::Unauthorized);
    if ((!IsMiningTransaction(R.Kind) && (R.MiningCluster || R.MiningSample || R.MiningTask
        || (R.Kind!=TransactionKind::Extract && R.MiningToken.Valid())))
        || (IsMiningTransaction(R.Kind) && (R.Target.Valid() || R.ReservationToken.Valid() || R.ReservationKey.Account.Valid()
            || R.ReservationKey.Request || R.Related.Valid() || R.ExpectedRelationVersion
            || R.TargetX!=0 || R.TargetY!=0 || R.TargetZ!=0 || R.FacingYaw!=0))) return Reject(CommitStatus::BadRequest);
    const auto SpawnDefinition=OwnershipDefinitions.find(RuleValue.TargetDefinition);
    const bool EffectAdmission=R.Kind==TransactionKind::Spawn && SpawnDefinition!=OwnershipDefinitions.end()
        && SpawnDefinition->second.Policy==OwnershipPolicy::RetainedSource;
    const auto* OperationContract=R.Kind==TransactionKind::Capability?FindCapability(RuleValue.Capability):nullptr;
    const bool RetainedCapability=OperationContract && OperationContract->AcceptsRetainedSource;
    if (R.Header.At<Source->BornAt || R.Header.At-Source->BornAt<RuleValue.MinimumAge)
        return Reject(CommitStatus::InvalidRule);
    const bool Finalizing=R.Kind==TransactionKind::Commit || R.Kind==TransactionKind::Refund;
    if (!ClosingWork && !Finalizing && (R.StartedAt < Source->BornAt || R.StartedAt > Source->DiedAt
        || R.Header.At - R.StartedAt > RuleValue.MaxEffectLifetime)) return Reject(CommitStatus::Unauthorized);
    if (!ClosingWork && (R.Kind==TransactionKind::Credit || R.Kind==TransactionKind::Reserve
        || (R.Kind>=TransactionKind::Spawn && R.Kind!=TransactionKind::Destroy && !EffectAdmission && !RetainedCapability)) && Source->Grant.State!=Life::Alive)
        return Reject(CommitStatus::Unauthorized);
    auto AttackIt = Attacks.find(AttackId);
    Attack NewAttack{AttackOwner,R.RuleId,R.StartedAt,{}};
    NewAttack.Damage=RuleValue.Damage;NewAttack.Cooldown=RuleValue.Cooldown;
    NewAttack.Lifetime=RuleValue.MaxEffectLifetime;NewAttack.MaximumTargets=RuleValue.MaxTargets;
    if (AttackIt == Attacks.end() && R.Kind!=TransactionKind::Damage)
    {
        const auto Last = LastAttack.find({R.Header.Object, R.RuleId});
        if (Last != LastAttack.end() && (R.StartedAt < Last->second || R.StartedAt - Last->second < RuleValue.Cooldown))
            return Reject(CommitStatus::InvalidRule);
    }
    const auto& AttackValue = AttackIt==Attacks.end() ? NewAttack : AttackIt->second;
    if (AttackValue.Source != AttackOwner || AttackValue.RuleId != R.RuleId || AttackValue.Started != R.StartedAt
        || AttackValue.Targets.contains(R.Target) || AttackValue.Targets.size() >= RuleValue.MaxTargets)
        return Reject(CommitStatus::InvalidRule);
    if (R.Kind!=TransactionKind::Upgrade && (!R.UpgradeNode.empty() || R.ExpectedUpgradeVersion)) return Reject(CommitStatus::BadRequest);
    if (R.Kind!=TransactionKind::Capability && R.Kind!=TransactionKind::Spawn && !R.CapabilityPayload.empty()) return Reject(CommitStatus::BadRequest);
    if (R.Kind==TransactionKind::Capability)
    {
        const auto* Contract=FindCapability(RuleValue.Capability);
        if (!Contract || Contract->Version!=RuleValue.CapabilityVersion
            || !Source->Capabilities.contains(Contract->Name)) return Reject(CommitStatus::InvalidRule);
        CapabilityDelta Delta;
        const auto Status=Contract->Prepare({R,RuleValue,(*Source),Previous,EconomyLedger,ResourceLedgerValue,Players},Delta);
        if (Status!=CommitStatus::Committed) return Reject(Status);
        CommitResult PreparedResult=Out;PreparedResult.Capability=Contract->Name;
        for (const auto& [Key,Object]:Delta.Objects)
        {
            if (Key!=Object.Grant.Object) return Reject(CommitStatus::InvalidRule);
            PreparedResult.Objects.push_back(Object);
        }
        std::size_t NewCount=0;for (const auto& [Key,O]:Delta.Objects) if (!Previous.contains(Key))
        {
            ++NewCount;
            const auto Definition=OwnershipDefinitions.find(O.Definition);
            if (!UpgradeStateAllowed(O) || Definition==OwnershipDefinitions.end()) return Reject(CommitStatus::InvalidRule);
            const bool Independent=Definition->second.Policy==OwnershipPolicy::RetainedSource
                || Definition->second.Policy==OwnershipPolicy::ParentWork;
            if (O.Grant.Controller.Valid() && (!IsLiveMember(O.Grant.Controller)
                || (!Independent && Players.at(O.Grant.Controller).Faction!=O.Grant.Faction))) return Reject(CommitStatus::InvalidRule);
            if (!O.Grant.Controller.Valid() && Definition->second.Policy==OwnershipPolicy::ControllerPinned)
                return Reject(CommitStatus::InvalidRule);
        }
        if (Previous.size()+NewCount>100000) return Reject(CommitStatus::InvalidRule);
        if (!ValidateCapabilityCommit(PreparedResult,Previous)) return Reject(CommitStatus::InvalidRule);
        // Ordinary spawn, batch spawn and affiliation changes use exactly the
        // same declared inventory profiles. Prepare before publishing ANY part
        // of the capability delta; failed registration cannot leave an orphan.
        for (const auto& [Key,O]:Delta.Objects)
        {
            if (!PrepareResourceActor(O,!Previous.contains(Key),Delta.Resources)) return Reject(CommitStatus::InvalidRule);
            const auto Existing=Previous.find(Key);
            if (Existing!=Previous.end() && Existing->second.Grant.Faction!=O.Grant.Faction && Delta.Resources
                && (Delta.Resources->Snapshot().Miners.contains(Key) || Delta.Resources->Snapshot().Factories.contains(Key)))
                PreparedResult.InventoryOwners.emplace(Key,O.Grant.Faction);
        }
        for (const auto& [Index,Owner]:Delta.TerritoryOwners)
        {
            if (!Delta.Resources) Delta.Resources=ResourceLedgerValue;
            if (!Delta.Resources->SetTerritoryOwner(Index,Owner)) return Reject(CommitStatus::InvalidRule);
        }
        // No capability code mutates the live room. This is the sole commit point.
        Out.Capability=std::move(PreparedResult.Capability);Out.Objects=std::move(PreparedResult.Objects);
        Out.InventoryOwners=std::move(PreparedResult.InventoryOwners);
        for (auto& [Key,Object]:Delta.Objects)
        {
            const auto Old=Previous.find(Key);
            if (Old==Previous.end()) Out.Births.push_back(Object);
            Directory.insert_or_assign(Key,std::move(Object));
        }
        if (Delta.Accounts) EconomyLedger=std::move(*Delta.Accounts);
        if (Delta.Resources) ResourceLedgerValue=std::move(*Delta.Resources);
        Out.TerritoryOwners=std::move(Delta.TerritoryOwners);
        Out.ResourceRevision=ResourceLedgerValue.Snapshot().Version;
    }
    else if (R.Kind==TransactionKind::Upgrade)
    {
        const auto Catalog=UpgradeCatalogs.find(RuleValue.UpgradeCatalog);
        if (R.Target!=R.Header.Object || R.Faction!=Source->Grant.Faction || R.UpgradeNode.empty()
            || R.ReservationToken.Valid() || R.ReservationKey.Account.Valid() || R.ReservationKey.Request
            || R.Related.Valid() || R.ExpectedRelationVersion || R.ResourceNode || R.MiningCluster || R.MiningSample
            || R.MiningTask || R.MiningToken.Valid() || R.TargetX || R.TargetY || R.TargetZ || R.FacingYaw)
            return Reject(CommitStatus::BadRequest);
        if (Catalog==UpgradeCatalogs.end()) return Reject(CommitStatus::InvalidRule);
        const auto Selected=Source->Upgrades.find(RuleValue.UpgradeCatalog);
        const UpgradeSelection Empty{Catalog->second.Data.Version,0,{}};
        const auto& Old=Selected==Source->Upgrades.end()?Empty:Selected->second;std::string Why;
        if (Old.CatalogVersion!=Catalog->second.Data.Version || Old.Version!=R.ExpectedUpgradeVersion
            || !CanChooseUpgrade(Catalog->second,Old.Chosen,R.UpgradeNode,Why)) return Reject(CommitStatus::InvalidRule);
        ObjectRecord Changed=(*Source);auto& Next=Changed.Upgrades[RuleValue.UpgradeCatalog];Next=Old;
        Next.Chosen.push_back(R.UpgradeNode);++Next.Version;
        if (!UpgradeStateAllowed(Changed)) return Reject(CommitStatus::InvalidRule);
        (*Source)=Changed;Out.Objects.push_back(std::move(Changed));Out.Balance=EconomyLedger.Balance(R.Faction);
    }
    else if (IsMiningTransaction(R.Kind))
    {
        if (R.Faction!=Source->Grant.Faction
            || !ResourceLedgerValue.Snapshot().Miners.contains(R.Header.Object)) return Reject(CommitStatus::Unauthorized);
        MiningReservationOutcome Result;
        if (R.Kind==TransactionKind::ClaimMiningPosition)
        {
            if (R.ResourceNode || R.MiningToken.Valid()) return Reject(CommitStatus::BadRequest);
            Result=MiningReservationsValue.ClaimPosition(R.Header.Object,R.MiningCluster,R.MiningSample,R.MiningTask,R.Header.Event);
        }
        else
        {
            if (R.MiningCluster || R.MiningSample || R.MiningTask) return Reject(CommitStatus::BadRequest);
            if (R.Kind==TransactionKind::ClaimMiningNode)
            {
                const auto Node=ResourceLedgerValue.Snapshot().Nodes.find(R.ResourceNode);
                if (Node==ResourceLedgerValue.Snapshot().Nodes.end() || !ResourceLedgerValue.Snapshot().Remaining.at(R.ResourceNode)) return Reject(CommitStatus::InvalidRule);
                const auto Owner=ResourceLedgerValue.Snapshot().Owners[Node->second.Territory];
                if (Playable(Owner) && Owner!=R.Faction) return Reject(CommitStatus::Unauthorized);
                Result=MiningReservationsValue.ClaimNode(R.Header.Object,R.MiningToken,R.ResourceNode,R.Header.Event);
            }
            else
            {
                if (R.ResourceNode) return Reject(CommitStatus::BadRequest);
                Result=MiningReservationsValue.Release(R.Header.Object,R.MiningToken,R.Header.Event);
            }
        }
        if (Result.Status==MiningReservationStatus::Deferred)
        { return Reject(CommitStatus::Deferred); }
        if (Result.Status==MiningReservationStatus::Conflict) return Reject(CommitStatus::BadRequest);
        Out.Mining=Result;
        if (!Result.IsCommitted()) return Reject(Result.Status==MiningReservationStatus::MissingLease ? CommitStatus::InvalidReservation : CommitStatus::InvalidRule);
    }
    else if (IsResourceTransaction(R.Kind))
    {
        if ((!ClosingWork && R.Faction!=Source->Grant.Faction) || R.ReservationToken.Valid() || R.ReservationKey.Request
            || R.ReservationKey.Account.Valid() || R.Related.Valid() || R.ExpectedRelationVersion)
            return Reject(CommitStatus::BadRequest);
        if ((R.Kind==TransactionKind::Extract && (R.Target.Valid() || !R.ResourceNode))
            || (R.Kind!=TransactionKind::Extract && R.ResourceNode)) return Reject(CommitStatus::BadRequest);
        ResourceOutcome Result;
        if (R.Kind==TransactionKind::Extract)
        {
            const auto Node=ResourceLedgerValue.Snapshot().Remaining.find(R.ResourceNode);
            const auto Held=HeldOre.find(R.ResourceNode);
            if (!ClosingWork && (!MiningReservationsValue.OwnsNode(R.Header.Object,R.MiningToken,R.ResourceNode)
                || Node==ResourceLedgerValue.Snapshot().Remaining.end() || (Held!=HeldOre.end() && Node->second<=Held->second)))
                return Reject(CommitStatus::InvalidReservation);
            Result=ResourceLedgerValue.Extract(R.Header.Object,R.ResourceNode,R.Header.Event,R.StartedAt,ClosingWork?&Admitted.Resource:nullptr);
        }
        else if (R.Kind==TransactionKind::Deliver)
        {
            const auto Target=Previous.find(R.Target);
            if (Target==Previous.end() || (!ClosingWork && Target->second.Grant.State!=Life::Alive) || !Target->second.Completed
                || Target->second.Definition!=RuleValue.TargetDefinition) return Reject(CommitStatus::InvalidRule);
            if (!ClosingWork)
            {
                const auto Cargo=ResourceLedgerValue.Snapshot().Miners.find(R.Header.Object);
                const auto Factory=ResourceLedgerValue.Snapshot().Factories.find(R.Target);const auto Held=HeldFactoryCapacity.find(R.Target);
                if (Cargo!=ResourceLedgerValue.Snapshot().Miners.end() && Factory!=ResourceLedgerValue.Snapshot().Factories.end()
                    && ResourceLedger::Queued(Factory->second)+static_cast<std::uint64_t>(Cargo->second.Cargo.Blue)+Cargo->second.Cargo.Red
                        +(Held==HeldFactoryCapacity.end()?0:Held->second)>Factory->second.Capacity) return Reject(CommitStatus::InsufficientResources);
            }
            Result=ResourceLedgerValue.Deliver(R.Header.Object,R.Target,R.Header.Event,R.StartedAt,ClosingWork?&Admitted.Resource:nullptr);
        }
        else
        {
            if (R.Target!=R.Header.Object || !Source->Completed) return Reject(CommitStatus::InvalidRule);
            Result=ResourceLedgerValue.Process(R.Header.Object,R.Header.Event,R.StartedAt,
                [&](Team F,const Resources& Amount){return EconomyLedger.Add(F,Amount);},ClosingWork?&Admitted.Resource:nullptr);
        }
        if (!Result.IsCommitted())
        {
            if (Result.Status==ResourceStatus::Deferred)
            { return Reject(CommitStatus::Deferred); }
            return Reject(Result.Status==ResourceStatus::Forbidden ? CommitStatus::Unauthorized
                : Result.Status==ResourceStatus::MissingSource ? CommitStatus::MissingObject : CommitStatus::InvalidRule);
        }
        Out.Resource=Result;
        if (R.Kind==TransactionKind::Process) Out.Faction=Result.Beneficiary;
        Out.Balance=EconomyLedger.Balance(Out.Faction);
    }
    else if (R.Kind == TransactionKind::Damage)
    {
        if (!std::isfinite(R.DamageAmount) || R.DamageAmount<=0) return Reject(CommitStatus::BadRequest);
        RuleValue.Damage=R.DamageAmount;
        auto* Target=EditRecord(R.Target);
        if (!Target) return Reject(CommitStatus::MissingObject);
        auto& T = *Target;
        if (T.Grant.State != Life::Alive || !ValidHealth(T.Vitals) || T.Vitals.Dead) return Reject(CommitStatus::TargetDead);
        if (T.Grant.Faction == Source->Grant.Faction) return Reject(CommitStatus::FriendlyFire);
        if (T.Phased || RuleValue.Damage <= 0 || !std::isfinite(R.TargetX) || !std::isfinite(R.TargetY))
            return Reject(CommitStatus::InvalidRule);
        float Remaining = RuleValue.Damage;
        for (const auto& Key : IndexedSource().ShieldObjects)
        {
            if (Remaining <= 0) break;
            const auto& B=Previous.at(Key);
            if (B.Grant.State != Life::Alive || B.Grant.Faction != T.Grant.Faction || !B.Supplied
                || B.ShieldRadius <= 0 || !ValidHealth(B.Vitals) || B.Vitals.Dead
                || !InShieldRadius(R.TargetX, R.TargetY, B.X, B.Y, B.ShieldRadius)) continue;
            auto& Shield=EditRecord(Key)->Vitals;
            const float Amount = AbsorbShield(Shield, Remaining); Remaining -= Amount;
            if (Amount > 0) Out.Barriers.emplace_back(Key, Shield);
        }
        DamageDelta Delta;
        if (Remaining > 0 && !ApplyDamage(T.Vitals, Remaining, Delta)) return Reject(CommitStatus::InvalidRule);
        Delta.Absorbed += RuleValue.Damage - Remaining; Out.Damage = Delta; Out.Vitals = T.Vitals;
        if (Delta.Killed)
        {
            T.Grant.State = Life::Dead; T.DiedAt = T.LogicalAge;
            Out.DeathEvent = DeriveDeathId(R.Header.Event); Out.RewardEvent = DeriveRewardId(R.Header.Event);
        }
    }
    else if (IsObjectTransaction(R.Kind))
    {
        const auto& S=(*Source);
        const Team Faction=ObjectAffiliation(S);
        if (R.Faction!=Faction || !std::isfinite(R.TargetX) || !std::isfinite(R.TargetY)
            || !std::isfinite(R.TargetZ) || !std::isfinite(R.FacingYaw)) return Reject(CommitStatus::BadRequest);
        // Validate every object and price before finalizing the reservation or mutating the directory.
        const Reservation* Payment=nullptr;
        if (RuleValue.Amount.Positive())
        {
            const auto It=EconomyLedger.Snapshot().Pending.find(R.ReservationKey);
            if (R.ReservationKey.Account!=DeriveEconomyAccount(R.Header.Object)
                || It==EconomyLedger.Snapshot().Pending.end() || It->second.Token!=R.ReservationToken
                || It->second.Faction!=Faction || It->second.Cost.Blue!=RuleValue.Amount.Blue
                || It->second.Cost.Red!=RuleValue.Amount.Red) return Reject(CommitStatus::InvalidReservation);
            Payment=&It->second;
        }
        else if (R.ReservationToken.Valid() || R.ReservationKey.Request) return Reject(CommitStatus::InvalidReservation);
        ObjectRecord Changed;
        if (R.Kind==TransactionKind::Spawn)
        {
            if (!S.Completed || !R.Target.Valid() || R.Target.Kind!=RuleValue.SpawnKind || Previous.contains(R.Target)
                || (R.Related.Valid() && R.Related!=R.Header.Object)) return Reject(CommitStatus::InvalidRule);
            if (Previous.size()>=100000) { Abort("Object directory capacity exhausted"); return Reject(CommitStatus::Paused); }
            std::uint32_t LastGeneration=LastRetiredLife(R.Target.Kind,R.Target.StableId), MaximumOrdinal=0, Children=0, FactionCount=0;
            const auto& Index=IndexedSource();
            if (const auto I=Index.MaximumOrdinals.find(OrdinalScope(R.Target.Kind));I!=Index.MaximumOrdinals.end()) MaximumOrdinal=I->second;
            // ObjectKey ordering keeps all lives of one stable identity adjacent.
            for (auto I=Previous.lower_bound({R.Target.Kind,R.Target.StableId,0});I!=Previous.end()
                && I->first.Kind==R.Target.Kind && I->first.StableId==R.Target.StableId;++I)
            {
                if (I->second.Grant.State<Life::Dead) return Reject(CommitStatus::InvalidRule);
                LastGeneration=std::max(LastGeneration,I->first.Generation);
            }
            if (RuleValue.MaximumLiveChildren || RuleValue.MaximumFactionObjects) for (const auto& K:Index.LivingObjects)
            {
                const auto& O=Previous.at(K);
                const auto Parent=S.Grant.Object.Kind==ObjectKind::BusinessWork && S.Parent.Valid()?Previous.find(O.Parent):Previous.end();
                const bool SameWorkSource=S.Grant.Object.Kind==ObjectKind::BusinessWork && S.Parent.Valid()
                    && Parent!=Previous.end() && Parent->first.Kind==ObjectKind::BusinessWork && Parent->second.Parent==S.Parent;
                if ((O.Parent==R.Header.Object || SameWorkSource) && O.Definition==RuleValue.TargetDefinition && O.Grant.State<Life::Dead) ++Children;
                if (K.Kind==R.Target.Kind && O.Grant.Faction==Faction && O.Grant.State<Life::Dead) ++FactionCount;
            }
            if (LastGeneration==UINT32_MAX || R.Target.Generation!=LastGeneration+1
                || (RuleValue.MaximumLiveChildren && Children>=RuleValue.MaximumLiveChildren)
                || (RuleValue.MaximumFactionObjects && FactionCount>=RuleValue.MaximumFactionObjects)) return Reject(CommitStatus::InvalidRule);
            if (MaximumOrdinal==UINT32_MAX) { Abort("Object ordinal capacity exhausted"); return Reject(CommitStatus::Paused); }
            const auto Ordinal=MaximumOrdinal+1;
            // An admitted effect keeps the producer's own authority independently
            // of later parent death/control changes. Geometry never migrates to a
            // different peer merely because the producer's measured cost changed.
            Id Desired=EffectAdmission ? Sender : S.Grant.Controller;
            if (RuleValue.SpawnControllerSeat!=Role::Observer)
            {
                Desired={};
                for (const auto& [CandidatePlayer,M] : Players)
                    if (M.Faction==Faction && M.Seat==RuleValue.SpawnControllerSeat && IsLiveMember(CandidatePlayer)) { Desired=CandidatePlayer; break; }
            }
            if (SpawnDefinition==OwnershipDefinitions.end()) return Reject(CommitStatus::InvalidRule);
            const auto& SpawnPolicy=SpawnDefinition->second;
            const bool Pinned=SpawnPolicy.Policy==OwnershipPolicy::ControllerPinned;
            const auto AcceptedController=Players.find(Desired);
            if (Pinned && (AcceptedController==Players.end() || !IsLiveMember(Desired)
                || AcceptedController->second.Faction!=Faction || AcceptedController->second.Seat!=SpawnPolicy.ControllerSeat))
                return Reject(CommitStatus::Unauthorized);
            if (!Pinned && !EffectAdmission && (AcceptedController==Players.end() || !IsLiveMember(Desired)
                || AcceptedController->second.Faction!=Faction)) Desired={};
            if (SpawnPolicy.RetireOnVacancy
                && (S.Grant.Policy!=OwnershipPolicy::ControllerPinned || S.Grant.Controller!=Desired || S.Grant.SimOwner!=Desired))
                return Reject(CommitStatus::Unauthorized);
            Changed.Grant.Object=R.Target; Changed.Grant.Faction=Faction; Changed.Grant.Controller=Desired;
            Changed.Grant.Policy=SpawnPolicy.Policy;Changed.Grant.ControllerSeat=SpawnPolicy.ControllerSeat;Changed.Grant.RetireOnVacancy=SpawnPolicy.RetireOnVacancy;
            Changed.Grant.OwnershipGroup=SpawnPolicy.Group.empty()?Id{}:StableId("GuLiOwnershipGroup/v1",SpawnPolicy.Group);
            if (Changed.Grant.OwnershipGroup.Valid())
                for (const auto& [K,V]:Previous) if (V.Grant.OwnershipGroup==Changed.Grant.OwnershipGroup)
                {Changed.Grant.GroupEpoch=V.Grant.GroupEpoch;break;}
            Changed.Grant.SourceLease=R.Header.Event; Changed.Grant.EstimatedCost=RuleValue.SpawnCost;
            Changed.Definition=RuleValue.TargetDefinition; Changed.Ordinal=Ordinal; Changed.FacingYaw=R.FacingYaw;
            Changed.X=R.TargetX; Changed.Y=R.TargetY; Changed.Z=R.TargetZ; Changed.BornAt=R.Header.At;
            Changed.Vitals.Value=Changed.Vitals.Maximum=RuleValue.SpawnHealth;
            Changed.Vitals.Shield=Changed.Vitals.MaximumShield=RuleValue.SpawnShield;
            Changed.Vitals.Version=RuleValue.SpawnHealth>0 ? 1 : 0;
            Changed.ShieldRadius=RuleValue.SpawnShieldRadius; Changed.Completed=RuleValue.SpawnCompleted;
            Changed.Parent=R.Header.Object;
            Changed.Capabilities=RuleValue.SpawnCapabilities;Changed.Upgrades=RuleValue.SpawnUpgrades;
            if (!DeriveSpawnCapabilities(S,Previous,R,Changed)) return Reject(CommitStatus::InvalidRule);
            if (!ValidateObjectCapabilities(Changed) || !UpgradeStateAllowed(Changed)) return Reject(CommitStatus::InvalidRule);
            if (EffectAdmission)
            {
                Changed.Grant.SimOwner=Sender; Changed.Grant.OwnerEpoch=1; Changed.Grant.ControlEpoch=1;
                Changed.Grant.State=Life::Alive;
            }
        }
        else
        {
            if (R.Target!=R.Header.Object || S.Definition!=RuleValue.TargetDefinition
                || R.ExpectedRelationVersion!=S.RelationVersion || S.RelationVersion==UINT64_MAX)
                return Reject(CommitStatus::InvalidRule);
            Changed=S;
            if (R.Kind==TransactionKind::Complete)
            {
                if (S.Completed) return Reject(CommitStatus::InvalidRule);
                Changed.Completed=true;
            }
            else if (R.Kind==TransactionKind::Destroy)
            {
                if (S.Grant.State!=Life::Alive && S.Grant.State!=Life::Dead) return Reject(CommitStatus::TargetDead);
                if (ValidHealth(Changed.Vitals))
                {
                    if (Changed.Vitals.Version==UINT32_MAX) return Reject(CommitStatus::InvalidRule);
                    Changed.Vitals.Value=0; Changed.Vitals.Dead=true; ++Changed.Vitals.Version;
                }
                Changed.Grant.State=Life::Destroyed;
                Changed.DiedAt=std::min(Changed.DiedAt,R.Header.At); Changed.Supplied=false;
            }
            else if (RuleValue.Relation==RelationKind::Supply)
            {
                if (!S.Completed || S.Supplied==RuleValue.RelationValue) return Reject(CommitStatus::InvalidRule);
                Changed.Supplied=RuleValue.RelationValue;
            }
            else if (RuleValue.Relation==RelationKind::Phase)
            {
                if (S.Phased==RuleValue.RelationValue) return Reject(CommitStatus::InvalidRule);
                Changed.Phased=RuleValue.RelationValue;
            }
            else if (RuleValue.Relation==RelationKind::Parent)
            {
                if (RuleValue.RelationValue)
                {
                    const auto Parent=Previous.find(R.Related);
                    if (Parent==Previous.end() || Parent->second.Grant.State!=Life::Alive || !Parent->second.Completed
                        || Parent->second.Grant.Faction!=Faction || R.Related==R.Target) return Reject(CommitStatus::InvalidRule);
                    ObjectKey Ancestor=R.Related;
                    std::set<ObjectKey> Visited;
                    while (Ancestor.Valid())
                    {
                        if (Ancestor==R.Target || !Visited.insert(Ancestor).second) return Reject(CommitStatus::InvalidRule);
                        const auto It=Previous.find(Ancestor);
                        if (It==Previous.end()) return Reject(CommitStatus::MissingObject);
                        Ancestor=It->second.Parent;
                    }
                }
                else if (R.Related.Valid()) return Reject(CommitStatus::InvalidRule);
                if (S.Parent==R.Related) return Reject(CommitStatus::InvalidRule);
                Changed.Parent=R.Related;
            }
            else return Reject(CommitStatus::InvalidRule);
            ++Changed.RelationVersion;
        }
        std::optional<ResourceLedger> SpawnResources;
        if (!ValidateCapabilityMutation(Previous,std::span<const ObjectRecord>(&Changed,1))) return Reject(CommitStatus::InvalidRule);
        if (R.Kind==TransactionKind::Spawn && !PrepareResourceActor(Changed,true,SpawnResources)) return Reject(CommitStatus::InvalidRule);
        if (Payment)
        {
            const Reservation Value=*Payment;
            if (!EconomyLedger.Finalize(Value,false)) return Reject(CommitStatus::InvalidReservation);
            Out.Reserved=Value;
        }
        if (R.Kind==TransactionKind::Spawn)
        {
            if (SpawnResources) ResourceLedgerValue=std::move(*SpawnResources);
            Directory.emplace(R.Target,Changed);Out.Births.push_back(Changed);

        }
        else
        {
            (*Source)=Changed;
        }
        Out.Objects.push_back(std::move(Changed)); Out.Balance=EconomyLedger.Balance(Faction);
    }
    else
    {
        const Team Faction = ClosingWork?Admitted.Request.Faction:Source->Grant.Faction;
        if (R.Faction != Faction) return Reject(CommitStatus::Unauthorized);
        if (R.Kind == TransactionKind::Credit)
        { if (!EconomyLedger.Add(Faction, RuleValue.Amount)) return Reject(CommitStatus::InvalidRule); }
        else if (R.Kind == TransactionKind::Reserve)
        {
            if (R.ReservationKey.Account != DeriveEconomyAccount(R.Header.Object) || !EconomyLedger.Reserve(Faction, R.ReservationKey, RuleValue.Amount, NewToken, Out.Reserved))
                return Reject(CommitStatus::InsufficientResources);
        }
        else
        {
            const auto It = EconomyLedger.Snapshot().Pending.find(R.ReservationKey);
            if (R.ReservationKey.Account != DeriveEconomyAccount(R.Header.Object) || It == EconomyLedger.Snapshot().Pending.end()
                || It->second.Token != R.ReservationToken || It->second.Faction != Faction)
                return Reject(CommitStatus::InvalidReservation);
            const Reservation Value = It->second;
            // An admitted voucher must remain held until its money and work
            // closure can commit together (a refund can temporarily overflow).
            if (!EconomyLedger.Finalize(Value, R.Kind == TransactionKind::Refund))
                return Reject(ClosingWork ? CommitStatus::Deferred : CommitStatus::InvalidReservation);
            Out.Reserved=Value;
        }
        Out.Balance = EconomyLedger.Balance(Faction);
    }
    auto Abandon=[&](ObjectKey K)
    {
        if (!MiningReservationsValue.Snapshot().Configured) return true;
        if (!MiningReservationsValue.Snapshot().Leases.contains(K)) return true;
        return MiningReservationsValue.AbandonSource(K);
    };
    if (Out.Damage.Killed && !Abandon(Out.Target)) { Abort("Mining lease revision exhausted"); return Reject(CommitStatus::Paused); }
    for (const auto& O:Out.Objects) if (O.Grant.State>=Life::Dead && !Abandon(O.Grant.Object))
    { Abort("Mining lease revision exhausted"); return Reject(CommitStatus::Paused); }
    Out.MiningRevision=MiningReservationsValue.Snapshot().Version;
    if (AttackIt==Attacks.end())
    {
        NewAttack.Targets.insert(R.Target);IndexAttackDomain(AttackId,NewAttack.Source);Attacks.emplace(AttackId,std::move(NewAttack));
        if (R.Kind!=TransactionKind::Damage)
        {LastAttack[{R.Header.Object,R.RuleId}]=R.StartedAt;HistoryFragments[R.Header.Object].insert("cooldown/"+R.RuleId);}
    }
    else AttackIt->second.Targets.insert(R.Target);
    // Every committed transaction carries an authoritative balance at its ledger
    // version, including occupancy/relationship operations that spend nothing.
    // A default zero payload must never overwrite a client's actual account.
    if (Playable(Out.Faction)) Out.Balance=EconomyLedger.Balance(Out.Faction);
    if (ClosingWork && !CloseWork(*ClosingWork,Out,Delivery.Header.At))
    {Abort("Admitted work closure invariant failed");return Reject(CommitStatus::Paused);}
    Out.Status = CommitStatus::Committed; Out.StateVersion = NextDecisionVersion();
    Remember(Delivery, Out); return Out;
}

}
