#include "Gameplay/Navigation/Grid/GuLiGridNetwork.h"
#include "Gameplay/Navigation/Grid/GuLiGroundGridSubsystem.h"
#include "Gameplay/Navigation/Grid/GuLiGridMotion.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Building/GuLiBuildingProductionComponent.h"
#include "Gameplay/Stronghold/GuLiStrongholdFacilitiesComponent.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

const FName FGuLiGridNetwork::System(TEXT("GroundGridTraffic"));
namespace
{
constexpr int32 MaxBodies=32768,MaxAdmissions=65536,MaxPackets=256,RowsPerPacket=64;
constexpr uint32 PacketLimit=32*1024,CheckpointLimit=16*1024*1024;
bool Same(const GuLiGrid::FRect& A,const GuLiGrid::FRect& B) {return A.Min==B.Min && A.Max==B.Max;}
bool Displacement(UGuLiHybridWorldSubsystem& H,FGuLiHybridObjectState& O)
{
    FGuLiHybridCapabilityState State;
    if (!H.GetObjectCapability(O.Key,TEXT("relocation_body"),State)) return false;
    O.Capabilities.Add(TEXT("relocation_body"),MoveTemp(State));return true;
}
void SerializeGridRect(FGuLiHybridStateArchive& A,GuLiGrid::FRect& R)
{
    A.Value(R.Min.X);A.Value(R.Min.Y);A.Value(R.Max.X);A.Value(R.Max.Y);
    if (R.Min.X<-65536 || R.Min.Y<-65536 || R.Max.X>65536 || R.Max.Y>65536
        || R.Max.X<R.Min.X || R.Max.Y<R.Min.Y || int64(R.Max.X)-R.Min.X>128 || int64(R.Max.Y)-R.Min.Y>128) A.Reject();
}
}
FGuLiGridNetwork::FGuLiGridNetwork(UGuLiGroundGridSubsystem& G):Grid(&G)
{
    const auto* Map=LoadObject<UGuLiResourceMapDefinition>(nullptr,TEXT("/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap.DA_CommanderResourceMap"));
    if (Map && !Map->Territories.IsEmpty()) Anchor=FGuLiHybridManifest::MakeStableKey(Map->MapPackage.ToString(),
        EGuLiHybridObjectKind::Territory,Map->Territories[0].TerritoryId.ToString());
}
UObject* FGuLiGridNetwork::LifetimeOwner() const {return Grid.Get();}
UGuLiHybridWorldSubsystem* FGuLiGridNetwork::Hybrid() const
{return Grid.IsValid()?Grid->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;}
UGuLiHybridSessionSubsystem* FGuLiGridNetwork::Session() const
{auto* W=Grid.IsValid()?Grid->GetWorld():nullptr;return W && W->GetGameInstance()?W->GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>():nullptr;}
bool FGuLiGridNetwork::IsLocal(const FGuid& Owner) const {const auto* S=Session();return S && S->GetLocalPlayerId()==Owner;}
void FGuLiGridNetwork::SerializeRow(FGuLiHybridStateArchive& A,FRow& R)
{
    A.Value(R.Key);A.Value(R.Other);A.Value(R.Owner);A.Value(R.Event);A.Value(R.OwnerEpoch);A.Value(R.Nonce);
    A.Value(R.Sequence);A.Value(R.ControlEpoch);A.Value(R.Size);A.Value(R.Team);
    A.Enum(R.Priority.Class,uint8(GuLiGrid::ETrafficClass::DirectPlayer));A.Value(R.Priority.AcceptedSequence);A.Value(R.Priority.Command);
    SerializeGridRect(A,R.Actual);SerializeGridRect(A,R.Scope);SerializeGridRect(A,R.Passage);A.Value(R.bGranted);A.Value(R.bPlacement);A.Value(R.bPhysicalBlocked);
    if (R.Size<1 || R.Size>64 || R.Team>2) A.Reject();
}
bool FGuLiGridNetwork::Decode(TConstArrayView<uint8> Bytes,FPacket& P)
{
    FGuLiHybridStateArchive A(Bytes,PacketLimit);uint32 Schema=2;A.Value(Schema);
    A.Enum(P.Type,uint8(EPacket::Withdraw));A.Value(P.Epoch);A.Value(P.Roster);A.Array(P.Rows,RowsPerPacket,SerializeRow);
    return Schema==2 && P.Epoch && A.Done();
}
bool FGuLiGridNetwork::ReceiveEvent(const FGuid& Peer,const FGuid& Event,TConstArrayView<uint8> Bytes)
{
    if (Bytes.Num()>int32(PacketLimit)) return true;
    if (Incoming.Num()-InCursor>=MaxPackets || QueuedBytes+Bytes.Num()>4*1024*1024) return false;
    FPacket P;if (!Decode(Bytes,P)) return true;P.Peer=Peer;P.Event=Event;
    QueuedBytes+=P.Rows.GetAllocatedSize()+sizeof(FPacket);Incoming.Add(MoveTemp(P));return true;
}
void FGuLiGridNetwork::Queue(EPacket Type,const FGuid& Peer,const FRow* Row)
{
    // Coalesce within the unsent tail, preserving per-peer reliable order.
    FPacket* Packet=nullptr;
    if (!Outgoing.IsEmpty())
    {auto& Tail=Outgoing.Last();if (Tail.Type==Type && Tail.Peer==Peer && Tail.Epoch==AuthorityEpoch && Tail.Rows.Num()<RowsPerPacket) Packet=&Tail;}
    if (!Packet)
    {
        if (Outgoing.Num()>=MaxPackets)
        {if (auto* H=Hybrid()) H->Suspend(TEXT("Grid traffic outbound capacity exceeded"));return;}
        Packet=&Outgoing.AddDefaulted_GetRef();Packet->Type=Type;Packet->Peer=Peer;Packet->Epoch=AuthorityEpoch;Packet->Roster=Roster;Packet->Event=FGuid::NewGuid();
    }
    if (Row) Packet->Rows.Add(*Row);
}
void FGuLiGridNetwork::Broadcast(EPacket Type,const FRow* Row)
{
    if (auto* S=Session())
    {
        for (const auto& M:S->GetMembers()) if (M.bOnline && M.bReady && M.Player!=S->GetLocalPlayerId()) Queue(Type,M.Player,Row);
        // A coordinator may route its command immediately after its own receipt. Remote receipts
        // must precede that command on the same reliable channel to avoid head-of-line blocking.
        Queue(Type,S->GetLocalPlayerId(),Row);
    }
}
bool FGuLiGridNetwork::RefreshAuthority()
{
    auto* H=Hybrid();FGuLiHybridObjectState O;
    if (!H || !H->CanPublishFacts() || !Anchor.IsValid() || !H->GetObjectView(Anchor,O) || !O.SimulationOwner.IsValid()) return false;
    const uint64 Members=CurrentRoster();
    if (O.SimulationOwner!=Authority || O.OwnerEpoch!=AuthorityEpoch || Members!=Roster)
    {
        Authority=O.SimulationOwner;AuthorityEpoch=O.OwnerEpoch;Roster=Members;bReady=false;bSyncDone=false;
        ++SyncGeneration;ReconcileCursor=0;
        SyncCursor=SyncAdmissionCursor=0;Synced.Reset();Admissions.GenerateKeyArray(SyncAdmissionKeys);
        Outgoing.Reset();Queries.Reset();
        // Existing reservations stay conservative until every online peer has frozen and resubmitted.
        DirtyKeys.Reset();DirtyCursor=0;
        for (auto& Pair:Locals) {Pair.Value.bPending=false;Pair.Value.bDirty=false;Pair.Value.bQueued=false;}
    }
    return AuthorityEpoch!=0;
}
uint64 FGuLiGridNetwork::CurrentRoster() const
{
    const auto* S=Session();if (!S) return 0;
    uint64 Value=S->GetMatchEpoch();
    // Order independent: member-array order is not a distributed identity.
    for (const auto& M:S->GetMembers()) if (M.bOnline && M.bReady)
    {
        uint64 V=(uint64(M.Player.A)<<32)|M.Player.B;
        V^=(uint64(M.Player.C)<<32)|M.Player.D;V+=0x9e3779b97f4a7c15ull;
        V=(V^(V>>30))*0xbf58476d1ce4e5b9ull;V=(V^(V>>27))*0x94d049bb133111ebull;
        Value^=V^(V>>31);
    }
    return Value;
}
void FGuLiGridNetwork::Dirty(FLocal& L)
{
    L.bDirty=true;if (L.bQueued) return;
    if (DirtyKeys.Num()-DirtyCursor>=MaxBodies) {if (auto* H=Hybrid()) H->Suspend(TEXT("Grid dirty queue capacity exceeded"));return;}
    L.bQueued=true;DirtyKeys.Add(L.Row.Key);
}
void FGuLiGridNetwork::RemoveLocal(const FGuLiHybridObjectKey& Key)
{
    const auto* L=Locals.Find(Key);if (!L) return;const int32 I=L->StorageIndex;
    LocalKeys.RemoveAtSwap(I,1,EAllowShrinking::No);Locals.Remove(Key);
    if (LocalKeys.IsValidIndex(I)) Locals.FindChecked(LocalKeys[I]).StorageIndex=I;
    SyncCursor=FMath::Min(SyncCursor,I);
}
void FGuLiGridNetwork::RemoveBody(const FGuLiHybridObjectKey& Key)
{
    const auto* B=Bodies.Find(Key);if (!B) return;const int32 I=B->StorageIndex;
    AuthorityIndex.Remove(Key,B->Owner,B->OwnerEpoch);Bodies.Remove(Key);
    AuthorityKeys.RemoveAtSwap(I,1,EAllowShrinking::No);
    if (AuthorityKeys.IsValidIndex(I)) Bodies.FindChecked(AuthorityKeys[I]).StorageIndex=I;
}
bool FGuLiGridNetwork::CurrentBody(const FGuid& Peer,const FRow& R,FGuLiHybridObjectState& O) const
{
    auto* H=Hybrid();const bool Valid=R.Key.IsValid() && R.Owner==Peer && R.OwnerEpoch && R.Scope.Contains(R.Actual)
        && R.Actual.Max.X>R.Actual.Min.X && R.Actual.Max.Y>R.Actual.Min.Y
        && R.Actual.Max.X-R.Actual.Min.X<=R.Size+1 && R.Actual.Max.Y-R.Actual.Min.Y<=R.Size+1
        && H && H->GetObjectView(R.bPlacement?R.Other:R.Key,O) && O.SimulationOwner==Peer && O.OwnerEpoch==R.OwnerEpoch
        && O.LifeState==1 && (R.bPlacement || !O.bPhased && !DisplacementHolds.Contains(R.Key)) && uint8(O.Team)==R.Team;
    if (!Valid) return false;
    if (R.bPlacement)
    {
        FGuLiHybridObjectState Target;
        // A future birth or a phased landing cannot overwrite a living ground body.
        if (!H->GetObjectView(R.Key,Target) || Target.LifeState==0 && Target.Parent==R.Other) return true;
        if (R.Key!=R.Other) return false;
        if (Target.bPhased) return true;
        return Displacement(*H,Target) && GuLiGrid::PendingDisplacementAt(Target,*Grid->Snapshot(),R.Scope,R.Size);
    }
    if (R.Key.Kind==EGuLiHybridObjectKind::GroundPlayer) return R.Size==1;
    const FString Definition=O.Definition.ToString();int32 Slash=INDEX_NONE;Definition.FindLastChar('/',Slash);
    const auto* Catalog=Grid->GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
    const auto* D=Catalog && Slash>=0?Catalog->FindSoldierDefinition(FCString::Atoi(*Definition.Mid(Slash+1))):nullptr;
    return D && R.Size==D->FootprintCells;
}
void FGuLiGridNetwork::PublishBody(const FRow& R)
{
    auto* Old=Bodies.Find(R.Key);
    if (!Old && Bodies.Num()>=MaxBodies) {Hybrid()->Suspend(TEXT("Grid authority body capacity exceeded"));return;}
    const int32 Index=Old?Old->StorageIndex:AuthorityKeys.Add(R.Key);
    if (Old && Old->bPlacement!=R.bPlacement) AuthorityIndex.Remove(R.Key,Old->Owner,Old->OwnerEpoch);
    auto& Stored=Bodies.Add(R.Key,R);Stored.StorageIndex=Index;Stored.SyncGeneration=SyncGeneration;
    AuthorityIndex.Admit(R.Key,R.Owner,R.OwnerEpoch,R.Team,R.Actual,R.Priority,R.Size);
    AuthorityIndex.SetAuthorityScope(R.Key,R.Owner,R.OwnerEpoch,R.Scope);
}
void FGuLiGridNetwork::Observe(const FGuLiGridTrafficRecord& R)
{
    PublishedPose(R.Key,R.Owner,R.OwnerEpoch,R.Occupied);
    if (!IsLocal(R.Owner)) return;
    if (DisplacementHolds.Contains(R.Key)) return;
    auto* L=Locals.Find(R.Key);
    if (!L)
    {if (Locals.Num()>=MaxBodies) return;L=&Locals.Add(R.Key);L->StorageIndex=LocalKeys.Add(R.Key);}
    if (L->Row.OwnerEpoch!=R.OwnerEpoch || L->Row.Owner!=R.Owner || L->Row.bPlacement)
    { const int32 Index=L->StorageIndex;const bool Queued=L->bQueued;*L=FLocal{};L->StorageIndex=Index;L->bQueued=Queued;
      L->Row.Key=R.Key;L->Row.Nonce=1;L->Row.Owner=R.Owner;L->Row.OwnerEpoch=R.OwnerEpoch;L->Desired=R.Occupied; }
    L->Row.Actual=R.Occupied;L->Row.Size=R.Footprint;L->Row.Team=R.Team;L->Row.Priority=R.Priority;
    // Ordinary bodies only report actual occupancy; placement is the sole exclusion protocol.
    L->Row.Scope=R.Occupied;L->bGrant=L->bPending=L->bDirty=false;
}
void FGuLiGridNetwork::PublishedPose(const FGuLiHybridObjectKey& Key,const FGuid& Owner,uint64 Epoch,const GuLiGrid::FRect& Actual)
{
    if (auto* L=Locals.Find(Key);L && !L->Row.bPlacement && L->Row.Owner==Owner && L->Row.OwnerEpoch==Epoch)
        L->Row.Actual=L->Row.Scope=Actual;
    if (!IsLocal(Authority) || !Grid.IsValid() || DisplacementHolds.Contains(Key)) return;
    const auto* Body=Grid->Traffic().Find(Key);
    if (!Body || Body->Owner!=Owner || Body->OwnerEpoch!=Epoch) return;
    auto* B=Bodies.Find(Key);
    if (!B || !B->bPlacement && (B->Owner!=Owner || B->OwnerEpoch!=Epoch))
    {
        if (B && B->OwnerEpoch>Epoch) return;
        FRow Row;Row.Key=Key;Row.Owner=Owner;Row.OwnerEpoch=Epoch;Row.Size=Body->Footprint;Row.Team=Body->Team;
        Row.Actual=Row.Scope=Actual;PublishBody(Row);return;
    }
    if (B->bPlacement || B->Owner!=Owner || B->OwnerEpoch!=Epoch) return;
    B->SyncGeneration=SyncGeneration;
    if (!Same(B->Actual,Actual) && AuthorityIndex.Commit(Key,Owner,Epoch,Actual)) B->Actual=B->Scope=Actual;
}
EGuLiGridPermit FGuLiGridNetwork::PermitPlacement(const FGuLiHybridObjectKey& Source,const FGuLiHybridObjectKey& Target,
    const GuLiGrid::FRect& Rect,int32 Size,uint8 Team)
{
    FGuLiHybridObjectState O;auto* H=Hybrid();
    if (!H || !H->GetObjectView(Source,O) || !IsLocal(O.SimulationOwner) || !Target.IsValid() || !Grid.IsValid()) return EGuLiGridPermit::Pending;
    auto* L=Locals.Find(Target);
    if (L && !L->Row.bPlacement && Source==Target)
    {
        if (O.bPhased || Displacement(*H,O) && GuLiGrid::PendingDisplacementAt(O,*Grid->Snapshot(),Rect,Size))
        {Withdraw(Target);L=nullptr;}
    }
    if (!L)
    {
        if (Locals.Num()>=MaxBodies) return EGuLiGridPermit::Pending;
        L=&Locals.Add(Target);L->StorageIndex=LocalKeys.Add(Target);
        L->Row.Key=Target;L->Row.Other=Source;L->Row.Owner=O.SimulationOwner;L->Row.OwnerEpoch=O.OwnerEpoch;
        L->Row.bPlacement=true;L->Row.Size=Size;L->Row.Team=Team;L->Row.Actual=L->Row.Scope=Rect;L->Desired=Rect;
    }
    if (!L->Row.bPlacement || L->Row.Other!=Source || L->Row.OwnerEpoch!=O.OwnerEpoch || !Same(Rect,L->Row.Actual)) return EGuLiGridPermit::Blocked;
    if (!RefreshAuthority() || !bReady) return EGuLiGridPermit::Pending;
    if (L->bGrant) return EGuLiGridPermit::Granted;
    if (!L->bPending && FPlatformTime::Seconds()>=L->RetryAt)
    {++L->Row.Nonce;L->bPending=true;Dirty(*L);}
    return L->bDenied?EGuLiGridPermit::Blocked:EGuLiGridPermit::Pending;
}
void FGuLiGridNetwork::ObjectChanged(const FGuLiHybridObjectKey& Key)
{
    FGuLiHybridObjectState O;auto* H=Hybrid();if (!H) return;
    const bool Exists=H->GetObjectView(Key,O);
    const bool Ground=Key.Kind==EGuLiHybridObjectKind::Soldier || Key.Kind==EGuLiHybridObjectKind::Miner
        || Key.Kind==EGuLiHybridObjectKind::Engineer || Key.Kind==EGuLiHybridObjectKind::GroundPlayer;
    if (!Exists || O.LifeState>=2) {DisplacementHolds.Remove(Key);DisplacementRevisions.Remove(Key);}
    else if (Ground)
    {
        const auto* Old=DisplacementRevisions.Find(Key);
        if (!Old || *Old!=O.RelationRevision)
        {
            if (Displacement(*H,O) && GuLiGrid::HoldsDisplacement(O)) DisplacementHolds.Add(Key);else DisplacementHolds.Remove(Key);
            if (Old || DisplacementRevisions.Num()<MaxBodies) DisplacementRevisions.Add(Key,O.RelationRevision);
        }
    }
    const bool Held=DisplacementHolds.Contains(Key);
    const bool Alive=Exists && O.LifeState==1 && !O.bPhased && !Held;
    if (const auto* L=Locals.Find(Key);L && !L->Row.bPlacement
        && (!Alive || O.SimulationOwner!=L->Row.Owner || O.OwnerEpoch!=L->Row.OwnerEpoch)) Withdraw(Key);
    if (Alive && IsLocal(O.SimulationOwner))
        if (auto* Actor=H->FindBoundActor(Key))
        {
            if (auto* Production=Actor->FindComponentByClass<UGuLiBuildingProductionComponent>()) Production->RecoverGridBirths();
            if (auto* Gifts=Actor->FindComponentByClass<UGuLiStrongholdFacilitiesComponent>()) Gifts->RecoverGridGifts();
        }
    if (Alive) if (const auto* L=Locals.Find(Key);L && L->Row.bPlacement) RemoveLocal(Key);
    if (Grid.IsValid())
        if (const auto* R=Grid->Traffic().Find(PlacementKey(Key));R && Alive)
            Grid->Traffic().Remove(R->Key,R->Owner,R->OwnerEpoch);
    if (!IsLocal(Authority)) return;
    if (const auto* Old=Bodies.Find(Key))
    {
        if (!Alive && !Old->bPlacement) {RemoveBody(Key);return;}
        if (Alive && Old->bPlacement)
        {
            if (Key.Kind==EGuLiHybridObjectKind::Building || Key.Kind==EGuLiHybridObjectKind::Territory)
            {RemoveBody(Key);return;}
            auto R=*Old;R.bPlacement=false;R.Other={};R.Owner=O.SimulationOwner;R.OwnerEpoch=O.OwnerEpoch;R.Nonce=0;
            // The placement is the committed landing/birth position; the last 10 Hz pose may still be at the source.
            R.Actual=R.Scope;
            PublishBody(R);
        }
    }
}
void FGuLiGridNetwork::RecoverPlacement(const FGuLiHybridObjectState& Source,const FGuLiHybridObjectKey& Target,
    const GuLiGrid::FRect& Rect,int32 Size,bool AppliedDisplacement)
{
    if (!IsLocal(Source.SimulationOwner) || !Target.IsValid() || !Source.OwnerEpoch) return;
    FGuLiHybridObjectState Born;
    if (AppliedDisplacement)
    {if (Source.Key!=Target || !GuLiGrid::PendingDisplacementAt(Source,*Grid->Snapshot(),Rect,Size)) return;}
    else if (Hybrid()->GetObjectView(Target,Born) && Born.LifeState!=0) return;
    auto* L=Locals.Find(Target);
    if (L && L->Row.bPlacement && L->Row.OwnerEpoch==Source.OwnerEpoch && L->bGrant) return;
    if (L) RemoveLocal(Target);
    if (Locals.Num()>=MaxBodies) {Hybrid()->Suspend(TEXT("Grid placement recovery capacity exceeded"));return;}
    L=&Locals.Add(Target);L->StorageIndex=LocalKeys.Add(Target);L->Row.Key=Target;L->Row.Other=Source.Key;
    L->Row.Owner=Source.SimulationOwner;L->Row.OwnerEpoch=Source.OwnerEpoch;L->Row.Size=Size;L->Row.Team=uint8(Source.Team);
    L->Row.bPlacement=true;L->Row.bGranted=true;L->Row.Actual=L->Row.Scope=L->Granted=L->Desired=Rect;
    L->bGrant=true;L->bDirty=false;L->Row.Nonce=1;
    // Submitted production work is restored before movement resumes. Its occupied exit is not a new grant.
    if (RefreshAuthority() && bReady) Queue(EPacket::Sync,Authority,&L->Row);
}
void FGuLiGridNetwork::Stop(const FGuLiHybridObjectKey& Key,const GuLiGrid::FRect& Actual)
{
    auto* L=Locals.Find(Key);if (!L) return;
    if (!L->bGrant && !L->bPending && !L->bDirty && Same(L->Row.Scope,Actual) && Same(L->Row.Actual,Actual)) return;
    L->Row.Actual=Actual;L->Desired=Actual;L->Row.Scope=Actual;L->Granted=Actual;
    L->bGrant=false;L->bPending=false;L->bDenied=false;L->bDirty=false;++L->Row.Nonce;
    // Explicit trim is sent before any later request on this reliable peer channel.
    if (L->Row.bPlacement && AuthorityEpoch) Queue(EPacket::Trim,Authority,&L->Row);
}
void FGuLiGridNetwork::Withdraw(const FGuLiHybridObjectKey& Key)
{
    if (auto* L=Locals.Find(Key)) {++L->Row.Nonce;if (AuthorityEpoch) Queue(EPacket::Withdraw,Authority,&L->Row);RemoveLocal(Key);}
}
void FGuLiGridNetwork::Handle(FPacket& P,FRow* R)
{
    if (P.Epoch!=AuthorityEpoch || P.Roster!=Roster) return;
    const bool Server=IsLocal(Authority);
    if (P.Type==EPacket::Ready) {if (P.Peer==Authority) bReady=true;return;}
    if (P.Type==EPacket::SyncDone) {if (Server) Synced.Add(P.Peer);return;}
    if (!R) return;
    if (P.Type==EPacket::Admission || P.Type==EPacket::SyncAdmissions)
    {
        if (P.Type==EPacket::Admission?P.Peer!=Authority:!Server || bReady) return;
        if (!R->Event.IsValid() || !R->Owner.IsValid() || !R->Sequence) return;
        if (const auto* Old=Admissions.Find(R->Event);Old && (Old->Controller!=R->Owner || Old->Sequence!=R->Sequence))
        {Hybrid()->Suspend(TEXT("Conflicting grid command admission"));return;}
        if (const auto* Old=SequenceEvents.Find(R->Sequence);Old && *Old!=R->Event)
        {Hybrid()->Suspend(TEXT("Duplicate grid command sequence"));return;}
        if (!Admissions.Contains(R->Event) && Admissions.Num()>=MaxAdmissions)
        {Hybrid()->Suspend(TEXT("Grid command history capacity exceeded"));return;}
        Admissions.Add(R->Event,{R->Owner,R->Sequence});SequenceEvents.Add(R->Sequence,R->Event);
        Counter=FMath::Max(Counter,R->Sequence);Queries.Remove(R->Event);return;
    }
    if (P.Type==EPacket::Admit || P.Type==EPacket::QueryAdmission) return; // Retired wire values.
    if (P.Type==EPacket::Reply)
    {
        if (P.Peer!=Authority) return;auto* L=Locals.Find(R->Key);
        if (!L || R->Nonce!=L->Row.Nonce || R->OwnerEpoch!=L->Row.OwnerEpoch) return;
        L->bPending=false;L->bDenied=!R->bGranted && R->bPhysicalBlocked;L->RetryAt=FPlatformTime::Seconds()+.5;
        if (R->bGranted)
        {
            // Limit ourselves first, then release the obsolete tail without another round trip.
            L->Granted=GuLiGrid::FRect::Union(L->Desired,L->Row.Actual);L->Row.Scope=L->Granted;L->bGrant=true;
            Queue(EPacket::Trim,Authority,&L->Row);
        }
        return;
    }
    if (P.Type==EPacket::Yield)
    {
        // Retired wire value: yielding is handled by the restored movement/avoidance code.
        return;
    }
    if (!Server) return;FGuLiHybridObjectState O;
    if (P.Type==EPacket::Withdraw)
    {
        if (!Hybrid()->GetObjectView(R->bPlacement?R->Other:R->Key,O) || O.SimulationOwner!=P.Peer || O.OwnerEpoch!=R->OwnerEpoch) return;
        if (const auto* B=Bodies.Find(R->Key);B && B->bPlacement==R->bPlacement && B->Owner==P.Peer && B->OwnerEpoch==R->OwnerEpoch && R->Nonce>=B->Nonce)
        RemoveBody(R->Key);
        return;
    }
    if (!CurrentBody(P.Peer,*R,O)) return;
    const auto* Existing=Bodies.Find(R->Key);
    if (Existing && Existing->bPlacement==R->bPlacement
        && (Existing->OwnerEpoch>R->OwnerEpoch || Existing->OwnerEpoch==R->OwnerEpoch && Existing->Nonce>R->Nonce)) return;
    if (P.Type==EPacket::Sync)
    {
        if (!R->bPlacement) {R->Scope=R->Actual;R->bGranted=false;}
        if (!bReady || R->bPlacement && Existing && Existing->bPlacement && Existing->Other==R->Other
            && Same(Existing->Scope,R->Scope)) PublishBody(*R);
        else if (R->bPlacement && R->bGranted)
        {
            // A submitted birth/applied landing can be restored after the peer's initial sync.
            // Never silently lose its certified scope, or overwrite another owner's live corridor.
            TArray<FGuLiHybridObjectKey> Hits;AuthorityIndex.Query(R->Scope,R->Key,Hits);
            if (Existing || !Hits.IsEmpty() || !Same(R->Actual,R->Scope) || !Grid->Snapshot()->CoveredTerrainClear(R->Scope))
                Hybrid()->Suspend(TEXT("Recovered grid placement conflicts with retained traffic state"));
            else PublishBody(*R);
        }
        return;
    }
    if (P.Type==EPacket::Trim)
    {
        // A stopped owner may cancel all future scope; movement never follows an unacknowledged expansion.
        if (Existing && Existing->OwnerEpoch==R->OwnerEpoch && Existing->Scope.Contains(R->Scope)) PublishBody(*R);
        else if (Same(R->Scope,R->Actual)) PublishBody(*R);
        return;
    }
    if (P.Type!=EPacket::Request || !bReady || !R->bPlacement) return;
    if (R->bPlacement && (O.LifeState!=1 || !Same(R->Actual,R->Scope) || !Grid->Snapshot()->CoveredTerrainClear(R->Scope)))
    {R->bGranted=false;R->bPhysicalBlocked=true;Queue(EPacket::Reply,P.Peer,R);return;}
    // Keep the previous scope until a grant is applied, or an explicit trim is received.
    if (Existing && Existing->OwnerEpoch==R->OwnerEpoch && !R->Scope.Contains(Existing->Scope)) return;
    TArray<FGuLiHybridObjectKey> Hits;AuthorityIndex.Query(R->Scope,R->Key,Hits);R->bGranted=true;R->bPhysicalBlocked=false;
    for (const auto& K:Hits)
    {
        const auto* B=Bodies.Find(K);if (!B) continue;
        FGuLiHybridObjectState Other;
        if (!Hybrid()->GetObjectView(B->bPlacement?B->Other:K,Other) || Other.LifeState!=1 || !B->bPlacement && Other.bPhased) continue;
        R->bGranted=false;
        R->bPhysicalBlocked=true;

    }
    if (R->bGranted) PublishBody(*R);
    Queue(EPacket::Reply,P.Peer,R);
}
void FGuLiGridNetwork::Advance(uint64 Tick,double Delta)
{
    if (!Grid.IsValid() || !Grid->IsReady() || !RefreshAuthority()) return;
    if (AdvancedFrame==GFrameCounter) return;AdvancedFrame=GFrameCounter;
    auto& Budget=Grid->FrameBudget();FGuLiNavigationWorkBudget::FScope Scope(Budget);
    TGuardValue<double> Slice(Budget.LimitSeconds,FMath::Min(Budget.LimitSeconds,Budget.Elapsed()+.0003));
    auto* S=Session();if (!S) return;
    // Drain before handling more requests; a burst of requests must not starve its own replies.
    Flush(Budget);
    if (!bReady && bSyncDone && FPlatformTime::Seconds()>=SyncRetryAt)
    {bSyncDone=false;SyncCursor=SyncAdmissionCursor=0;Admissions.GenerateKeyArray(SyncAdmissionKeys);}
    if (!bSyncDone)
    {
        int32 Count=0;
        while (SyncCursor<LocalKeys.Num() && Count++<RowsPerPacket && Budget.CanWork())
        {
            auto* L=Locals.Find(LocalKeys[SyncCursor++]);if (!L) continue;
            if (L->Row.bPlacement && !L->bGrant) continue;
            FRow R=L->Row;R.Scope=L->bGrant?GuLiGrid::FRect::Union(L->Granted,R.Actual):R.Actual;Queue(EPacket::Sync,Authority,&R);
        }
        while (SyncCursor==LocalKeys.Num() && SyncAdmissionCursor<SyncAdmissionKeys.Num() && Count++<RowsPerPacket && Budget.CanWork())
        {
            const auto E=SyncAdmissionKeys[SyncAdmissionCursor++];if (const auto* A=Admissions.Find(E))
            {FRow R;R.Event=E;R.Owner=A->Controller;R.Sequence=A->Sequence;Queue(EPacket::SyncAdmissions,Authority,&R);}
        }
        if (SyncCursor==LocalKeys.Num() && SyncAdmissionCursor==SyncAdmissionKeys.Num())
        {Queue(EPacket::SyncDone,Authority);bSyncDone=true;SyncAdmissionKeys.Reset();SyncRetryAt=FPlatformTime::Seconds()+2.;}
    }
    while (InCursor<Incoming.Num() && Budget.CanWork() && Outgoing.Num()<MaxPackets-32)
    {
        auto& P=Incoming[InCursor];
        if (P.Rows.IsEmpty()) Handle(P,nullptr);
        else while (P.Cursor<P.Rows.Num() && Budget.CanWork() && Outgoing.Num()<MaxPackets-32) Handle(P,&P.Rows[P.Cursor++]);
        if (P.Cursor<P.Rows.Num()) break;
        QueuedBytes-=P.Rows.GetAllocatedSize()+sizeof(FPacket);P.Rows.Empty();++InCursor;
    }
    if (InCursor==Incoming.Num()) {Incoming.Reset();InCursor=0;}
    if (IsLocal(Authority) && !bReady && bSyncDone)
    {
        bool All=true;for (const auto& M:S->GetMembers()) if (M.bOnline && M.bReady && !Synced.Contains(M.Player)) {All=false;break;}
        // Every peer is frozen. Discard scopes absent from their current ownership reports.
        if (All)
        {
            while (ReconcileCursor<AuthorityKeys.Num() && Budget.CanWork())
            {
                const auto K=AuthorityKeys[ReconcileCursor];
                if (Bodies.FindChecked(K).SyncGeneration!=SyncGeneration) RemoveBody(K);else ++ReconcileCursor;
            }
            if (ReconcileCursor==AuthorityKeys.Num()) {bReady=true;Broadcast(EPacket::Ready);}
        }
    }
    // Round-robin dirty rows; no per-frame copy of the ownership roster or all reservations.
    int32 Count=RowsPerPacket;
    while (Count-- && DirtyCursor<DirtyKeys.Num() && bReady && Budget.CanWork() && Outgoing.Num()<MaxPackets-32)
    {
        const auto K=DirtyKeys[DirtyCursor++];auto* L=Locals.Find(K);FGuLiHybridObjectState O;
        if (!L || !CurrentBody(S->GetLocalPlayerId(),L->Row,O))
        {RemoveLocal(K);continue;}
        L->bQueued=false;
        if (L->bDirty) {Queue(EPacket::Request,Authority,&L->Row);L->SentNonce=L->Row.Nonce;L->bDirty=false;}
    }
    if (DirtyCursor==DirtyKeys.Num()) {DirtyKeys.Reset();DirtyCursor=0;}
    else if (DirtyCursor>=MaxBodies) {DirtyKeys.RemoveAt(0,DirtyCursor,EAllowShrinking::No);DirtyCursor=0;}
    if (IsLocal(Authority) && !AuthorityKeys.IsEmpty() && Budget.CanWork())
    {
        MaintenanceCursor%=AuthorityKeys.Num();const auto K=AuthorityKeys[MaintenanceCursor];FGuLiHybridObjectState O;
        auto* B=Bodies.Find(K);
        if (B && (!Hybrid()->GetObjectView(B->bPlacement?B->Other:K,O) || O.LifeState!=1 || !B->bPlacement && O.bPhased))
        RemoveBody(K);
        else ++MaintenanceCursor;
    }
    Flush(Budget);
}
void FGuLiGridNetwork::Flush(FGuLiNavigationWorkBudget& Budget)
{
    int32 Sent=0;
    while (Sent<Outgoing.Num() && Budget.CanWork())
    {
        auto& P=Outgoing[Sent];FGuLiHybridStateArchive A(PacketLimit);uint32 Schema=2;A.Value(Schema);
        A.Enum(P.Type,uint8(EPacket::Withdraw));A.Value(P.Epoch);A.Value(P.Roster);A.Array(P.Rows,RowsPerPacket,SerializeRow);TArray<uint8> Bytes;
        if (!A.Finish(Bytes)) {Hybrid()->Suspend(TEXT("Invalid grid traffic batch"));return;}
        if (!Hybrid()->SendSharedStateEvent(System,P.Peer,P.Event,Bytes)) break;++Sent;
    }
    if (Sent) Outgoing.RemoveAt(0,Sent,EAllowShrinking::No);
}
void FGuLiGridNetwork::SerializeSave(FGuLiHybridStateArchive& A,FSave& Save) const
{
    uint32 Schema=2;A.Value(Schema);if (Schema!=2) A.Reject();A.Value(Save.Counter);
    A.Array(Save.Bodies,MaxBodies,SerializeRow);
    A.Array(Save.Admissions,MaxAdmissions,[](auto& B,auto& R){B.Value(R.Event);B.Value(R.Owner);B.Value(R.Sequence);});
}
bool FGuLiGridNetwork::Capture(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Bytes) const
{
    if (Objects.Num()!=1 || Objects[0]!=Anchor) return false;
    // Stream the checkpoint once. No roster clone or O(N log N) sorting on the backup tick.
    FGuLiHybridStateArchive A(CheckpointLimit);uint32 Schema=2;A.Value(Schema);auto SavedCounter=Counter;A.Value(SavedCounter);
    uint32 Count=Bodies.Num();A.Value(Count);
    for (const auto& Pair:Bodies) {auto R=Pair.Value;SerializeRow(A,R);}
    Count=Admissions.Num();A.Value(Count);
    for (const auto& Pair:Admissions) {auto E=Pair.Key;auto Owner=Pair.Value.Controller;auto Seq=Pair.Value.Sequence;A.Value(E);A.Value(Owner);A.Value(Seq);}
    return A.Finish(Bytes);
}
bool FGuLiGridNetwork::Validate(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Local) const
{
    if (Local.Num()!=1 || Local[0]!=Anchor || Pieces.Num()!=1) return false;
    const auto& P=Pieces[0];if (P.Name!=System || P.Version!=Version() || P.Objects.Num()!=1 || P.Objects[0]!=Anchor) return false;
    FSave Save;FGuLiHybridStateArchive A(P.Bytes,CheckpointLimit);SerializeSave(A,Save);if (!A.Done()) return false;
    TSet<FGuLiHybridObjectKey> Keys;TSet<FGuid> Events;TSet<uint64> Sequences;
    for (const auto& R:Save.Bodies)
    {if (!R.Key.IsValid() || !R.Owner.IsValid() || !R.OwnerEpoch || !R.Scope.Contains(R.Actual) || Keys.Contains(R.Key)) return false;Keys.Add(R.Key);}
    for (const auto& R:Save.Admissions)
    {if (!R.Event.IsValid() || !R.Owner.IsValid() || !R.Sequence || R.Sequence>Save.Counter || Events.Contains(R.Event) || Sequences.Contains(R.Sequence)) return false;Events.Add(R.Event);Sequences.Add(R.Sequence);}
    return true;
}
bool FGuLiGridNetwork::Restore(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Local,EGuLiHybridRestoreStage Stage)
{
    if (Stage==EGuLiHybridRestoreStage::Detach) {bReady=false;return true;}
    if (Stage!=EGuLiHybridRestoreStage::Create) return true;
    if (!Validate(Pieces,Local)) return false;
    FSave Save;FGuLiHybridStateArchive A(Pieces[0].Bytes,CheckpointLimit);SerializeSave(A,Save);if (!A.Done()) return false;
    Bodies.Reset();AuthorityKeys.Reset();AuthorityIndex={};Admissions.Reset();SequenceEvents.Reset();Counter=Save.Counter;
    for (auto& R:Save.Bodies)
    {if (!R.bPlacement) {R.Scope=R.Actual;R.bGranted=false;}PublishBody(R);}
    for (const auto& R:Save.Admissions) {Admissions.Add(R.Event,{R.Owner,R.Sequence});SequenceEvents.Add(R.Sequence,R.Event);}
    AuthorityEpoch=0;bReady=false;return true;
}
