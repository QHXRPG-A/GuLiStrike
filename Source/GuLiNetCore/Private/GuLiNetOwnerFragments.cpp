#include "GuLiNetOwnerState.h"
#include "GuLiNetHash.h"
#include "GuLiNetTrace.h"
#include <algorithm>
#include <charconv>

namespace GuLiNet
{
namespace
{
bool Count(Reader& R,std::uint32_t& N,std::uint32_t Maximum=100000) {return R.U32(N) && N<=Maximum;}
bool SuffixId(const std::string& Name,const char* Prefix,Id& Out)
{return Name.starts_with(Prefix) && Id::Parse(Name.substr(std::char_traits<char>::length(Prefix)),Out);}
bool SuffixNumber(const std::string& Name,const char* Prefix,std::uint32_t& Out)
{
    if (!Name.starts_with(Prefix)) return false;
    const auto Begin=Name.data()+std::char_traits<char>::length(Prefix),End=Name.data()+Name.size();
    const auto V=std::from_chars(Begin,End,Out);return V.ec==std::errc{} && V.ptr==End;
}
void WriteReservation(Writer& W,const Reservation& V)
{W.Identity(V.Token);W.Identity(V.Key.Account);W.U64(V.Key.Request);W.U8(static_cast<std::uint8_t>(V.Faction));WriteResources(W,V.Cost);}
bool ReadReservation(Reader& R,Reservation& V)
{
    std::uint8_t F;
    if (!R.Identity(V.Token) || !R.Identity(V.Key.Account) || !R.U64(V.Key.Request) || !R.U8(F) || F<1 || F>2 || !ReadResources(R,V.Cost)) return false;
    V.Faction=static_cast<Team>(F);return true;
}
void WriteLease(Writer& W,const MiningPositionLease& V)
{W.Key(V.Source);W.U32(V.Cluster);W.U8(V.Sample);W.U32(V.Task);W.Identity(V.Token);W.U32(V.Node);W.U64(V.Version);}
bool ReadLease(Reader& R,MiningPositionLease& V)
{return R.Key(V.Source) && R.U32(V.Cluster) && R.U8(V.Sample) && R.U32(V.Task) && R.Identity(V.Token) && R.U32(V.Node) && R.U64(V.Version);}
}
ObjectKey TeamAccountObject(Team F) {return {ObjectKind::BusinessWork,StableId("GuLiTeamAccount/v1",std::to_string(static_cast<unsigned>(F))),1};}
ObjectKey ResourceNodeObject(std::uint32_t N) {return {ObjectKind::ResourceNode,StableId("GuLiResourceNode/v1",std::to_string(N)),1};}
ObjectKey MiningClusterObject(std::uint32_t N) {return {ObjectKind::ResourceNode,StableId("GuLiResourceCluster/v1",std::to_string(N)),1};}
ObjectKey TerritoryStateObject(std::uint32_t N) {return {ObjectKind::Territory,StableId("GuLiTerritoryAccount/v1",std::to_string(N)),1};}
void WriteStateFragment(Writer& W,const StateFragment& V)
{W.Key(V.Key.Object);W.Text(V.Key.Capability);W.U32(V.Schema);W.U64(V.Version);W.Blob(V.Value);}
bool ReadStateFragment(Reader& R,StateFragment& V)
{return R.Key(V.Key.Object) && V.Key.Object.Valid() && R.Text(V.Key.Capability,512) && !V.Key.Capability.empty()
    && R.U32(V.Schema) && V.Schema==1 && R.U64(V.Version) && R.Blob(V.Value);}
Bytes EncodeOwnerWork(const OwnerWork& V)
{
    TraceScope Scope("GuLiHybrid_CoreEncodeWork");
    Writer W;W.U32(HandoffVersion);W.Identity(V.Event);W.Identity(V.Initiator);W.Identity(V.Proposal);W.U8(V.Continuation);W.Key(V.Source);
    Bytes Request;if (!EncodeMessage({V.Request.Header,EncodeTransaction(V.Request)},Request)) return {};
    W.Blob(Request);W.Blob(EncodeCommit(V.Result));W.U32(static_cast<std::uint32_t>(V.Changes.size()));
    for (const auto& M:V.Changes) {WriteStateFragment(W,M.Before);WriteStateFragment(W,M.After);W.U64(M.OwnerVersion);}
    return W.Take();
}
bool DecodeOwnerWork(std::span<const std::uint8_t> Bytes,OwnerWork& Out)
{
    Reader R(Bytes);OwnerWork V;std::uint32_t Schema,N;std::uint8_t Continuing;GuLiNet::Bytes Request,Result;Message M;
    if (!R.U32(Schema) || Schema!=HandoffVersion || !R.Identity(V.Event) || !V.Event.Valid()
        || !R.Identity(V.Initiator) || !V.Initiator.Valid() || !R.Identity(V.Proposal) || !V.Proposal.Valid() || !R.U8(Continuing) || Continuing>1 || !R.Key(V.Source) || !V.Source.Valid()
        || !R.Blob(Request) || !DecodeMessage(Request,M) || !DecodeTransaction(M.Header,M.Body,V.Request)
        || V.Request.Header.Event!=V.Event || !R.Blob(Result) || !DecodeCommit(Result,V.Result)
        || V.Result.Event!=V.Event || !Count(R,N,4096)) return false;
    std::set<StateKey> Keys;
    for (std::uint32_t I=0;I<N;++I)
    {
        StateMutation E;
        if (!ReadStateFragment(R,E.Before) || !ReadStateFragment(R,E.After) || E.Before.Key!=E.After.Key
            || E.Before.Version==UINT64_MAX || E.After.Version!=E.Before.Version+1 || !R.U64(E.OwnerVersion)
            || !Keys.insert(E.After.Key).second) return false;
        V.Changes.push_back(std::move(E));
    }
    if (!R.Done()) return false;V.Continuation=Continuing!=0;Out=std::move(V);return true;
}
Revision OwnerState::Version(const StateKey& K) const
{const auto I=Versions.find(K);return I==Versions.end()?(StageSource?StageSource->Version(K):0):I->second;}
Bytes OwnerState::CaptureFragment(const StateKey& K) const
{
    TraceScope Scope("GuLiHybrid_CoreCaptureFragment");
    if (!ReducedWork||StageSource||(K.Capability!="account"&&!K.Capability.starts_with("leases/"))) return CaptureFragmentUncached(K);
    const Revision DataVersion=K.Capability=="account"?EconomyLedger.Balance(K.Object==TeamAccountObject(Team::Red)?Team::Red:Team::Blue).Version:MiningReservationsValue.Snapshot().Version;
    const Revision Current=Version(K);const auto I=FragmentCache.find(K);
    if (I!=FragmentCache.end()&&I->second.Version==Current&&I->second.DataVersion==DataVersion) return I->second.Value;
    auto Value=CaptureFragmentUncached(K);constexpr std::size_t Budget=8ull*1024*1024;
    if (Value.size()<=Budget)
    {
        if (I!=FragmentCache.end()) {FragmentCacheBytes-=I->second.Value.size();FragmentCache.erase(I);}
        if (FragmentCacheBytes>Budget-Value.size()) {FragmentCache.clear();FragmentCacheBytes=0;}
        FragmentCacheBytes+=Value.size();FragmentCache[K]={Current,DataVersion,Value};
    }
    return Value;
}
Bytes OwnerState::CaptureFragmentUncached(const StateKey& K) const
{
    Writer W;const auto* O=FindRecord(K.Object);const auto& N=K.Capability;
    if (N=="object") {if (!O) return {};WriteObject(W,*O);return W.Take();}
    if (N=="account")
    {
        const Team F=K.Object==TeamAccountObject(Team::Red)?Team::Red:Team::Blue;
        if (K.Object!=TeamAccountObject(F)) return {};
        W.U8(static_cast<std::uint8_t>(F));WriteResources(W,EconomyLedger.Balance(F));
        const auto& E=EconomyLedger.Snapshot();std::uint32_t Size=0;
        for (const auto& [Key,V]:E.Pending) if (V.Faction==F) ++Size;
        W.U32(Size);for (const auto& [Key,V]:E.Pending) if (V.Faction==F) WriteReservation(W,V);
        const auto Finals=FinalizedAccounts.find(F);W.U32(Finals==FinalizedAccounts.end()?0:static_cast<std::uint32_t>(Finals->second.size()));
        if (Finals!=FinalizedAccounts.end()) for (const auto& Key:Finals->second) {W.Identity(Key.Account);W.U64(Key.Request);}
        return W.Take();
    }
    const auto& Resources=ResourceLedgerValue.Snapshot();
    if (N=="cargo")
    {
        const auto I=Resources.Miners.find(K.Object);if (I==Resources.Miners.end()) return {};const auto& V=I->second;
        W.U8(static_cast<std::uint8_t>(V.Faction));W.U32(V.Capacity);WriteResources(W,V.Cargo);
        W.U64(V.ExtractionPeriod);W.U64(V.NextExtraction);W.U64(V.Version);return W.Take();
    }
    if (N=="processor")
    {
        const auto I=Resources.Factories.find(K.Object);if (I==Resources.Factories.end()) return {};const auto& V=I->second;
        W.U8(static_cast<std::uint8_t>(V.Faction));W.U32(V.Capacity);W.U64(V.ProcessingPeriod);W.U64(V.NextProcessing);W.U64(V.Version);
        W.U32(static_cast<std::uint32_t>(V.Queue.size()));
        for (const auto& B:V.Queue) {W.Identity(B.Event);W.Key(B.Source);W.U8(static_cast<std::uint8_t>(B.Beneficiary));WriteResources(W,B.Amount);}
        WriteResources(W,V.Processed);
        const auto Held=HeldFactoryCapacity.find(K.Object);W.U64(Held==HeldFactoryCapacity.end()?0:Held->second);return W.Take();
    }
    std::uint32_t Index=0;
    if (SuffixNumber(N,"ore/",Index))
    {
        const auto I=Resources.Nodes.find(Index);
        if (I==Resources.Nodes.end() || MiningClusterObject(I->second.Cluster)!=K.Object) return {};
        W.U8(Resources.Remaining.at(Index));const auto Held=HeldOre.find(Index);W.U32(Held==HeldOre.end()?0:Held->second);return W.Take();
    }
    if (SuffixNumber(N,"territory/",Index))
    {
        if (K.Object!=TerritoryStateObject(Index) || Index>=Resources.Owners.size()) return {};
        W.U8(static_cast<std::uint8_t>(Resources.Owners[Index]));return W.Take();
    }
    if (SuffixNumber(N,"leases/",Index))
    {
        if (K.Object!=MiningClusterObject(Index)) return {};
        const auto& S=MiningReservationsValue.Snapshot();std::uint32_t Size=0;
        if (ReducedWork)
        {
            const auto& Leases=MiningReservationsValue.LeasesForCluster(Index);const auto& Decisions=MiningReservationsValue.DecisionsForCluster(Index);
            W.U32(static_cast<std::uint32_t>(Leases.size()));for (const auto& Key:Leases) WriteLease(W,S.Leases.at(Key));
            W.U32(static_cast<std::uint32_t>(Decisions.size()));for (const auto E:Decisions)
            {const auto& D=S.Decisions.at(E);W.Identity(E);W.Blob(D.Fingerprint);W.Blob(EncodeMiningReservationOutcome(D.Outcome));}
            return W.Take();
        }
        for (const auto& [Key,V]:S.Leases) if (V.Cluster==Index) ++Size;
        W.U32(Size);for (const auto& [Key,V]:S.Leases) if (V.Cluster==Index) WriteLease(W,V);
        Size=0;for (const auto& [E,D]:S.Decisions) if (D.Outcome.Lease.Cluster==Index) ++Size;
        W.U32(Size);for (const auto& [E,D]:S.Decisions) if (D.Outcome.Lease.Cluster==Index)
        {W.Identity(E);W.Blob(D.Fingerprint);W.Blob(EncodeMiningReservationOutcome(D.Outcome));}
        return W.Take();
    }
    if (N=="tasks")
    {
        const auto I=ActiveWorks.find(K.Object);W.U8(I==ActiveWorks.end()?0:1);
        if (I!=ActiveWorks.end()) W.Key(I->second);
        return W.Take();
    }
    Id Event;
    if (SuffixId(N,"event/",Event))
    {
        const auto I=Results.find(Event);
        if (I==Results.end() || ResultDomains.at(Event)!=K.Object) return {};
        W.Blob(EncodeCommit(I->second));W.Blob(RequestBodies.at(Event));return W.Take();
    }
    if (SuffixId(N,"attack/",Event))
    {
        const auto I=Attacks.find(Event);if (I==Attacks.end() || I->second.Source!=K.Object) return {};const auto& A=I->second;
        W.Text(A.RuleId);W.U64(A.Started);W.F32(A.Damage);W.U64(A.Cooldown);W.U64(A.Lifetime);W.U32(A.MaximumTargets);
        W.U32(static_cast<std::uint32_t>(A.Targets.size()));for (const auto& T:A.Targets) W.Key(T);return W.Take();
    }
    if (N.starts_with("cooldown/"))
    {
        const auto I=LastAttack.find({K.Object,N.substr(9)});if (I==LastAttack.end()) return {};
        W.U64(I->second);return W.Take();
    }
    return {};
}
bool OwnerState::UsesResourceLedger(const StateFragment& F)
{
    const auto& N=F.Key.Capability;
    return N=="cargo" || N=="processor" || N.starts_with("ore/") || N.starts_with("territory/");
}
bool OwnerState::ApplyFragment(const StateFragment& F,ResourceLedgerState* ResourceBatch)
{
    if (F.Schema!=1 || !F.Key.Object.Valid() || F.Key.Capability.empty()) return false;
    const auto& K=F.Key;const auto& N=K.Capability;Reader R(F.Value);
    std::optional<ResourceLedgerState> SingleResources;
    if (!ResourceBatch && UsesResourceLedger(F)) SingleResources=ResourceLedgerValue.Snapshot();
    auto* LedgerState=ResourceBatch?ResourceBatch:(SingleResources?&*SingleResources:nullptr);
    if (N=="object")
    {
        ObjectRecord O;if (!ReadObject(R,O) || !R.Done() || O.Grant.Object!=K.Object || !UpgradeStateAllowed(O)) return false;
        // A gameplay publication cannot revoke a newer coordinator grant.
        if (const auto Old=Directory.find(K.Object);Old!=Directory.end())
        {
            const auto State=O.Grant.State;const auto Faction=O.Grant.Faction;
            O.Grant=Old->second.Grant;O.Grant.State=State;O.Grant.Faction=Faction;
            O.LogicalAge=std::max(O.LogicalAge,Old->second.LogicalAge);
        }
        IndexRecord(O);Directory.insert_or_assign(K.Object,std::move(O));
    }
    else if (N=="account")
    {
        std::uint8_t TeamValue;Resources Balance;std::uint32_t Size;
        if (!R.U8(TeamValue) || TeamValue<1 || TeamValue>2 || K.Object!=TeamAccountObject(static_cast<Team>(TeamValue))
            || !ReadResources(R,Balance) || !Count(R,Size)) return false;
        const auto Team=static_cast<GuLiNet::Team>(TeamValue);auto E=EconomyLedger.Snapshot();
        for (auto I=E.Pending.begin();I!=E.Pending.end();) if (I->second.Faction==Team) I=E.Pending.erase(I);else ++I;
        for (std::uint32_t I=0;I<Size;++I) {Reservation V;if (!ReadReservation(R,V) || V.Faction!=Team || !E.Pending.emplace(V.Key,V).second) return false;}
        if (!Count(R,Size,1000000)) return false;
        for (const auto& Key:FinalizedAccounts[Team]) E.Finalized.erase(Key);
        std::set<EconomyKey> Finals;
        for (std::uint32_t I=0;I<Size;++I) {EconomyKey Key;if (!R.Identity(Key.Account) || !R.U64(Key.Request) || !Finals.insert(Key).second) return false;E.Finalized.insert(Key);}
        if (!R.Done()) return false;
        (Team==Team::Red?E.Red:E.Blue)=Balance;
        if (!EconomyLedger.Restore(std::move(E))) return false;FinalizedAccounts[Team]=std::move(Finals);
    }
    else if (N=="cargo" || N=="processor")
    {
        auto& S=*LedgerState;std::uint8_t Team;std::uint32_t Size;
        if (N=="cargo")
        {
            MinerInventory V;
            if (!R.U8(Team) || Team<1 || Team>2 || !R.U32(V.Capacity) || !ReadResources(R,V.Cargo)
                || !R.U64(V.ExtractionPeriod) || !R.U64(V.NextExtraction) || !R.U64(V.Version) || !R.Done()) return false;
            V.Faction=static_cast<GuLiNet::Team>(Team);S.Miners[K.Object]=V;
        }
        else
        {
            FactoryInventory V;std::uint64_t Held;
            if (!R.U8(Team) || Team<1 || Team>2 || !R.U32(V.Capacity) || !R.U64(V.ProcessingPeriod)
                || !R.U64(V.NextProcessing) || !R.U64(V.Version) || !Count(R,Size,4096)) return false;
            V.Faction=static_cast<GuLiNet::Team>(Team);
            for (std::uint32_t I=0;I<Size;++I)
            {
                RawDelivery B;
                if (!R.Identity(B.Event) || !R.Key(B.Source) || !R.U8(Team) || Team<1 || Team>2 || !ReadResources(R,B.Amount)) return false;
                B.Beneficiary=static_cast<GuLiNet::Team>(Team);V.Queue.push_back(B);
            }
            if (!ReadResources(R,V.Processed) || !R.U64(Held) || !R.Done() || Held+ResourceLedger::Queued(V)>V.Capacity) return false;
            if (Held) HeldFactoryCapacity[K.Object]=Held;else HeldFactoryCapacity.erase(K.Object);
            S.Factories[K.Object]=std::move(V);
            std::int64_t Blue=0,Red=0;
            for (const auto& [Object,Factory]:S.Factories) {Blue+=Factory.Processed.Blue;Red+=Factory.Processed.Red;}
            if (Blue>INT32_MAX || Red>INT32_MAX) return false;
            S.Processed.Blue=static_cast<std::int32_t>(Blue);S.Processed.Red=static_cast<std::int32_t>(Red);
        }
        S.Version=std::max(S.Version,F.Version);
    }
    else if (N=="tasks")
    {
        std::uint8_t Present;ObjectKey Work;
        if (!R.U8(Present) || Present>1 || (Present && (!R.Key(Work) || !Work.Valid())) || !R.Done()) return false;
        if (Present) ActiveWorks[K.Object]=Work;else ActiveWorks.erase(K.Object);
    }
    else
    {
        std::uint32_t Index=0;Id Event;
        if (SuffixNumber(N,"ore/",Index))
        {
            auto& S=*LedgerState;const auto I=S.Nodes.find(Index);std::uint8_t Remaining;std::uint32_t Held;
            if (I==S.Nodes.end() || K.Object!=MiningClusterObject(I->second.Cluster) || !R.U8(Remaining) || Remaining>I->second.Initial
                || !R.U32(Held) || Held>Remaining || !R.Done()) return false;
            S.Remaining[Index]=Remaining;if (Held) HeldOre[Index]=Held;else HeldOre.erase(Index);
            S.Version=std::max(S.Version,F.Version);
        }
        else if (SuffixNumber(N,"territory/",Index))
        {
            auto& S=*LedgerState;std::uint8_t Owner;
            if (K.Object!=TerritoryStateObject(Index) || Index>=S.Owners.size() || !R.U8(Owner) || Owner>2 || !R.Done()) return false;
            S.Owners[Index]=static_cast<Team>(Owner);S.Version=std::max(S.Version,F.Version);
        }
        else if (SuffixNumber(N,"leases/",Index))
        {
            if (K.Object!=MiningClusterObject(Index)) return false;
            auto S=MiningReservationsValue.Snapshot();std::uint32_t Size;
            if (ReducedWork)
            {for (const auto K0:MiningReservationsValue.LeasesForCluster(Index)) S.Leases.erase(K0);for (const auto E:MiningReservationsValue.DecisionsForCluster(Index)) S.Decisions.erase(E);}
            else
            {
                for (auto I=S.Leases.begin();I!=S.Leases.end();) if (I->second.Cluster==Index) I=S.Leases.erase(I);else ++I;
                for (auto I=S.Decisions.begin();I!=S.Decisions.end();) if (I->second.Outcome.Lease.Cluster==Index) I=S.Decisions.erase(I);else ++I;
            }
            if (!Count(R,Size)) return false;
            for (std::uint32_t I=0;I<Size;++I)
            {MiningPositionLease V;if (!ReadLease(R,V) || V.Cluster!=Index || !S.Leases.emplace(V.Source,V).second) return false;S.Version=std::max(S.Version,V.Version);}
            if (!Count(R,Size)) return false;
            for (std::uint32_t I=0;I<Size;++I)
            {
                Id E;MiningReservationDecision V;Bytes B;
                if (!R.Identity(E) || !R.Blob(V.Fingerprint,160) || !R.Blob(B,512) || !DecodeMiningReservationOutcome(B,V.Outcome)
                    || V.Outcome.Lease.Cluster!=Index || V.Outcome.Event!=E || !S.Decisions.emplace(E,V).second) return false;
                S.Version=std::max(S.Version,V.Outcome.Version);
            }
            if (!R.Done() || !MiningReservationsValue.Restore(std::move(S))) return false;
        }
        else if (SuffixId(N,"event/",Event))
        {
            CommitResult V;Bytes B,Request;
            if (!R.Blob(B) || !DecodeCommit(B,V) || V.Event!=Event || V.StateDomain!=K.Object
                || !R.Blob(Request) || !R.Done()) return false;
            Results.insert_or_assign(Event,std::move(V));RequestBodies.insert_or_assign(Event,std::move(Request));IndexResultDomain(Event,K.Object);
        }
        else if (SuffixId(N,"attack/",Event))
        {
            Attack A;A.Source=K.Object;std::uint32_t Size;
            if (!R.Text(A.RuleId,256) || !R.U64(A.Started) || !R.F32(A.Damage) || !R.U64(A.Cooldown)
                || !R.U64(A.Lifetime) || !R.U32(A.MaximumTargets) || !Count(R,Size,MaximumEffectTargets)) return false;
            for (std::uint32_t I=0;I<Size;++I) {ObjectKey T;if (!R.Key(T) || !A.Targets.insert(T).second) return false;}
            if (!R.Done() || A.Targets.size()>A.MaximumTargets || !Rules.contains(A.RuleId)) return false;
            IndexAttackDomain(Event,A.Source);Attacks.insert_or_assign(Event,std::move(A));
        }
        else if (N.starts_with("cooldown/"))
        {Tick At;if (!R.U64(At) || !R.Done() || !Rules.contains(N.substr(9))) return false;
         LastAttack[{K.Object,N.substr(9)}]=At;HistoryFragments[K.Object].insert(N);}
        else return false;
    }
    if (SingleResources && !ResourceLedgerValue.Restore(std::move(*SingleResources))) return false;
    Versions[K]=F.Version;return true;
}
std::set<StateKey> OwnerState::FragmentKeys(std::span<const ObjectKey> Keys) const
{
    std::set<StateKey> Out;const std::set<ObjectKey> Objects(Keys.begin(),Keys.end());const auto& R=ResourceLedgerValue.Snapshot();
    for (const auto& K:Keys)
    {
        if (!Directory.contains(K)) continue;Out.insert({K,"object"});Out.insert({K,"tasks"});
        if (K==TeamAccountObject(Team::Red) || K==TeamAccountObject(Team::Blue)) Out.insert({K,"account"});
        if (R.Miners.contains(K)) Out.insert({K,"cargo"});if (R.Factories.contains(K)) Out.insert({K,"processor"});
    }
    // Ore and territory fragments cannot belong to a flight, player or unit.
    // A leave exports hundreds of individual flights: hashing all 6240 ore
    // keys for each cut can exhaust the unchanged membership deadline.
    for (const auto& K:Keys) if (K.Kind==ObjectKind::ResourceNode)
        if (const auto I=IndexedSource().ResourceClusterNodes.find(K);I!=IndexedSource().ResourceClusterNodes.end())
        {
            for (const auto Node:I->second) Out.insert({K,"ore/"+std::to_string(Node)});
            if (!I->second.empty()) Out.insert({K,"leases/"+std::to_string(IndexedSource().ResourceLedgerValue.Snapshot().Nodes.at(I->second.front()).Cluster)});
        }
    if (std::any_of(Keys.begin(),Keys.end(),[](const auto& K){return K.Kind==ObjectKind::Territory;}))
        for (std::uint32_t I=0;I<R.Owners.size();++I) if (Objects.contains(TerritoryStateObject(I))) Out.insert({TerritoryStateObject(I),"territory/"+std::to_string(I)});
    for (const auto& K:Objects) if (const auto History=HistoryFragments.find(K);History!=HistoryFragments.end())
        for (const auto& Name:History->second) Out.insert({K,Name});
    return Out;
}
std::vector<StateFragment> OwnerState::Export(std::span<const ObjectKey> Objects) const
{
    std::vector<StateFragment> Out;
    for (const auto& K:FragmentKeys(Objects)) Out.push_back({K,1,Version(K),CaptureFragment(K)});
    return Out;
}
std::vector<StateFragment> OwnerState::Published(const OwnerWork& Work) const
{
    TraceScope Scope("GuLiHybrid_CorePublishedFragments");
    std::vector<StateFragment> Out;
    for (const auto& M:Work.Changes) if (MutationOwner(Work,M)==Player)
        Out.push_back({M.After.Key,1,Version(M.After.Key),CaptureFragment(M.After.Key)});
    return Out;
}
bool OwnerState::RebuildWorkIndex()
{
    TraceScope Scope("GuLiHybrid_CoreRebuildWorkIndex");
    ActiveWorkEvents.clear();
    for (const auto& [Source,Key]:ActiveWorks)
    {
        const auto* W=FindRecord(Key);if (!W) continue;
        FrozenWorkRecipe Recipe;if (!DecodeFrozenWork(*W,Recipe) || W->Parent!=Source) return false;
        if (!ActiveWorkEvents.emplace(Recipe.Request.Header.Event,Key).second) return false;
    }
    return true;
}
}
