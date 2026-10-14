#include "GuLiNetResourceLedger.h"
#include <algorithm>
#include <limits>

namespace GuLiNet
{
namespace
{
constexpr std::uint32_t MaximumNodes=100000, MaximumActors=100000, MaximumDecisions=65536, MaximumQueue=4096;
constexpr std::uint32_t ResourceVersion=2;
bool Empty(const Resources& V) { return V.Blue==0 && V.Red==0; }
std::uint64_t Total(const Resources& V) { return static_cast<std::uint64_t>(V.Blue)+V.Red; }
bool TeamValue(Team V) { return V<=Team::Blue; }
bool InventoryKey(ObjectKey K) { return K.Valid(); }
bool Count(Reader& R,std::uint32_t& N,std::uint32_t Limit) { return R.U32(N) && N<=Limit; }
void WriteOutcome(Writer& W,const ResourceOutcome& V)
{
    W.Identity(V.Event); W.U8(static_cast<std::uint8_t>(V.Operation)); W.U8(static_cast<std::uint8_t>(V.Status));
    W.U64(V.Version); W.Key(V.Source); W.Key(V.Target); W.U32(V.Node); W.U8(V.Remaining);
    WriteResources(W,V.Cargo); WriteResources(W,V.Credit); WriteResources(W,V.RawAmount); W.U8(static_cast<std::uint8_t>(V.Beneficiary)); W.Identity(V.Delivery);
}
bool ReadOutcome(Reader& R,ResourceOutcome& V)
{
    std::uint8_t Operation,Status,Beneficiary;
    if (!R.Identity(V.Event) || !R.U8(Operation) || Operation>2 || !R.U8(Status)
        || Status>static_cast<std::uint8_t>(ResourceStatus::Deferred) || !R.U64(V.Version)
        || !R.Key(V.Source) || !R.Key(V.Target) || !R.U32(V.Node) || !R.U8(V.Remaining) || V.Remaining>3
        || !ReadResources(R,V.Cargo) || !ReadResources(R,V.Credit) || !ReadResources(R,V.RawAmount) || !R.U8(Beneficiary) || Beneficiary>2
        || !R.Identity(V.Delivery)) return false;
    V.Operation=static_cast<ResourceOperation>(Operation); V.Status=static_cast<ResourceStatus>(Status);
    V.Beneficiary=static_cast<Team>(Beneficiary); return true;
}
}
std::uint64_t ResourceLedger::Queued(const FactoryInventory& F)
{
    std::uint64_t N=0; for (const auto& V:F.Queue) N+=Total(V.Amount); return N;
}
bool ValidResourceLedger(const ResourceLedgerState& S)
{
    if (!S.Configured)
        return !S.Version && S.Nodes.empty() && S.Remaining.empty() && S.Owners.empty() && S.Miners.empty()
            && S.Factories.empty() && Empty(S.Processed) && !S.Processed.Version && S.Decisions.empty();
    if (S.Nodes.empty() || S.Nodes.size()>MaximumNodes || S.Remaining.size()!=S.Nodes.size()
        || S.Owners.empty() || S.Owners.size()>64 || S.Miners.size()>MaximumActors || S.Factories.size()>MaximumActors
        || S.Decisions.size()>MaximumDecisions || !S.Processed.Nonnegative() || S.Processed.Version) return false;
    for (const auto Owner:S.Owners) if (!TeamValue(Owner)) return false;
    for (const auto& [Node,V]:S.Nodes)
    {
        const auto Remaining=S.Remaining.find(Node);
        if (!Node || !V.Cluster || V.Territory>=S.Owners.size() || V.Type>Commodity::Red || !V.Initial || V.Initial>3
            || Remaining==S.Remaining.end() || Remaining->second>V.Initial) return false;
    }
    std::map<std::uint32_t,std::pair<std::uint32_t,Commodity>> Clusters;
    for (const auto& [Node,V]:S.Nodes)
    {
        (void)Node;
        const auto Definition=std::make_pair(V.Territory,V.Type);
        const auto [It,Added]=Clusters.emplace(V.Cluster,Definition);
        if (!Added && It->second!=Definition) return false;
    }
    std::set<Id> Deliveries;
    for (const auto& [K,V]:S.Miners)
    {
        if (!InventoryKey(K) || !Playable(V.Faction) || !V.Capacity || V.Capacity>1000000 || !V.ExtractionPeriod
            || V.ExtractionPeriod>TicksPerSecond*86400 || !V.Version || !V.Cargo.Nonnegative() || V.Cargo.Version
            || Total(V.Cargo)>V.Capacity) return false;
    }
    for (const auto& [K,V]:S.Factories)
    {
        if (!InventoryKey(K) || !Playable(V.Faction) || !V.Capacity || V.Capacity>10000000 || !V.ProcessingPeriod
            || V.ProcessingPeriod>TicksPerSecond*86400 || !V.Version || V.Queue.size()>MaximumQueue || ResourceLedger::Queued(V)>V.Capacity
            || !V.Processed.Nonnegative() || V.Processed.Version) return false;
        for (const auto& B:V.Queue)
        {
            if (!B.Event.Valid() || !InventoryKey(B.Source) || !Playable(B.Beneficiary)
                || !B.Amount.Nonnegative() || Empty(B.Amount) || B.Amount.Version || !Deliveries.insert(B.Event).second) return false;
        }
    }
    // Each inventory is validated independently. Cross-owner conservation is
    // enforced by its reserved work, never by summing asynchronous replicas.
    for (const auto& [Event,D]:S.Decisions)
    {
        const auto& V=D.Outcome;
        Reader Input(D.Fingerprint); std::uint8_t Op; ObjectKey Source,Target; std::uint32_t Node; Tick At;
        if (!Event.Valid() || V.Event!=Event || V.Operation>ResourceOperation::Process || V.Status>ResourceStatus::Deferred
            || !V.Version || V.Version>S.Version
            || V.Status==ResourceStatus::Duplicate || V.Status==ResourceStatus::MissingSource || V.Status==ResourceStatus::Conflict
            || V.Status==ResourceStatus::Deferred || D.Fingerprint.size()>128
            || !Input.U8(Op) || Op>2 || !Input.Key(Source) || !Input.Key(Target) || !Input.U32(Node) || !Input.U64(At) || !Input.Done()
            || Op!=static_cast<std::uint8_t>(V.Operation) || Source!=V.Source || Target!=V.Target || Node!=V.Node
            || (V.Operation==ResourceOperation::Process ? !S.Factories.contains(Source) : !S.Miners.contains(Source))
            || !V.Cargo.Nonnegative() || V.Cargo.Version || !V.Credit.Nonnegative() || V.Credit.Version
            || !V.RawAmount.Nonnegative() || V.RawAmount.Version
            || !TeamValue(V.Beneficiary)) return false;
        if (V.IsCommitted() && ((V.Operation==ResourceOperation::Extract && (!S.Nodes.contains(V.Node) || Total(V.RawAmount)!=1 || !Empty(V.Credit)))
            || (V.Operation==ResourceOperation::Deliver && (!S.Factories.contains(Target) || Empty(V.RawAmount) || !Empty(V.Credit)
                || !Playable(V.Beneficiary)))
            || (V.Operation==ResourceOperation::Process && (!Playable(V.Beneficiary) || Total(V.Credit)!=1
                || V.RawAmount!=V.Credit || !V.Delivery.Valid())))) return false;
    }
    return true;
}
ResourceLedgerState& ResourceLedger::WriteState()
{
    if (Storage.use_count()!=1) Storage=std::make_shared<ResourceLedgerState>(*Storage);
    return *Storage;
}
bool ResourceLedger::Configure(std::map<std::uint32_t,OreDefinition> Nodes,std::vector<Team> Owners)
{
    const auto& State=Snapshot();
    if (State.Configured) return false;
    ResourceLedgerState V; V.Configured=true; V.Nodes=std::move(Nodes); V.Owners=std::move(Owners);
    for (const auto& [Node,D]:V.Nodes) V.Remaining.emplace(Node,D.Initial);
    if (!ValidResourceLedger(V)) return false;
    Storage=std::make_shared<ResourceLedgerState>(std::move(V)); return true;
}
bool ResourceLedger::RegisterMiner(ObjectKey K,Team Faction,std::uint32_t Capacity,Tick Period)
{
    auto& State=WriteState();
    if (!State.Configured || !InventoryKey(K) || !Playable(Faction) || !Capacity || Capacity>1000000 || !Period
        || Period>TicksPerSecond*86400 || State.Miners.size()>=MaximumActors || State.Miners.contains(K)) return false;
    State.Miners.emplace(K,MinerInventory{Faction,Capacity,{},Period,0,1}); return true;
}
bool ResourceLedger::RegisterFactory(ObjectKey K,Team Faction,std::uint32_t Capacity,Tick Period)
{
    auto& State=WriteState();
    if (!State.Configured || !InventoryKey(K) || !Playable(Faction) || !Capacity || Capacity>10000000 || !Period
        || Period>TicksPerSecond*86400 || State.Factories.size()>=MaximumActors || State.Factories.contains(K)) return false;
    State.Factories.emplace(K,FactoryInventory{Faction,Capacity,Period,0,1,{}}); return true;
}
bool ResourceLedger::SetTerritoryOwner(std::uint32_t Territory,Team Owner)
{
    auto& State=WriteState();
    if (!State.Configured || Territory>=State.Owners.size() || !TeamValue(Owner) || State.Version==UINT64_MAX) return false;
    if (State.Owners[Territory]!=Owner) { State.Owners[Territory]=Owner; ++State.Version; } return true;
}
bool ResourceLedger::SetFactoryOwner(ObjectKey K,Team Owner)
{
    auto& State=WriteState();
    auto It=State.Factories.find(K);
    if (It==State.Factories.end() || !Playable(Owner) || It->second.Version==UINT64_MAX || State.Version==UINT64_MAX) return false;
    if (It->second.Faction!=Owner) { It->second.Faction=Owner; ++It->second.Version; ++State.Version; } return true;
}
bool ResourceLedger::SetInventoryOwner(ObjectKey K,Team Owner)
{
    auto& State=WriteState();
    const auto Cargo=State.Miners.find(K);const auto Processor=State.Factories.find(K);
    if (!State.Configured || !Playable(Owner) || (Cargo==State.Miners.end() && Processor==State.Factories.end())) return false;
    const bool ChangeCargo=Cargo!=State.Miners.end() && Cargo->second.Faction!=Owner;
    const bool ChangeProcessor=Processor!=State.Factories.end() && Processor->second.Faction!=Owner;
    if ((ChangeCargo || ChangeProcessor) && (State.Version==UINT64_MAX
        || (ChangeCargo && Cargo->second.Version==UINT64_MAX) || (ChangeProcessor && Processor->second.Version==UINT64_MAX))) return false;
    if (ChangeCargo) {Cargo->second.Faction=Owner;++Cargo->second.Version;}
    if (ChangeProcessor) {Processor->second.Faction=Owner;++Processor->second.Version;}
    if (ChangeCargo || ChangeProcessor) ++State.Version;
    return true;
}
ResourceOutcome ResourceLedger::Begin(ResourceOperation Operation,ObjectKey Source,ObjectKey Target,std::uint32_t Node,Id Event,Tick At,Bytes& Fingerprint) const
{
    const auto& State=Snapshot();
    ResourceOutcome V; V.Event=Event; V.Operation=Operation; V.Source=Source; V.Target=Target; V.Node=Node;
    Writer W(128); W.U8(static_cast<std::uint8_t>(Operation)); W.Key(Source); W.Key(Target); W.U32(Node); W.U64(At); Fingerprint=W.Take();
    if (!State.Configured || !Event.Valid() || State.Version==UINT64_MAX || Fingerprint.empty()) return V;
    const auto Old=State.Decisions.find(Event);
    if (Old!=State.Decisions.end())
    {
        if (Old->second.Fingerprint!=Fingerprint) { V.Status=ResourceStatus::Conflict; return V; }
        V=Old->second.Outcome; if (V.Status==ResourceStatus::Committed) V.Status=ResourceStatus::Duplicate; return V;
    }
    V.Status=ResourceStatus::Deferred; return V; // New, not yet admitted to a mutation.
}
ResourceOutcome ResourceLedger::Finish(ResourceOutcome V,Bytes Fingerprint,bool Durable)
{
    auto& State=WriteState();
    if (Durable)
    {
        V.Version=++State.Version; State.Decisions.emplace(V.Event,ResourceDecision{std::move(Fingerprint),V});
    }
    return V;
}
ResourceOutcome ResourceLedger::Preview(ResourceOperation Operation,ObjectKey K,ObjectKey Target,std::uint32_t Node,Id Event,Tick At,
    const ResourceOutcome* Admitted) const
{
    const auto& State=Snapshot();
    ResourceOutcome V;V.Operation=Operation;V.Source=K;V.Target=Target;V.Node=Node;V.Event=Event;
    auto Reject=[&](ResourceStatus Status){V.Status=Status;return V;};
    if (!State.Configured || !Event.Valid() || Operation>ResourceOperation::Process || State.Version==UINT64_MAX)
        return Reject(ResourceStatus::Invalid);
    if (State.Decisions.size()>=MaximumDecisions) return Reject(ResourceStatus::Deferred);
    if (Admitted && (!Admitted->IsCommitted() || Admitted->Operation!=Operation || Admitted->Source!=K
        || Admitted->Target!=Target || Admitted->Node!=Node || Admitted->Event!=Event)) return Reject(ResourceStatus::Invalid);
    if (Operation!=ResourceOperation::Process)
    {
        const auto Source=State.Miners.find(K);if (Source==State.Miners.end()) return Reject(ResourceStatus::MissingSource);
        const auto& S=Source->second;V.Cargo=S.Cargo;
        if (S.Version==UINT64_MAX) return Reject(ResourceStatus::Invalid);
        if (Operation==ResourceOperation::Extract)
        {
            const auto Ore=State.Nodes.find(Node);if (Ore==State.Nodes.end() || Target.Valid()) return Reject(ResourceStatus::Invalid);
            V.Remaining=State.Remaining.at(Node);const auto Owner=State.Owners[Ore->second.Territory];
            if (!Admitted && Playable(Owner) && Owner!=S.Faction) return Reject(ResourceStatus::Forbidden);
            if (At<S.NextExtraction) return Reject(ResourceStatus::TooEarly);
            if (!V.Remaining) return Reject(ResourceStatus::Depleted);
            if (Total(S.Cargo)>=S.Capacity) return Reject(ResourceStatus::Full);
            if (At>UINT64_MAX-S.ExtractionPeriod) return Reject(ResourceStatus::Invalid);
            const bool Blue=Ore->second.Type==Commodity::Blue;
            ++(Blue?V.Cargo.Blue:V.Cargo.Red);(Blue?V.RawAmount.Blue:V.RawAmount.Red)=1;--V.Remaining;
        }
        else
        {
            const auto Destination=State.Factories.find(Target);if (Destination==State.Factories.end() || Node) return Reject(ResourceStatus::Invalid);
            const auto& F=Destination->second;
            if (!Admitted && S.Faction!=F.Faction) return Reject(ResourceStatus::Forbidden);
            if (Empty(S.Cargo)) return Reject(ResourceStatus::Empty);
            if (F.Queue.size()>=MaximumQueue || Queued(F)+Total(S.Cargo)>F.Capacity) return Reject(ResourceStatus::Full);
            if (F.Version==UINT64_MAX) return Reject(ResourceStatus::Invalid);
            V.RawAmount=S.Cargo;V.Cargo={};V.Delivery=Event;V.Beneficiary=Admitted?Admitted->Beneficiary:S.Faction;
        }
    }
    else
    {
        const auto Source=State.Factories.find(K);if (Source==State.Factories.end()) return Reject(ResourceStatus::MissingSource);
        const auto& F=Source->second;
        if (Target!=K || Node || At>UINT64_MAX-F.ProcessingPeriod || F.Version==UINT64_MAX) return Reject(ResourceStatus::Invalid);
        if (F.Queue.empty()) return Reject(ResourceStatus::Empty);
        if (At<F.NextProcessing) return Reject(ResourceStatus::TooEarly);
        const auto& B=F.Queue.front();V.Beneficiary=B.Beneficiary;V.Delivery=B.Event;
        const bool Blue=B.Amount.Blue>0;(Blue?V.Credit.Blue:V.Credit.Red)=1;V.RawAmount=V.Credit;
        if ((Blue?State.Processed.Blue:State.Processed.Red)==INT32_MAX) return Reject(ResourceStatus::Invalid);
    }
    if (Admitted && (V.RawAmount!=Admitted->RawAmount || V.Cargo!=Admitted->Cargo || V.Credit!=Admitted->Credit
        || V.Beneficiary!=Admitted->Beneficiary || V.Delivery!=Admitted->Delivery)) return Reject(ResourceStatus::Conflict);
    V.Version=State.Version+1;V.Status=ResourceStatus::Committed;return V;
}
ResourceOutcome ResourceLedger::Extract(ObjectKey K,std::uint32_t Node,Id Event,Tick At,const ResourceOutcome* Admitted)
{
    auto& State=WriteState();
    Bytes Fingerprint;auto V=Begin(ResourceOperation::Extract,K,{},Node,Event,At,Fingerprint);
    if (V.Status!=ResourceStatus::Deferred) return V;
    V=Preview(ResourceOperation::Extract,K,{},Node,Event,At,Admitted);
    if (V.Status==ResourceStatus::MissingSource || V.Status==ResourceStatus::Deferred || V.Status==ResourceStatus::Conflict) return V;
    if (V.IsCommitted())
    {auto& S=State.Miners.at(K);S.Cargo=V.Cargo;S.NextExtraction=At+S.ExtractionPeriod;++S.Version;State.Remaining.at(Node)=V.Remaining;}
    return Finish(V,std::move(Fingerprint));
}
ResourceOutcome ResourceLedger::Deliver(ObjectKey K,ObjectKey Target,Id Event,Tick At,const ResourceOutcome* Admitted)
{
    auto& State=WriteState();
    Bytes Fingerprint;auto V=Begin(ResourceOperation::Deliver,K,Target,0,Event,At,Fingerprint);
    if (V.Status!=ResourceStatus::Deferred) return V;
    V=Preview(ResourceOperation::Deliver,K,Target,0,Event,At,Admitted);
    if (V.Status==ResourceStatus::MissingSource || V.Status==ResourceStatus::Deferred || V.Status==ResourceStatus::Conflict) return V;
    if (V.IsCommitted())
    {auto& S=State.Miners.at(K);auto& F=State.Factories.at(Target);F.Queue.push_back({Event,K,V.Beneficiary,V.RawAmount});S.Cargo={};++S.Version;++F.Version;}
    return Finish(V,std::move(Fingerprint));
}
ResourceOutcome ResourceLedger::Process(ObjectKey K,Id Event,Tick At,const std::function<bool(Team,const Resources&)>& Credit,const ResourceOutcome* Admitted)
{
    auto& State=WriteState();
    Bytes Fingerprint;auto V=Begin(ResourceOperation::Process,K,K,0,Event,At,Fingerprint);
    if (V.Status!=ResourceStatus::Deferred) return V;
    V=Preview(ResourceOperation::Process,K,K,0,Event,At,Admitted);
    if (V.Status==ResourceStatus::MissingSource || V.Status==ResourceStatus::Deferred || V.Status==ResourceStatus::Conflict) return V;
    if (V.IsCommitted())
    {
        if (!Credit || !Credit(V.Beneficiary,V.Credit)) {V.Status=ResourceStatus::Deferred;V.Version=0;return V;}
        auto& F=State.Factories.at(K);auto& B=F.Queue.front();const bool Blue=V.Credit.Blue>0;
        --(Blue?B.Amount.Blue:B.Amount.Red);++(Blue?State.Processed.Blue:State.Processed.Red);
        ++(Blue?F.Processed.Blue:F.Processed.Red);
        F.NextProcessing=At+F.ProcessingPeriod;++F.Version;if (Empty(B.Amount)) F.Queue.pop_front();
    }
    return Finish(V,std::move(Fingerprint));
}
bool ResourceLedger::Restore(ResourceLedgerState V)
{
    const auto& State=Snapshot();
    if (!ValidResourceLedger(V)) return false;
    if (State.Configured && (!V.Configured || V.Nodes!=State.Nodes || V.Owners.size()!=State.Owners.size())) return false;
    Storage=std::make_shared<ResourceLedgerState>(std::move(V)); return true;
}
Bytes EncodeResourceOutcome(const ResourceOutcome& V)
{
    Writer W(1024); WriteOutcome(W,V); return W.Take();
}
bool DecodeResourceOutcome(std::span<const std::uint8_t> Data,ResourceOutcome& Out)
{
    Reader R(Data); ResourceOutcome V;
    if (Data.size()>1024 || !ReadOutcome(R,V) || !R.Done()) return false;
    Out=V; return true;
}
Bytes EncodeResourceLedger(const ResourceLedgerState& S)
{
    if (!ValidResourceLedger(S)) return {};
    Writer W(64*1024*1024); W.U32(ResourceVersion); W.U8(S.Configured?1:0); W.U64(S.Version);
    W.U32(static_cast<std::uint32_t>(S.Nodes.size()));
    for (const auto& [Node,V]:S.Nodes)
    { W.U32(Node); W.U32(V.Cluster); W.U32(V.Territory); W.U8(static_cast<std::uint8_t>(V.Type)); W.U8(V.Initial); W.U8(S.Remaining.at(Node)); }
    W.U32(static_cast<std::uint32_t>(S.Owners.size())); for (const auto Owner:S.Owners) W.U8(static_cast<std::uint8_t>(Owner));
    W.U32(static_cast<std::uint32_t>(S.Miners.size()));
    for (const auto& [Key,V]:S.Miners)
    {
        W.Key(Key); W.U8(static_cast<std::uint8_t>(V.Faction)); W.U32(V.Capacity); WriteResources(W,V.Cargo);
        W.U64(V.ExtractionPeriod); W.U64(V.NextExtraction); W.U64(V.Version);
    }
    W.U32(static_cast<std::uint32_t>(S.Factories.size()));
    for (const auto& [Key,V]:S.Factories)
    {
        W.Key(Key); W.U8(static_cast<std::uint8_t>(V.Faction)); W.U32(V.Capacity); W.U64(V.ProcessingPeriod); W.U64(V.NextProcessing); W.U64(V.Version);
        W.U32(static_cast<std::uint32_t>(V.Queue.size()));
        for (const auto& B:V.Queue) { W.Identity(B.Event); W.Key(B.Source); W.U8(static_cast<std::uint8_t>(B.Beneficiary)); WriteResources(W,B.Amount); }
        WriteResources(W,V.Processed);
    }
    WriteResources(W,S.Processed); W.U32(static_cast<std::uint32_t>(S.Decisions.size()));
    for (const auto& [Event,D]:S.Decisions) { W.Identity(Event); W.Blob(D.Fingerprint); WriteOutcome(W,D.Outcome); }
    return W.Take();
}
Bytes EncodeResourceDirectory(const ResourceLedgerState& S)
{
    Writer W; W.U32(ResourceVersion); W.U8(S.Configured?1:0); W.U64(S.Version);
    W.U32(static_cast<std::uint32_t>(S.Remaining.size()));
    for (const auto& [Node,Remaining]:S.Remaining) { W.U32(Node); W.U8(Remaining); }
    W.U32(static_cast<std::uint32_t>(S.Owners.size())); for (const auto Owner:S.Owners) W.U8(static_cast<std::uint8_t>(Owner));
    return W.Take();
}
bool DecodeResourceDirectory(std::span<const std::uint8_t> Data,const ResourceLedgerState& Catalog,ResourceDirectory& Out)
{
    Reader R(Data); ResourceDirectory V; std::uint32_t Version,N; std::uint8_t Configured;
    if (Data.size()>MaxMessageBytes || !R.U32(Version) || Version!=ResourceVersion || !R.U8(Configured) || Configured>1
        || static_cast<bool>(Configured)!=Catalog.Configured || !R.U64(V.Version) || !Count(R,N,MaximumNodes)
        || N!=Catalog.Nodes.size()) return false;
    V.Configured=Configured!=0;
    for (std::uint32_t I=0;I<N;++I)
    {
        std::uint32_t Node; std::uint8_t Amount;
        if (!R.U32(Node) || !R.U8(Amount) || !Catalog.Nodes.contains(Node) || Amount>Catalog.Nodes.at(Node).Initial
            || !V.Remaining.emplace(Node,Amount).second) return false;
    }
    if (!Count(R,N,64) || N!=Catalog.Owners.size()) return false;
    for (std::uint32_t I=0;I<N;++I) { std::uint8_t Owner; if (!R.U8(Owner) || Owner>2) return false; V.Owners.push_back(static_cast<Team>(Owner)); }
    if (!R.Done() || (!V.Configured && V.Version)) return false;
    Out=std::move(V); return true;
}
bool DecodeResourceLedger(std::span<const std::uint8_t> Data,ResourceLedgerState& Out)
{
    if (Data.size()>64*1024*1024) return false;
    Reader R(Data); ResourceLedgerState S; std::uint32_t Version,N; std::uint8_t Configured;
    if (!R.U32(Version) || Version!=ResourceVersion || !R.U8(Configured) || Configured>1 || !R.U64(S.Version) || !Count(R,N,MaximumNodes)) return false;
    S.Configured=Configured!=0;
    for (std::uint32_t I=0;I<N;++I)
    {
        std::uint32_t Node; OreDefinition V; std::uint8_t Type,Remaining;
        if (!R.U32(Node) || !R.U32(V.Cluster) || !R.U32(V.Territory) || !R.U8(Type) || Type>1 || !R.U8(V.Initial) || !R.U8(Remaining)) return false;
        V.Type=static_cast<Commodity>(Type); if (!S.Nodes.emplace(Node,V).second || !S.Remaining.emplace(Node,Remaining).second) return false;
    }
    if (!Count(R,N,64)) return false;
    for (std::uint32_t I=0;I<N;++I) { std::uint8_t Team; if (!R.U8(Team) || Team>2) return false; S.Owners.push_back(static_cast<GuLiNet::Team>(Team)); }
    if (!Count(R,N,MaximumActors)) return false;
    for (std::uint32_t I=0;I<N;++I)
    {
        ObjectKey Key; MinerInventory V; std::uint8_t Team;
        if (!R.Key(Key) || !R.U8(Team) || Team>2 || !R.U32(V.Capacity) || !ReadResources(R,V.Cargo)
            || !R.U64(V.ExtractionPeriod) || !R.U64(V.NextExtraction) || !R.U64(V.Version)) return false;
        V.Faction=static_cast<GuLiNet::Team>(Team); if (!S.Miners.emplace(Key,V).second) return false;
    }
    if (!Count(R,N,MaximumActors)) return false;
    for (std::uint32_t I=0;I<N;++I)
    {
        ObjectKey Key; FactoryInventory V; std::uint8_t Team; std::uint32_t Batches;
        if (!R.Key(Key) || !R.U8(Team) || Team>2 || !R.U32(V.Capacity) || !R.U64(V.ProcessingPeriod)
            || !R.U64(V.NextProcessing) || !R.U64(V.Version) || !Count(R,Batches,MaximumQueue)) return false;
        V.Faction=static_cast<GuLiNet::Team>(Team);
        for (std::uint32_t B=0;B<Batches;++B)
        {
            RawDelivery Entry;
            if (!R.Identity(Entry.Event) || !R.Key(Entry.Source) || !R.U8(Team) || Team>2 || !ReadResources(R,Entry.Amount)) return false;
            Entry.Beneficiary=static_cast<GuLiNet::Team>(Team); V.Queue.push_back(Entry);
        }
        if (!ReadResources(R,V.Processed) || !S.Factories.emplace(Key,std::move(V)).second) return false;
    }
    if (!ReadResources(R,S.Processed) || !Count(R,N,MaximumDecisions)) return false;
    for (std::uint32_t I=0;I<N;++I)
    {
        Id Event; ResourceDecision V;
        if (!R.Identity(Event) || !R.Blob(V.Fingerprint,128) || !ReadOutcome(R,V.Outcome) || !S.Decisions.emplace(Event,std::move(V)).second) return false;
    }
    if (!R.Done() || !ValidResourceLedger(S)) return false;
    Out=std::move(S); return true;
}
}
