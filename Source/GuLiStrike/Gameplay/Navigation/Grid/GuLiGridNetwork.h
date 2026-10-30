#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Navigation/Grid/GuLiGridTraffic.h"
#include "Network/GuLiHybridWorldSubsystem.h"

class UGuLiGroundGridSubsystem;
class FGuLiHybridStateArchive;
struct FGuLiNavigationWorkBudget;

enum class EGuLiGridPermit : uint8 { Pending,Granted,Blocked };

/** Reliable placement arbitration and actual occupancy. Ordinary Mass movement has no permits. */
class FGuLiGridNetwork final : public IGuLiHybridSharedStateAdapter
{
public:
    static const FName System;
    explicit FGuLiGridNetwork(UGuLiGroundGridSubsystem& Grid);
    UObject* LifetimeOwner() const override;
    uint32 Version() const override { return 3; }
    bool UsesObject(const FGuLiHybridObjectKey& Key) const override { return Key==Anchor; }
    void Advance(uint64 Tick,double Delta) override;
    bool ReceiveEvent(const FGuid& Peer,const FGuid& Event,TConstArrayView<uint8> Bytes) override;
    bool Capture(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Bytes) const override;
    bool Validate(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Local) const override;
    bool Restore(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Local,EGuLiHybridRestoreStage Stage) override;
    void Observe(const FGuLiGridTrafficRecord& Body);
    void PublishedPose(const FGuLiHybridObjectKey& Key,const FGuid& Owner,uint64 Epoch,const GuLiGrid::FRect& Actual);
    void Stop(const FGuLiHybridObjectKey& Key,const GuLiGrid::FRect& Actual);
    void Withdraw(const FGuLiHybridObjectKey& Key);
    EGuLiGridPermit PermitPlacement(const FGuLiHybridObjectKey& Source,const FGuLiHybridObjectKey& Target,const GuLiGrid::FRect& Rect,int32 Size,uint8 Team,EGuLiUnitMovementLayer Layer=EGuLiUnitMovementLayer::Ground);
    /** Only a restored, already submitted birth may carry a certified placement across owners. */
    void RecoverPlacement(const FGuLiHybridObjectState& Source,const FGuLiHybridObjectKey& Target,const GuLiGrid::FRect& Rect,int32 Size,
        bool bAppliedDisplacement=false,EGuLiUnitMovementLayer Layer=EGuLiUnitMovementLayer::Ground);
    uint64 PlanningGeneration() const {return SyncGeneration;}
    bool HoldsBody(const FGuLiHybridObjectKey& Key) const {return DisplacementHolds.Contains(Key);}
    void ObjectChanged(const FGuLiHybridObjectKey& Key);
    static FGuLiHybridObjectKey PlacementKey(FGuLiHybridObjectKey Key) {Key.Kind=EGuLiHybridObjectKind::BusinessWork;return Key;}
    bool IsLocal(const FGuid& Owner) const;
private:
    enum class EPacket : uint8 { Sync,SyncAdmissions,SyncDone,Ready,Admit,Admission,QueryAdmission,Request,Reply,Trim,Yield,Withdraw };
    struct FRow
    {
        FGuLiHybridObjectKey Key,Other;
        FGuid Owner,Event;
        uint64 OwnerEpoch=0,Nonce=0,Sequence=0,ControlEpoch=0;
        int32 Size=1;
        uint8 Team=0;
        EGuLiUnitMovementLayer MovementLayer=EGuLiUnitMovementLayer::Ground;
        GuLiGrid::FTrafficPriority Priority;
        GuLiGrid::FRect Actual,Scope,Passage;
        bool bGranted=false,bPlacement=false,bPhysicalBlocked=false;
        int32 StorageIndex=INDEX_NONE;
        uint64 SyncGeneration=0;
    };
    struct FPacket { EPacket Type=EPacket::Request;FGuid Peer,Event;uint64 Epoch=0,Roster=0;TArray<FRow> Rows;int32 Cursor=0; };
    struct FLocal
    {
        FRow Row;
        GuLiGrid::FRect Granted,Desired;
        bool bGrant=false,bPending=false,bDenied=false,bDirty=true;
        uint64 SentNonce=0;
        double RetryAt=0;
        int32 StorageIndex=INDEX_NONE;
        bool bQueued=false;
    };
    struct FAdmission { FGuid Controller;uint64 Sequence=0; };
    struct FSave { uint64 Counter=0;TArray<FRow> Bodies,Admissions; };
    TWeakObjectPtr<UGuLiGroundGridSubsystem> Grid;
    FGuLiHybridObjectKey Anchor;
    FGuid Authority;
    uint64 AuthorityEpoch=0,Counter=0,Roster=0,AdvancedFrame=MAX_uint64,SyncGeneration=0;
    int32 ReconcileCursor=0;
    double SyncRetryAt=0;
    bool bReady=false,bSyncDone=false;
    int32 SyncCursor=0,SyncAdmissionCursor=0,OutCursor=0,InCursor=0,MaintenanceCursor=0;
    TSet<FGuid> Synced;
    TArray<FGuLiHybridObjectKey> LocalKeys,AuthorityKeys;
    TMap<FGuLiHybridObjectKey,FLocal> Locals;
    TMap<FGuLiHybridObjectKey,FRow> Bodies;
    TSet<FGuLiHybridObjectKey> DisplacementHolds;
    TMap<FGuLiHybridObjectKey,uint64> DisplacementRevisions;
    TArray<FGuLiHybridObjectKey> DirtyKeys;
    int32 DirtyCursor=0;
    FGuLiGridTraffic AuthorityIndex;
    TMap<FGuid,FAdmission> Admissions;
    TArray<FGuid> SyncAdmissionKeys;
    TMap<uint64,FGuid> SequenceEvents;
    TMap<FGuid,double> Queries;
    TArray<FPacket> Incoming,Outgoing;
    SIZE_T QueuedBytes=0;
    UGuLiHybridWorldSubsystem* Hybrid() const;
    UGuLiHybridSessionSubsystem* Session() const;
    bool RefreshAuthority();
    uint64 CurrentRoster() const;
    void Dirty(FLocal& Local);
    void RemoveLocal(const FGuLiHybridObjectKey& Key);
    void RemoveBody(const FGuLiHybridObjectKey& Key);
    void Queue(EPacket Type,const FGuid& Peer,const FRow* Row=nullptr);
    void Broadcast(EPacket Type,const FRow* Row=nullptr);
    void Flush(FGuLiNavigationWorkBudget& Budget);
    void Handle(FPacket& Packet,FRow* Row);
    bool CurrentBody(const FGuid& Peer,const FRow& Row,FGuLiHybridObjectState& View) const;
    void PublishBody(const FRow& Row);
    void SerializeSave(FGuLiHybridStateArchive& A,FSave& Save) const;
    static void SerializeRow(FGuLiHybridStateArchive& A,FRow& Row);
    static bool Decode(TConstArrayView<uint8> Bytes,FPacket& Packet);
};
