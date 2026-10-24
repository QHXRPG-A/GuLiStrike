#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Navigation/Grid/GuLiGridTypes.h"
#include "Network/GuLiHybridObjectKey.h"

struct FGuLiGridTrafficRecord
{
    FGuLiHybridObjectKey Key;
    FGuid Owner;
    uint64 OwnerEpoch=0;
    uint8 Team=0;
    int32 Footprint=1;
    GuLiGrid::FTrafficPriority Priority;
    GuLiGrid::FRect Occupied,Reserved;
    bool bReserved=false;
    mutable uint64 LastQuery=0;
};

/** Game-thread ledger. Writes require the currently admitted owner AND epoch. */
class FGuLiGridTraffic final
{
public:
    static constexpr int32 MaximumObjects=32768;
    bool Admit(const FGuLiHybridObjectKey& Key,const FGuid& Owner,uint64 Epoch,uint8 Team,
        const GuLiGrid::FRect& Occupied,const GuLiGrid::FTrafficPriority& Priority,int32 Footprint=1);
    bool Commit(const FGuLiHybridObjectKey& Key,const FGuid& Owner,uint64 Epoch,const GuLiGrid::FRect& Actual);
    bool Cancel(const FGuLiHybridObjectKey& Key,const FGuid& Owner,uint64 Epoch);
    /** Authority-only publication after its cross-owner conflict query. */
    bool SetAuthorityScope(const FGuLiHybridObjectKey& Key,const FGuid& Owner,uint64 Epoch,const GuLiGrid::FRect& Scope);
    bool Remove(const FGuLiHybridObjectKey& Key,const FGuid& Owner,uint64 Epoch);
    void Query(const GuLiGrid::FRect& Rect,const FGuLiHybridObjectKey& Ignore,TArray<FGuLiHybridObjectKey>& Out) const;
    const FGuLiGridTrafficRecord* Find(const FGuLiHybridObjectKey& Key) const { return Objects.Find(Key); }
    FGuLiGridTrafficRecord* Find(const FGuLiHybridObjectKey& Key) { return Objects.Find(Key); }
    SIZE_T Bytes() const;
private:
    static constexpr int32 BucketSide=4;
    TMap<FGuLiHybridObjectKey,FGuLiGridTrafficRecord> Objects;
    TMap<FIntPoint,TArray<FGuLiHybridObjectKey>> Buckets;
    mutable uint64 QuerySerial=0;
    static GuLiGrid::FRect Bounds(const FGuLiGridTrafficRecord& R);
    void Index(const FGuLiGridTrafficRecord& R,bool bAdd);
    FGuLiGridTrafficRecord* Authorized(const FGuLiHybridObjectKey& Key,const FGuid& Owner,uint64 Epoch);
};
