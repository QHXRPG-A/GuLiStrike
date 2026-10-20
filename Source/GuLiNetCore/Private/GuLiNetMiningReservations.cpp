#include "GuLiNetMiningReservations.h"
#include <utility>

namespace GuLiNet
{
namespace
{
constexpr std::uint32_t Schema=1,SnapshotSchema=2,MaximumNodes=100000,MaximumLeases=100000,MaximumDecisions=65536;
bool SourceKey(ObjectKey K) { return K.Valid(); }
void WriteLease(Writer& W,const MiningPositionLease& V)
{ W.Key(V.Source); W.U32(V.Cluster); W.U8(V.Sample); W.U32(V.Task); W.Identity(V.Token); W.U32(V.Node); W.U64(V.Version); }
bool ReadLease(Reader& R,MiningPositionLease& V)
{ return R.Key(V.Source) && R.U32(V.Cluster) && R.U8(V.Sample) && R.U32(V.Task) && R.Identity(V.Token) && R.U32(V.Node) && R.U64(V.Version); }
void WriteOutcome(Writer& W,const MiningReservationOutcome& V)
{ W.Identity(V.Event); W.U8(static_cast<std::uint8_t>(V.Operation)); W.U8(static_cast<std::uint8_t>(V.Status)); W.Key(V.Source); W.U64(V.Version); WriteLease(W,V.Lease); }
bool ReadOutcome(Reader& R,MiningReservationOutcome& V)
{
    std::uint8_t Operation=0,Status=0;
    if (!R.Identity(V.Event) || !R.U8(Operation) || Operation>2 || !R.U8(Status) || Status>6 || !R.Key(V.Source)
        || !R.U64(V.Version) || !ReadLease(R,V.Lease)) return false;
    V.Operation=static_cast<MiningReservationOperation>(Operation); V.Status=static_cast<MiningReservationStatus>(Status); return true;
}
bool ValidLease(const MiningPositionLease& V,const std::map<std::uint32_t,std::uint32_t>& Nodes,const std::set<std::uint32_t>& Clusters,Revision Version)
{
    return SourceKey(V.Source) && Clusters.contains(V.Cluster) && V.Sample<MiningSamplesPerCluster && V.Task && V.Token.Valid()
        && V.Version && V.Version<=Version && (!V.Node || (Nodes.contains(V.Node) && Nodes.at(V.Node)==V.Cluster));
}
bool Count(Reader& R,std::uint32_t& V,std::uint32_t Limit) { return R.U32(V) && V<=Limit; }
}
bool ValidMiningReservations(const MiningReservationState& S)
{
    if (!S.Configured) return !S.Version && S.NodeClusters.empty() && S.Leases.empty() && S.Decisions.empty() && S.SnapshotLeases.empty();
    if (S.NodeClusters.empty() || S.NodeClusters.size()>MaximumNodes || S.Leases.size()>MaximumLeases || S.Decisions.size()>MaximumDecisions
        || S.SnapshotLeases.size()>S.Leases.size()) return false;
    std::set<std::uint32_t> Clusters,Nodes;
    for (const auto& [Node,Cluster]:S.NodeClusters) { if (!Node || !Cluster) return false; Clusters.insert(Cluster); }
    std::set<std::pair<std::uint32_t,std::uint8_t>> Positions;
    std::set<Id> Tokens;
    std::map<Id,MiningPositionLease> Recorded;
    std::map<ObjectKey,std::pair<Revision,Id>> LatestAdmission;
    for (const auto& [Source,B]:S.SnapshotLeases)
    {
        const auto L=S.Leases.find(Source);
        if (Source!=B.Source || !ValidLease(B,S.NodeClusters,Clusters,S.Version) || L==S.Leases.end()
            || B.Token!=L->second.Token || B.Cluster!=L->second.Cluster || B.Sample!=L->second.Sample || B.Task!=L->second.Task
            || B.Version>L->second.Version || !Recorded.emplace(B.Token,B).second) return false;
        LatestAdmission.emplace(Source,std::pair{B.Version,B.Token});
    }
    for (const auto& [Source,V]:S.Leases)
    {
        const auto Origin=S.Decisions.find(V.Token);
        const bool HistoricalOrigin=Origin!=S.Decisions.end() && Origin->second.Outcome.Operation==MiningReservationOperation::ClaimPosition
            && Origin->second.Outcome.Status==MiningReservationStatus::Committed && Origin->second.Outcome.Source==Source
            && Origin->second.Outcome.Lease.Token==V.Token && Origin->second.Outcome.Lease.Cluster==V.Cluster
            && Origin->second.Outcome.Lease.Sample==V.Sample && Origin->second.Outcome.Lease.Task==V.Task;
        if (Source!=V.Source || !ValidLease(V,S.NodeClusters,Clusters,S.Version) || !Positions.emplace(V.Cluster,V.Sample).second
            || !Tokens.insert(V.Token).second || (V.Node && !Nodes.insert(V.Node).second)
            || (!HistoricalOrigin && !S.SnapshotLeases.contains(Source))) return false;
    }
    std::set<Id> Released;
    for (const auto& [Event,D]:S.Decisions)
    {
        const auto& V=D.Outcome;
        Reader R(D.Fingerprint); ObjectKey Source; Id Token; std::uint8_t Op=0,Sample=0; std::uint32_t Cluster=0,Task=0,Node=0;
        if (!Event.Valid() || Event!=V.Event || !SourceKey(V.Source) || !V.Version || V.Version>S.Version
            || V.Operation>MiningReservationOperation::Release || V.Status>MiningReservationStatus::MissingLease
            || V.Status==MiningReservationStatus::Duplicate || V.Status==MiningReservationStatus::Conflict
            || D.Fingerprint.size()>160 || !R.U8(Op) || Op>2 || !R.Key(Source) || !R.U32(Cluster) || !R.U8(Sample)
            || !R.U32(Task) || !R.Identity(Token) || !R.U32(Node) || !R.Done() || Source!=V.Source || Op!=static_cast<std::uint8_t>(V.Operation)) return false;
        if (V.Lease.Source.Valid() && (V.Lease.Source!=Source || !ValidLease(V.Lease,S.NodeClusters,Clusters,V.Version))) return false;
        if (!V.Lease.Source.Valid() && V.Lease!=MiningPositionLease{}) return false;
        if (V.IsCommitted() && (!V.Lease.Source.Valid()
            || (V.Operation==MiningReservationOperation::ClaimPosition && (V.Lease.Cluster!=Cluster || V.Lease.Sample!=Sample || V.Lease.Task!=Task || Token.Valid() || Node))
            || (V.Operation==MiningReservationOperation::ClaimNode && (V.Lease.Token!=Token || V.Lease.Node!=Node || Cluster || Sample || Task))
            || (V.Operation==MiningReservationOperation::Release && (V.Lease.Token!=Token || Cluster || Sample || Task || Node)))) return false;
        if (V.IsCommitted())
        {
            if (V.Operation==MiningReservationOperation::Release) Released.insert(V.Lease.Token);
            else
            {
                const auto Previous=Recorded.find(V.Lease.Token);
                if (Previous==Recorded.end() || Previous->second.Version<V.Lease.Version) Recorded[V.Lease.Token]=V.Lease;
                else if (Previous->second.Version==V.Lease.Version && Previous->second!=V.Lease) return false;
                if (V.Operation==MiningReservationOperation::ClaimPosition)
                {
                    auto& Latest=LatestAdmission[V.Source];
                    if (Latest.first<V.Version) Latest={V.Version,V.Lease.Token};
                }
            }
        }
    }
    for (const auto& [Source,V]:S.Leases)
        if (!Recorded.contains(V.Token) || Recorded.at(V.Token)!=V || Released.contains(V.Token)
            || !LatestAdmission.contains(Source) || LatestAdmission.at(Source).second!=V.Token) return false;
    return true;
}
void MiningReservations::RebuildIndexes()
{
    auto& Data=Write(); auto& State=Data.State; auto& PositionOwners=Data.PositionOwners; auto& NodeOwners=Data.NodeOwners; auto& Clusters=Data.Clusters;
    PositionOwners.clear(); NodeOwners.clear(); Clusters.clear();Data.ClusterLeases.clear();Data.ClusterDecisions.clear();
    for (const auto& [Node,Cluster]:State.NodeClusters) { (void)Node; Clusters.insert(Cluster); }
    for (const auto& [Source,V]:State.Leases)
    { PositionOwners.emplace(std::make_pair(V.Cluster,V.Sample),Source); if (V.Node) NodeOwners.emplace(V.Node,Source);Data.ClusterLeases[V.Cluster].insert(Source); }
    for (const auto& [Event,D]:State.Decisions) Data.ClusterDecisions[D.Outcome.Lease.Cluster].insert(Event);
}
const std::set<ObjectKey>& MiningReservations::LeasesForCluster(std::uint32_t Cluster) const
{static const std::set<ObjectKey> Empty;const auto I=Value->ClusterLeases.find(Cluster);return I==Value->ClusterLeases.end()?Empty:I->second;}
const std::set<Id>& MiningReservations::DecisionsForCluster(std::uint32_t Cluster) const
{static const std::set<Id> Empty;const auto I=Value->ClusterDecisions.find(Cluster);return I==Value->ClusterDecisions.end()?Empty:I->second;}
bool MiningReservations::Configure(std::map<std::uint32_t,std::uint32_t> Nodes)
{
    auto& Data=Write(); auto& State=Data.State;
    if (State.Configured) return false;
    MiningReservationState V; V.Configured=true; V.NodeClusters=std::move(Nodes);
    if (!ValidMiningReservations(V)) return false; State=std::move(V); RebuildIndexes(); return true;
}
bool MiningReservations::Restore(MiningReservationState V)
{
    auto& Data=Write(); auto& State=Data.State;
    if (!ValidMiningReservations(V) || (State.Configured && (!V.Configured || V.NodeClusters!=State.NodeClusters))) return false;
    State=std::move(V); RebuildIndexes(); return true;
}
MiningReservationOutcome MiningReservations::Begin(MiningReservationOperation Op,ObjectKey Source,Id Event,
    std::uint32_t Cluster,std::uint8_t Sample,std::uint32_t Task,Id Token,std::uint32_t Node,Bytes& Fingerprint) const
{
    const auto& Data=*Value; const auto& State=Data.State;
    MiningReservationOutcome V; V.Event=Event; V.Source=Source; V.Operation=Op;
    if (!State.Configured || !SourceKey(Source) || !Event.Valid()) return V;
    Writer W(160); W.U8(static_cast<std::uint8_t>(Op)); W.Key(Source); W.U32(Cluster); W.U8(Sample); W.U32(Task); W.Identity(Token); W.U32(Node); Fingerprint=W.Take();
    if (const auto I=State.Decisions.find(Event); I!=State.Decisions.end())
    {
        if (I->second.Fingerprint!=Fingerprint) { V.Status=MiningReservationStatus::Conflict; return V; }
        V=I->second.Outcome; if (V.Status==MiningReservationStatus::Committed) V.Status=MiningReservationStatus::Duplicate; return V;
    }
    V.Status=MiningReservationStatus::Deferred; return V;
}
MiningReservationOutcome MiningReservations::Finish(MiningReservationOutcome V,Bytes Fingerprint)
{
    auto& Data=Write(); auto& State=Data.State;
    V.Version=++State.Version; State.Decisions.emplace(V.Event,MiningReservationDecision{std::move(Fingerprint),V});Data.ClusterDecisions[V.Lease.Cluster].insert(V.Event);return V;
}
void MiningReservations::RemoveIndexes(const MiningPositionLease& V)
{
    auto& Data=Write(); auto& PositionOwners=Data.PositionOwners; auto& NodeOwners=Data.NodeOwners; PositionOwners.erase(std::make_pair(V.Cluster,V.Sample)); if (V.Node) NodeOwners.erase(V.Node);
    if (auto I=Data.ClusterLeases.find(V.Cluster);I!=Data.ClusterLeases.end()) {I->second.erase(V.Source);if (I->second.empty()) Data.ClusterLeases.erase(I);} }
MiningReservationOutcome MiningReservations::ClaimPosition(ObjectKey Source,std::uint32_t Cluster,std::uint8_t Sample,std::uint32_t Task,Id Event)
{
    auto& Data=Write(); auto& State=Data.State; auto& PositionOwners=Data.PositionOwners; auto& Clusters=Data.Clusters;
    Bytes Fingerprint; auto V=Begin(MiningReservationOperation::ClaimPosition,Source,Event,Cluster,Sample,Task,{},0,Fingerprint);
    if (V.Status!=MiningReservationStatus::Deferred || State.Decisions.size()>=MaximumDecisions || State.Version==UINT64_MAX) return V;
    const auto Existing=State.Leases.find(Source); if (Existing!=State.Leases.end()) V.Lease=Existing->second;
    auto Reject=[&](MiningReservationStatus Status) { V.Status=Status; return Finish(V,std::move(Fingerprint)); };
    if (!Clusters.contains(Cluster) || Sample>=MiningSamplesPerCluster || !Task) return Reject(MiningReservationStatus::Invalid);
    const auto Owner=PositionOwners.find({Cluster,Sample});
    if (Owner!=PositionOwners.end() && Owner->second!=Source) return Reject(MiningReservationStatus::Occupied);
    if (Existing==State.Leases.end() && State.Leases.size()>=MaximumLeases) return V;
    if (Existing!=State.Leases.end() && Existing->second.Cluster==Cluster && Existing->second.Sample==Sample && Existing->second.Task==Task)
    { V.Status=MiningReservationStatus::Committed; return Finish(V,std::move(Fingerprint)); }
    if (Existing!=State.Leases.end()) RemoveIndexes(Existing->second);
    State.SnapshotLeases.erase(Source);
    V.Lease={Source,Cluster,Task,Sample,Event,0,State.Version+1}; State.Leases[Source]=V.Lease;
    PositionOwners[{Cluster,Sample}]=Source;Data.ClusterLeases[Cluster].insert(Source);V.Status=MiningReservationStatus::Committed; return Finish(V,std::move(Fingerprint));
}
MiningReservationOutcome MiningReservations::ClaimNode(ObjectKey Source,Id Token,std::uint32_t Node,Id Event)
{
    auto& Data=Write(); auto& State=Data.State; auto& NodeOwners=Data.NodeOwners;
    Bytes Fingerprint; auto V=Begin(MiningReservationOperation::ClaimNode,Source,Event,0,0,0,Token,Node,Fingerprint);
    if (V.Status!=MiningReservationStatus::Deferred || State.Decisions.size()>=MaximumDecisions || State.Version==UINT64_MAX) return V;
    const auto I=State.Leases.find(Source); if (I!=State.Leases.end()) V.Lease=I->second;
    auto Reject=[&](MiningReservationStatus Status) { V.Status=Status; return Finish(V,std::move(Fingerprint)); };
    if (!Token.Valid() || !Node || !State.NodeClusters.contains(Node)) return Reject(MiningReservationStatus::Invalid);
    if (I==State.Leases.end() || I->second.Token!=Token) return Reject(MiningReservationStatus::MissingLease);
    if (State.NodeClusters.at(Node)!=I->second.Cluster) return Reject(MiningReservationStatus::Invalid);
    if (const auto Other=NodeOwners.find(Node); Other!=NodeOwners.end() && Other->second!=Source) return Reject(MiningReservationStatus::Occupied);
    auto& Lease=I->second;
    if (Lease.Node) NodeOwners.erase(Lease.Node);
    Lease.Node=Node; Lease.Version=State.Version+1; NodeOwners[Node]=Source; V.Lease=Lease;
    V.Status=MiningReservationStatus::Committed; return Finish(V,std::move(Fingerprint));
}
MiningReservationOutcome MiningReservations::Release(ObjectKey Source,Id Token,Id Event)
{
    auto& Data=Write(); auto& State=Data.State;
    Bytes Fingerprint; auto V=Begin(MiningReservationOperation::Release,Source,Event,0,0,0,Token,0,Fingerprint);
    if (V.Status!=MiningReservationStatus::Deferred || State.Decisions.size()>=MaximumDecisions || State.Version==UINT64_MAX) return V;
    const auto I=State.Leases.find(Source); if (I!=State.Leases.end()) V.Lease=I->second;
    if (!Token.Valid()) V.Status=MiningReservationStatus::Invalid;
    else if (I==State.Leases.end() || I->second.Token!=Token) V.Status=MiningReservationStatus::MissingLease;
    else { RemoveIndexes(I->second); State.Leases.erase(I); State.SnapshotLeases.erase(Source); V.Status=MiningReservationStatus::Committed; }
    return Finish(V,std::move(Fingerprint));
}
bool MiningReservations::AbandonSource(ObjectKey Source)
{
    auto& Data=Write(); auto& State=Data.State;
    if (!State.Configured || !SourceKey(Source)) return false;
    const auto I=State.Leases.find(Source); if (I==State.Leases.end()) return true;
    if (State.Version==UINT64_MAX) return false;
    RemoveIndexes(I->second); State.Leases.erase(I); State.SnapshotLeases.erase(Source); ++State.Version; return true;
}
bool MiningReservations::OwnsNode(ObjectKey Source,Id Token,std::uint32_t Node) const
{
    const auto& Data=*Value; const auto& State=Data.State; const auto& NodeOwners=Data.NodeOwners;
    const auto I=State.Leases.find(Source);
    return Node && I!=State.Leases.end() && I->second.Token==Token && I->second.Node==Node
        && NodeOwners.contains(Node) && NodeOwners.at(Node)==Source;
}
Bytes EncodeMiningReservationOutcome(const MiningReservationOutcome& V)
{ Writer W; W.U32(Schema); WriteOutcome(W,V); return W.Take(); }
bool DecodeMiningReservationOutcome(std::span<const std::uint8_t> Data,MiningReservationOutcome& Out)
{
    Reader R(Data); std::uint32_t Version=0; MiningReservationOutcome V;
    if (Data.size()>512 || !R.U32(Version) || Version!=Schema || !ReadOutcome(R,V) || !R.Done()
        || !V.Event.Valid() || !SourceKey(V.Source)) return false;
    if (V.Lease.Source.Valid())
    {
        const auto& L=V.Lease;
        if (L.Source!=V.Source || !L.Cluster || L.Sample>=MiningSamplesPerCluster || !L.Task || !L.Token.Valid()
            || !L.Version || L.Version>V.Version) return false;
    }
    else if (V.IsCommitted() || V.Lease!=MiningPositionLease{}) return false;
    Out=std::move(V); return true;
}
Bytes EncodeMiningReservations(const MiningReservationState& S)
{
    Writer W(32*1024*1024); W.U32(S.SnapshotLeases.empty()?Schema:SnapshotSchema); W.U8(S.Configured?1:0); W.U64(S.Version);
    W.U32(static_cast<std::uint32_t>(S.NodeClusters.size())); for (const auto& [Node,Cluster]:S.NodeClusters) { W.U32(Node); W.U32(Cluster); }
    W.U32(static_cast<std::uint32_t>(S.Leases.size())); for (const auto& [Source,V]:S.Leases) { W.Key(Source); WriteLease(W,V); }
    W.U32(static_cast<std::uint32_t>(S.Decisions.size())); for (const auto& [Event,D]:S.Decisions) { W.Identity(Event); W.Blob(D.Fingerprint); WriteOutcome(W,D.Outcome); }
    if (!S.SnapshotLeases.empty())
    {W.U32(static_cast<std::uint32_t>(S.SnapshotLeases.size()));for (const auto& [Source,L]:S.SnapshotLeases) WriteLease(W,L);}
    return W.Take();
}
bool DecodeMiningReservations(std::span<const std::uint8_t> Data,MiningReservationState& Out)
{
    if (Data.size()>32*1024*1024) return false;
    Reader R(Data); MiningReservationState S; std::uint32_t Version=0,N=0; std::uint8_t Configured=0;
    if (!R.U32(Version) || (Version!=Schema&&Version!=SnapshotSchema) || !R.U8(Configured) || Configured>1 || !R.U64(S.Version) || !Count(R,N,MaximumNodes)) return false;
    S.Configured=Configured!=0;
    for (std::uint32_t I=0;I<N;++I)
    { std::uint32_t Node=0,Cluster=0; if (!R.U32(Node) || !R.U32(Cluster) || !S.NodeClusters.emplace(Node,Cluster).second) return false; }
    if (!Count(R,N,MaximumLeases)) return false;
    for (std::uint32_t I=0;I<N;++I)
    { ObjectKey Source; MiningPositionLease V; if (!R.Key(Source) || !ReadLease(R,V) || !S.Leases.emplace(Source,V).second) return false; }
    if (!Count(R,N,MaximumDecisions)) return false;
    for (std::uint32_t I=0;I<N;++I)
    { Id Event; MiningReservationDecision V; if (!R.Identity(Event) || !R.Blob(V.Fingerprint,160) || !ReadOutcome(R,V.Outcome) || !S.Decisions.emplace(Event,std::move(V)).second) return false; }
    if (Version==SnapshotSchema)
    {
        if (!Count(R,N,MaximumLeases)) return false;
        for (std::uint32_t I=0;I<N;++I) {MiningPositionLease L;if (!ReadLease(R,L)||!S.SnapshotLeases.emplace(L.Source,L).second) return false;}
    }
    if (!R.Done() || !ValidMiningReservations(S)) return false; Out=std::move(S); return true;
}
Bytes EncodeMiningReservationDirectory(const MiningReservationState& S)
{
    Writer W; W.U32(Schema); W.U8(S.Configured?1:0); W.U64(S.Version);
    W.U32(static_cast<std::uint32_t>(S.Leases.size())); for (const auto& [Source,V]:S.Leases) { W.Key(Source); WriteLease(W,V); }
    return W.Take();
}
bool DecodeMiningReservationDirectory(std::span<const std::uint8_t> Data,
    const std::map<std::uint32_t,std::uint32_t>& Catalog,MiningReservationDirectory& Out)
{
    if (Data.size()>MaxMessageBytes) return false;
    Reader R(Data); MiningReservationDirectory S; std::uint32_t Version=0,N=0; std::uint8_t Configured=0;
    if (!R.U32(Version) || Version!=Schema || !R.U8(Configured) || Configured>1 || !R.U64(S.Version)
        || !Count(R,N,MaximumLeases) || (Configured!=0)!=!Catalog.empty()) return false;
    S.Configured=Configured!=0; if (!S.Configured && (S.Version || N)) return false;
    std::set<std::uint32_t> Clusters,Nodes;
    for (const auto& [Node,Cluster]:Catalog) { (void)Node; Clusters.insert(Cluster); }
    std::set<std::pair<std::uint32_t,std::uint8_t>> Positions; std::set<Id> Tokens;
    for (std::uint32_t I=0;I<N;++I)
    {
        ObjectKey Source; MiningPositionLease V;
        if (!R.Key(Source) || !ReadLease(R,V) || Source!=V.Source || !ValidLease(V,Catalog,Clusters,S.Version)
            || !Positions.emplace(V.Cluster,V.Sample).second || !Tokens.insert(V.Token).second || (V.Node && !Nodes.insert(V.Node).second)
            || !S.Leases.emplace(Source,V).second) return false;
    }
    if (!R.Done()) return false; Out=std::move(S); return true;
}
}
