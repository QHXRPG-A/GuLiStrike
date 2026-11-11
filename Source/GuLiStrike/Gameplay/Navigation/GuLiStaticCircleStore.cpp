#include "Gameplay/Navigation/GuLiStaticCircleStore.h"

namespace GuLiCircleAvoidance
{
namespace
{
FBox Bounds(const FCircle& C)
{
    const double R=C.Radius+C.Clearance+C.ContactMargin;
    return FBox(FVector(C.Center.X-R,C.Center.Y-R,C.MinimumZ),FVector(C.Center.X+R,C.Center.Y+R,C.MaximumZ));
}
bool SameGeometry(const FCircle& A,const FCircle& B)
{
    return A.Source==B.Source && A.Center==B.Center && A.Radius==B.Radius && A.Clearance==B.Clearance && A.ContactMargin==B.ContactMargin
        && A.MinimumZ==B.MinimumZ && A.MaximumZ==B.MaximumZ && A.LayerMask==B.LayerMask;
}
}

bool FStaticCircleStore::Replace(uint64 Source,TConstArrayView<FCircle> Circles,FBox& ChangedBounds)
{
    ChangedBounds=FBox(ForceInit);if (!Source) return false;
    TSet<uint64> Incoming;
    // Validate the complete group before touching live spatial state.
    for (const auto& C:Circles)
    {
        const uint64* Owner=Owners.Find(C.Id);
        if (!C.Id || Incoming.Contains(C.Id) || (Owner && *Owner!=Source) || C.Center.ContainsNaN()
            || !FMath::IsFinite(C.Radius) || C.Radius<=0 || !FMath::IsFinite(C.MinimumZ) || !FMath::IsFinite(C.MaximumZ)
            || C.MinimumZ>C.MaximumZ || !C.LayerMask || !C.StepDelta.IsZero()
            || !FMath::IsFinite(C.Clearance) || C.Clearance<0 || !FMath::IsFinite(C.ContactMargin) || C.ContactMargin<0)
            return false;
        Incoming.Add(C.Id);
    }
    if (const auto* Previous=Groups.Find(Source)) for (uint64 Id:*Previous) if (!Incoming.Contains(Id))
    {
        if (const auto* Circle=Index.Find(Id)) ChangedBounds+=Bounds(*Circle);
        Index.Remove(Id);Owners.Remove(Id);
    }
    TArray<uint64> Group;Group.Reserve(Circles.Num());
    for (auto C:Circles)
    {
        C.Source=Source;
        const auto* Previous=Index.Find(C.Id);
        if (!Previous || !SameGeometry(*Previous,C))
        {
            if (Previous) ChangedBounds+=Bounds(*Previous);
            ChangedBounds+=Bounds(C);Index.Upsert(C);
        }
        else if (Previous->Version!=C.Version) Index.Upsert(C);
        Owners.Add(C.Id,Source);Group.Add(C.Id);
    }
    if (Group.IsEmpty()) Groups.Remove(Source);else Groups.Add(Source,MoveTemp(Group));
    if (ChangedBounds.IsValid) ++Revision;
    return true;
}

void FStaticCircleStore::Remove(uint64 Source,FBox& ChangedBounds)
{Replace(Source,TConstArrayView<FCircle>{},ChangedBounds);}

void FStaticCircleStore::Reset()
{Index.Rebuild(TConstArrayView<FCircle>{});Groups.Reset();Owners.Reset();++Revision;}

SIZE_T FStaticCircleStore::GetAllocatedSize() const
{
    SIZE_T Bytes=Index.GetAllocatedSize()+Groups.GetAllocatedSize()+Owners.GetAllocatedSize();
    for (const auto& Pair:Groups) Bytes+=Pair.Value.GetAllocatedSize();
    return Bytes;
}
}
