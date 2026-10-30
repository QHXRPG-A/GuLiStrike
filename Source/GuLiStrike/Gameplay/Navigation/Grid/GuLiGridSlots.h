#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Units/GuLiUnitMovementLayer.h"
#include "Gameplay/Navigation/Grid/GuLiGridNavigation.h"

/** One command's frozen square packing. No pairwise assignment matrix or per-frame repacking. */
class FGuLiGridSlots final
{
public:
    static constexpr int32 MaximumMembers=32768;
    explicit FGuLiGridSlots(TSharedRef<int32> InQuota):Quota(InQuota) { ++*Quota; }
    ~FGuLiGridSlots() { --*Quota; }
    FGuLiGridSlots(const FGuLiGridSlots&)=delete;
    FGuLiGridSlots& operator=(const FGuLiGridSlots&)=delete;
    bool Add(uint32 StableId,int32 Size,EGuLiUnitMovementLayer Layer=EGuLiUnitMovementLayer::Ground);
    void Freeze(const FGuLiGridSnapshot& Grid,const FVector& Target,int32 MaximumRing=MAX_int32);
    bool Next(const FGuLiGridSnapshot& Grid,FGuLiNavigationWorkBudget& Budget,uint32& Id,FIntPoint& Anchor,int32& Size);
    bool Next(const FGuLiGridSnapshot& Grid,FGuLiNavigationWorkBudget& Budget,uint32& Id,FIntPoint& Anchor,int32& Size,
        TFunctionRef<bool(uint32,FIntPoint,int32)> Accept);
    void Occupy(FIntPoint Anchor,int32 Size,EGuLiUnitMovementLayer Layer=EGuLiUnitMovementLayer::Ground);
    bool IsFinished() const { return bFrozen && BucketCursor>=Buckets.Num(); }
    SIZE_T Bytes() const;
private:
    struct FBucket
    {
        EGuLiUnitMovementLayer Layer=EGuLiUnitMovementLayer::Ground;
        int32 Size=1,Member=0,Ring=0,Step=0;
        FIntPoint Center=FIntPoint::ZeroValue;
        TArray<uint32> Members;
        TArray<uint64> Unavailable;
    };
    TArray<FBucket> Buckets;
    FIntPoint Cells=FIntPoint::ZeroValue;
    int32 WordsPerRow=0,BucketCursor=0,MemberCount=0,LastRing=MAX_int32;
    bool bFrozen=false;
    TSharedRef<int32> Quota;
};
