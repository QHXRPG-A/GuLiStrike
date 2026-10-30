#include "Gameplay/Navigation/Grid/GuLiGridSlots.h"
#include "Commander/Mass/Navigation/GuLiNavigationWorkBudget.h"

bool FGuLiGridSlots::Add(uint32 Id,int32 Size,EGuLiUnitMovementLayer Layer)
{
    if (bFrozen || !Id || Size<1 || Size>64 || MemberCount>=MaximumMembers) return false;
    auto* Bucket=Buckets.FindByPredicate([&](const auto& B){return B.Size==Size && B.Layer==Layer;});
    if (!Bucket) { Bucket=&Buckets.AddDefaulted_GetRef();Bucket->Size=Size;Bucket->Layer=Layer; }
    Bucket->Members.Add(Id);++MemberCount;return true;
}
void FGuLiGridSlots::Freeze(const FGuLiGridSnapshot& Grid,const FVector& Target,int32 MaximumRing)
{
    if (bFrozen) return;
    LastRing=FMath::Clamp(MaximumRing,0,FMath::Max(Grid.Cells.X,Grid.Cells.Y));
    Cells=Grid.Cells;WordsPerRow=FMath::DivideAndRoundUp(Cells.X,64);
    Buckets.Sort([](const auto& A,const auto& B){return A.Layer!=B.Layer ? uint8(A.Layer)<uint8(B.Layer) : A.Size>B.Size;});
    for (auto& B:Buckets)
    {
        B.Center=Grid.AnchorAt(Target,B.Size);B.Center.X=FMath::Clamp(B.Center.X,0,Cells.X-1);B.Center.Y=FMath::Clamp(B.Center.Y,0,Cells.Y-1);
        B.Members.Sort();B.Unavailable.SetNumZeroed(WordsPerRow*Cells.Y);
    }
    bFrozen=true;
}
void FGuLiGridSlots::Occupy(FIntPoint Anchor,int32 Size,EGuLiUnitMovementLayer Layer)
{
    if (!bFrozen) return;
    // Invalidate exactly the candidate anchors whose full footprint would overlap this placement.
    for (auto& B:Buckets)
    {
        if (B.Layer!=Layer) continue;
        const int32 X0=FMath::Max(0,Anchor.X-B.Size+1),Y0=FMath::Max(0,Anchor.Y-B.Size+1);
        const int32 X1=FMath::Min(Cells.X,Anchor.X+Size),Y1=FMath::Min(Cells.Y,Anchor.Y+Size);
        if (X0>=X1 || Y0>=Y1) continue;
        for (int32 Y=Y0;Y<Y1;++Y) for (int32 W=X0/64;W<=(X1-1)/64;++W)
        {
            const int32 Lo=FMath::Max(0,X0-W*64),Hi=FMath::Min(64,X1-W*64);
            const uint64 Mask=(~uint64(0)<<Lo)&(Hi==64?~uint64(0):(uint64(1)<<Hi)-1);
            B.Unavailable[Y*WordsPerRow+W]|=Mask;
        }
    }
}
bool FGuLiGridSlots::Next(const FGuLiGridSnapshot& Grid,FGuLiNavigationWorkBudget& Budget,uint32& Id,FIntPoint& Anchor,int32& Size)
{
    return Next(Grid,Budget,Id,Anchor,Size,[](uint32,FIntPoint,int32){return true;});
}
bool FGuLiGridSlots::Next(const FGuLiGridSnapshot& Grid,FGuLiNavigationWorkBudget& Budget,uint32& Id,FIntPoint& Anchor,int32& Size,
    TFunctionRef<bool(uint32,FIntPoint,int32)> Accept)
{
    if (!bFrozen || Grid.Cells!=Cells) return false;
    FGuLiNavigationWorkBudget::FScope Scope(Budget);
    while (BucketCursor<Buckets.Num() && Budget.CanWork())
    {
        auto& B=Buckets[BucketCursor];
        if (B.Member>=B.Members.Num() || B.Ring>LastRing) { ++BucketCursor;continue; }
        if (Budget.Projections<=0) return false;
        FIntPoint Offset=FIntPoint::ZeroValue;const int32 R=B.Ring,S=B.Step;
        if (R)
        {
            if (S<2*R) Offset={-R+S,-R};
            else if (S<4*R) Offset={R,-R+S-2*R};
            else if (S<6*R) Offset={R-(S-4*R),R};
            else Offset={-R,R-(S-6*R)};
        }
        if (!R) B.Ring=1;
        else if (++B.Step==8*R) { B.Step=0;++B.Ring; }
        const auto P=B.Center+Offset;
        if (!Grid.Contains(P)) continue;
        const int32 Word=P.Y*WordsPerRow+P.X/64;const uint64 Bit=uint64(1)<<(P.X%64);
        if (B.Unavailable[Word]&Bit) continue;
        if (!Budget.TakeProjection()) return false;
        B.Unavailable[Word]|=Bit;
        if (!Grid.Fits(P,B.Size)) continue;
        if (!Accept(B.Members[B.Member],P,B.Size)) continue;
        Id=B.Members[B.Member++];Anchor=P;Size=B.Size;Occupy(P,Size,B.Layer);return true;
    }
    return false;
}
SIZE_T FGuLiGridSlots::Bytes() const
{
    SIZE_T Total=sizeof(*this)+Buckets.GetAllocatedSize();
    for (const auto& B:Buckets) Total+=B.Members.GetAllocatedSize()+B.Unavailable.GetAllocatedSize();return Total;
}
