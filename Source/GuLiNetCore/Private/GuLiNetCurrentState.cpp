#include "GuLiNetOwnerState.h"
#include "GuLiNetHash.h"
#include "GuLiNetTrace.h"
#include <algorithm>
#include <charconv>

namespace GuLiNet
{
Id OwnerState::ResourceGroupFor(const TransactionRequest& R) const
{
    auto Group=[&](ObjectKey K)
    {
        std::set<ObjectKey> Seen;
        while (K.Valid()&&Seen.size()<64&&Seen.insert(K).second)
        {const auto* O=FindRecord(K);if (!O) break;if (O->Grant.OwnershipGroup.Valid()) return O->Grant.OwnershipGroup;K=O->Parent;}
        return Id{};
    };
    for (const auto K:{ExecutionDomain(R),R.Header.Object,R.Target,R.Related}) if (const auto G=Group(K);G.Valid()) return G;
    // Economy and population mutations share the authored team account's domain.
    if (R.Kind==TransactionKind::Credit||R.Kind==TransactionKind::Reserve||R.Kind==TransactionKind::Commit||R.Kind==TransactionKind::Refund
        ||R.Kind==TransactionKind::Spawn||R.Kind==TransactionKind::Destroy||R.Kind==TransactionKind::Capability||R.Kind==TransactionKind::Damage)
    {
        Team F=R.Faction;if (!Playable(F)) if (const auto* O=FindRecord(R.Header.Object)) F=O->Grant.Faction;
        if (Playable(F)) return Group(TeamAccountObject(F));
    }
    return {};
}
bool OwnerState::CurrentResourceRequest(const TransactionRequest& R) const
{
    const auto G=ResourceGroupFor(R);const auto I=ResourceEpochs.find(G);
    // Standalone worlds without Coordinator group metadata have no resource fence.
    return I==ResourceEpochs.end()?(!R.Header.ResourceGroup.Valid()&&!R.Header.ResourceEpoch)
        :R.Header.ResourceGroup==G&&R.Header.ResourceEpoch==I->second;
}
void OwnerState::SetResourceEpoch(Id G,Revision E)
{if (G.Valid()&&E) ResourceEpochs[G]=std::max(ResourceEpochs[G],E);}
void OwnerState::SealResourceEpoch(Id G,Revision E)
{
    TraceScope Trace("GuLiHybrid_ResourceEpochCleanup");
    if (!G.Valid()||!E||ResourceEpochs[G]>=E) return;
    ResourceEpochs[G]=E;
    std::vector<std::tuple<Id,Id,Id>> AbortWork;
    for (const auto& [Event,P]:Prepared)
    {OwnerWork W;if (DecodeOwnerWork(P.second,W)&&ResourceGroupFor(W.Request)==G) AbortWork.emplace_back(P.first,Event,W.Proposal);}
    for (const auto& [Planner,Event,Proposal]:AbortWork) Cancel(Planner,Event,Proposal);
    std::set<Id> Events;
    for (const auto& [Event,R]:Results)
        if (R.ResourceGroup==G || (FindRecord(R.StateDomain)&&FindRecord(R.StateDomain)->Grant.OwnershipGroup==G)) Events.insert(Event);
    for (const auto Event:Events)
    {
        if (const auto D=ResultDomains.find(Event);D!=ResultDomains.end())
        {const StateKey K{D->second,"event/"+Event.ToString()};HistoryFragments[D->second].erase(K.Capability);Versions.erase(K);ResultDomains.erase(D);}
        Results.erase(Event);RequestBodies.erase(Event);Finished.erase(Event);
    }
    std::erase_if(Cancelled,[&](const auto& P){return Events.contains(P.first)
        ||std::any_of(AbortWork.begin(),AbortWork.end(),[&](const auto& W){return std::get<1>(W)==P.first;});});
    auto EState=EconomyLedger.Snapshot();
    for (const auto Team:{Team::Red,Team::Blue}) if (const auto* A=FindRecord(TeamAccountObject(Team));A&&A->Grant.OwnershipGroup==G)
    {for (const auto& Key:FinalizedAccounts[Team]) EState.Finalized.erase(Key);FinalizedAccounts[Team].clear();}
    EconomyLedger.Restore(std::move(EState));
    // Decisions are request deduplication, whereas leases, cargo, FIFO provenance
    // and already admitted capacity reservations are current gameplay state.
    auto Resources=ResourceLedgerValue.Snapshot();
    std::erase_if(Resources.Decisions,[&](const auto& P){return Events.contains(P.first);});ResourceLedgerValue.Restore(std::move(Resources));
    auto Mining=MiningReservationsValue.Snapshot();
    std::erase_if(Mining.Decisions,[&](const auto& P)
    {
        const auto* O=FindRecord(MiningClusterObject(P.second.Outcome.Lease.Cluster));
        const auto* Source=FindRecord(P.second.Outcome.Source);
        return Events.contains(P.first)||(O&&O->Grant.OwnershipGroup==G)||(Source&&Source->Grant.OwnershipGroup==G);
    });
    for (const auto& [Source,L]:Mining.Leases)
    {const auto* O=FindRecord(MiningClusterObject(L.Cluster));if (O&&O->Grant.OwnershipGroup==G) Mining.SnapshotLeases[Source]=L;}
    if (!MiningReservationsValue.Restore(std::move(Mining))) Abort("Unable to seal resource mining request ledger");
    FragmentCache.clear();FragmentCacheBytes=0;
}
std::vector<StateFragment> OwnerState::ExportCurrent(std::span<const ObjectKey> Objects) const
{
    TraceScope Trace("GuLiHybrid_ResourceSnapshotNumeric");
    std::vector<StateFragment> Out;auto Keys=FragmentKeys(Objects,false);
    const std::set<ObjectKey> Scope(Objects.begin(),Objects.end());
    for (const auto& [Event,A]:Attacks) if (Scope.contains(A.Source))
    {
        const auto* O=FindRecord(A.Source);
        if (O&&O->LogicalAge>=A.Started&&O->LogicalAge-A.Started<=A.Lifetime) Keys.insert({A.Source,"attack/"+Event.ToString()});
    }
    for (const auto& [K,At]:LastAttack) if (Scope.contains(K.first))
    {
        const auto* O=FindRecord(K.first);const auto Rule=Rules.find(K.second);
        if (O&&Rule!=Rules.end()&&(O->LogicalAge<At||O->LogicalAge-At<Rule->second.Cooldown)) Keys.insert({K.first,"cooldown/"+K.second});
    }
    for (const auto& K:Keys)
    {
        Bytes Value;
        if (K.Capability=="account")
        {
            const Team F=K.Object==TeamAccountObject(Team::Red)?Team::Red:Team::Blue;Writer W;
            W.U8(static_cast<std::uint8_t>(F));WriteResources(W,EconomyLedger.Balance(F));std::uint32_t N=0;
            for (const auto& [Key,R]:EconomyLedger.Snapshot().Pending) if (R.Faction==F) ++N;
            W.U32(N);for (const auto& [Key,R]:EconomyLedger.Snapshot().Pending) if (R.Faction==F)
            {W.Identity(R.Token);W.Identity(R.Key.Account);W.U64(R.Key.Request);W.U8(static_cast<std::uint8_t>(R.Faction));WriteResources(W,R.Cost);}
            W.U32(0);Value=W.Take();
        }
        else if (K.Capability.starts_with("leases/"))
        {
            std::uint32_t Cluster=0;const auto Text=K.Capability.substr(7);const auto Parsed=std::from_chars(Text.data(),Text.data()+Text.size(),Cluster);
            if (Parsed.ec!=std::errc{}||Parsed.ptr!=Text.data()+Text.size()) return {};
            Writer W;const auto& Leases=MiningReservationsValue.LeasesForCluster(Cluster);W.U32(static_cast<std::uint32_t>(Leases.size()));
            for (const auto Key:Leases)
            {const auto& L=MiningReservationsValue.Snapshot().Leases.at(Key);W.Key(L.Source);W.U32(L.Cluster);W.U8(L.Sample);W.U32(L.Task);W.Identity(L.Token);W.U32(L.Node);W.U64(L.Version);}
            W.U32(0);
            // Current leases are the authenticated baseline of this complete
            // cut. They do not require the discarded claim/release decisions.
            W.U32(static_cast<std::uint32_t>(Leases.size()));
            for (const auto Key:Leases)
            {const auto& L=MiningReservationsValue.Snapshot().Leases.at(Key);W.Key(L.Source);W.U32(L.Cluster);W.U8(L.Sample);W.U32(L.Task);W.Identity(L.Token);W.U32(L.Node);W.U64(L.Version);}
            Value=W.Take();
        }
        else Value=CaptureFragment(K);
        if (Value.empty()) return {};Out.push_back({K,1,Version(K),std::move(Value)});
    }
    return Out;
}
Bytes OwnerState::ExportCurrentIdentity(std::span<const ObjectKey> Objects) const
{
    Writer W(MaxPrivateStateBytes);W.U32(static_cast<std::uint32_t>(AnnouncedOrdinals.size()));
    for (const auto& [K,N]:AnnouncedOrdinals) {W.U8(static_cast<std::uint8_t>(K.first));W.U32((K.second<<24)|N);}
    // Ordinal high-water marks are bounded by kind/player slot. Keep generation
    // guards only for current identities and their referenced parent chains;
    // completed unrelated work must not become an ever-growing second history.
    std::set<std::pair<ObjectKind,Id>> Required;
    for (auto K:Objects)
    {
        std::set<ObjectKey> Seen;
        while (K.Valid()&&Seen.size()<64&&Seen.insert(K).second)
        {Required.emplace(K.Kind,K.StableId);const auto* O=FindRecord(K);if (!O) break;K=O->Parent;}
    }
    std::uint32_t Count=0;for (const auto& K:Required) if (RetiredLives.contains(K)) ++Count;
    W.U32(Count);
    for (const auto& K:Required) if (const auto I=RetiredLives.find(K);I!=RetiredLives.end())
    {W.U8(static_cast<std::uint8_t>(K.first));W.Identity(K.second);W.U32(I->second);}
    return W.Take();
}
bool OwnerState::MergeCurrentIdentity(std::span<const std::uint8_t> Data)
{
    Reader R(Data);std::uint32_t N;
    std::vector<std::pair<ObjectKind,std::uint32_t>> Ordinals;
    std::map<std::pair<ObjectKind,Id>,std::uint32_t> Retired;
    if (!R.U32(N)||N>65536) return false;
    for (std::uint32_t I=0;I<N;++I)
    {std::uint8_t Kind;std::uint32_t Ordinal;if (!R.U8(Kind)||Kind>unsigned(ObjectKind::BusinessWork)||!R.U32(Ordinal)||!Ordinal) return false;
        Ordinals.emplace_back(ObjectKind(Kind),Ordinal);}
    if (!R.U32(N)||N>1000000) return false;
    for (std::uint32_t I=0;I<N;++I)
    {std::uint8_t Kind;Id Stable;std::uint32_t Life;if (!R.U8(Kind)||Kind>unsigned(ObjectKind::BusinessWork)||!R.Identity(Stable)||!Stable.Valid()
        ||!R.U32(Life)||!Life||!Retired.emplace(std::pair{ObjectKind(Kind),Stable},Life).second) return false;}
    if (!R.Done()) return false;
    for (const auto& [Kind,Ordinal]:Ordinals) ObserveObjectOrdinal(Kind,Ordinal);
    for (const auto& [K,Life]:Retired) if (Life>RetiredLives[K]) {RetiredLives[K]=Life;RetiredLifeChanges[K]=Life;}
    return true;
}
bool OwnerState::ReplaceCurrent(Id Source,std::span<const ObjectKey> Objects,const std::vector<StateFragment>& Fragments,std::span<const std::uint8_t> Identity,std::string* CurrentStateFailure)
{
    TraceScope Trace("GuLiHybrid_ResourceSnapshotInstallNumeric");
    if (CurrentStateFailure) CurrentStateFailure->clear();
    const auto Fail=[&](std::string Reason){if (CurrentStateFailure) *CurrentStateFailure=std::move(Reason);return false;};
    const std::set<ObjectKey> Scope(Objects.begin(),Objects.end());
    if (!Source.Valid()||Scope.empty()||Scope.size()!=Objects.size()) return Fail("scope");
    std::set<StateKey> Seen;std::map<ObjectKey,ObjectRecord> Records;
    for (const auto& F:Fragments)
    {
        if (!Scope.contains(F.Key.Object)||F.Schema!=1||F.Key.Capability.starts_with("event/")||!Seen.insert(F.Key).second) return Fail("fragment-scope/"+F.Key.Capability);
        if (F.Key.Capability=="object")
        {Reader R(F.Value);ObjectRecord O;if (!ReadObject(R,O)||!R.Done()||O.Grant.Object!=F.Key.Object||!HasPrivateCustody(O.Grant,Source)) return Fail("object/"+F.Key.Object.StableId.ToString());Records.emplace(F.Key.Object,std::move(O));}
    }
    if (Records.size()!=Scope.size()) return Fail("missing-objects");
    // A valid checksum is not a completeness guarantee: require every current
    // capability (including empty inventories and task slots) before any write.
    for (const auto& K:FragmentKeys(Objects,false)) if (!Seen.contains(K)) return Fail("missing-capability/"+K.Capability+"/"+K.Object.StableId.ToString());
    OwnerState Trial=Stage(nullptr,&Fragments);
    if (!Identity.empty()&&!Trial.MergeCurrentIdentity(Identity)) return Fail("identity");
    for (const auto K:Scope)
    {
        if (Busy(K)) return Fail("locked/"+K.StableId.ToString());
        // A selected complete snapshot may precede our latest replica. Replace,
        // rather than merge, values and logical age; Coordinator grants stay current.
        auto O=Records.at(K);
        if (const auto Old=Directory.find(K);Old!=Directory.end())
        {const auto Life=O.Grant.State;const auto Team=O.Grant.Faction;O.Grant=Old->second.Grant;O.Grant.State=Life;O.Grant.Faction=Team;}
        Trial.Directory[K]=O;Trial.IndexRecord(O);
        Trial.ActiveWorks.erase(K);
    }
    if (!Trial.ApplyValidated(Fragments,CurrentStateFailure)) return false;
    // Validation used only the affected current state. Do not copy completed
    // transaction history into a trial world as part of snapshot restoration.
    std::erase_if(Versions,[&](const auto& P){return Scope.contains(P.first.Object);});
    std::erase_if(Attacks,[&](const auto& P){return Scope.contains(P.second.Source);});
    std::erase_if(LastAttack,[&](const auto& P){return Scope.contains(P.first.first);});
    for (const auto& [K,O]:Records)
    {Directory[K]=Trial.Directory.at(K);Directory[K].LogicalAge=O.LogicalAge;IndexRecord(Directory[K]);HistoryFragments.erase(K);ActiveWorks.erase(K);}
    if ((!Identity.empty()&&!MergeCurrentIdentity(Identity))||!ApplyValidated(Fragments,CurrentStateFailure))
    {Abort("Validated resource snapshot application failed");return false;}
    for (const auto& [K,O]:Records) Directory.at(K).LogicalAge=O.LogicalAge;
    FragmentCache.clear();FragmentCacheBytes=0;return true;
}
}
