#include "Gameplay/Navigation/Grid/GuLiGridTraffic.h"

namespace
{
bool ValidRect(const GuLiGrid::FRect& R)
{
    return R.Max.X>R.Min.X && R.Max.Y>R.Min.Y && int64(R.Max.X)-R.Min.X<=128
        && int64(R.Max.Y)-R.Min.Y<=128 && R.Min.X>=-65536 && R.Min.Y>=-65536
        && R.Max.X<=65536 && R.Max.Y<=65536;
}
int32 Bucket(int32 V) { return FMath::FloorToInt32(double(V)/4.); }
}
GuLiGrid::FRect FGuLiGridTraffic::Bounds(const FGuLiGridTrafficRecord& R)
{
    auto Result=R.Occupied;
    if (R.bReserved) Result=GuLiGrid::FRect::Union(Result,R.Reserved);
    return Result;
}
void FGuLiGridTraffic::Index(const FGuLiGridTrafficRecord& R,bool bAdd)
{
    const auto B=Bounds(R);
    for (int32 Y=Bucket(B.Min.Y);Y<=Bucket(B.Max.Y-1);++Y)
    for (int32 X=Bucket(B.Min.X);X<=Bucket(B.Max.X-1);++X)
    {
        const FIntPoint Cell(X,Y);
        if (bAdd) Buckets.FindOrAdd(Cell).Add(R.Key);
        else if (auto* Entries=Buckets.Find(Cell))
        { Entries->RemoveSwap(R.Key,EAllowShrinking::No);if (Entries->IsEmpty()) Buckets.Remove(Cell); }
    }
}
FGuLiGridTrafficRecord* FGuLiGridTraffic::Authorized(const FGuLiHybridObjectKey& Key,const FGuid& Owner,uint64 Epoch)
{
    auto* R=Objects.Find(Key);return R && R->Owner==Owner && R->OwnerEpoch==Epoch?R:nullptr;
}
bool FGuLiGridTraffic::Admit(const FGuLiHybridObjectKey& Key,const FGuid& Owner,uint64 Epoch,uint8 Team,
    const GuLiGrid::FRect& Occupied,const GuLiGrid::FTrafficPriority& Priority,int32 Footprint)
{
    if (!Key.IsValid() || !Owner.IsValid() || !Epoch || !ValidRect(Occupied) || Footprint<1 || Footprint>64) return false;
    if (auto* Old=Objects.Find(Key))
    {
        if (Epoch<Old->OwnerEpoch || (Epoch==Old->OwnerEpoch && Old->Owner!=Owner)) return false;
        Index(*Old,false);
    }
    else if (Objects.Num()>=MaximumObjects) return false;
    auto& R=Objects.FindOrAdd(Key);R.Key=Key;R.Owner=Owner;R.OwnerEpoch=Epoch;R.Team=Team;
    R.Footprint=Footprint;
    R.Occupied=Occupied;R.Priority=Priority;R.bReserved=false;Index(R,true);return true;
}
void FGuLiGridTraffic::Query(const GuLiGrid::FRect& Rect,const FGuLiHybridObjectKey& Ignore,TArray<FGuLiHybridObjectKey>& Out) const
{
    Out.Reset();if (!ValidRect(Rect)) return;
    const uint64 Serial=++QuerySerial;
    for (int32 Y=Bucket(Rect.Min.Y);Y<=Bucket(Rect.Max.Y-1);++Y)
    for (int32 X=Bucket(Rect.Min.X);X<=Bucket(Rect.Max.X-1);++X)
    {
        const auto* Entries=Buckets.Find({X,Y});if (!Entries) continue;
        for (const auto& Key:*Entries)
        {
            if (Key==Ignore) continue;
            const auto* R=Objects.Find(Key);if (!R || R->LastQuery==Serial) continue;
            R->LastQuery=Serial;
            if (R->Occupied.Overlaps(Rect) || (R->bReserved && R->Reserved.Overlaps(Rect))) Out.Add(Key);
        }
    }
}
bool FGuLiGridTraffic::Commit(const FGuLiHybridObjectKey& Key,const FGuid& Owner,uint64 Epoch,const GuLiGrid::FRect& Actual)
{
    auto* R=Authorized(Key,Owner,Epoch);if (!R || !ValidRect(Actual)) return false;
    Index(*R,false);R->Occupied=Actual;R->bReserved=false;Index(*R,true);return true;
}
bool FGuLiGridTraffic::SetAuthorityScope(const FGuLiHybridObjectKey& Key,const FGuid& Owner,uint64 Epoch,const GuLiGrid::FRect& Scope)
{
    auto* R=Authorized(Key,Owner,Epoch);
    if (!R || !ValidRect(Scope) || !Scope.Contains(R->Occupied)) return false;
    Index(*R,false);R->Reserved=Scope;R->bReserved=true;Index(*R,true);return true;
}
bool FGuLiGridTraffic::Cancel(const FGuLiHybridObjectKey& Key,const FGuid& Owner,uint64 Epoch)
{
    auto* R=Authorized(Key,Owner,Epoch);if (!R) return false;
    if (R->bReserved) { Index(*R,false);R->bReserved=false;Index(*R,true); }return true;
}
bool FGuLiGridTraffic::Remove(const FGuLiHybridObjectKey& Key,const FGuid& Owner,uint64 Epoch)
{
    auto* R=Authorized(Key,Owner,Epoch);if (!R) return false;
    Index(*R,false);Objects.Remove(Key);return true;
}
SIZE_T FGuLiGridTraffic::Bytes() const
{
    SIZE_T Total=sizeof(*this)+Objects.GetAllocatedSize()+Buckets.GetAllocatedSize();
    for (const auto& B:Buckets) Total+=B.Value.GetAllocatedSize();return Total;
}
