#include "Gameplay/Navigation/GuLiOreCircleClustering.h"

namespace GuLiCircleAvoidance
{
namespace
{
FCircle Enclose(const FCircle& A,const FCircle& B)
{
    FCircle C=A;C.Id=FMath::Min(A.Id,B.Id);C.Version=FMath::Max(A.Version,B.Version);
    C.MinimumZ=FMath::Min(A.MinimumZ,B.MinimumZ);C.MaximumZ=FMath::Max(A.MaximumZ,B.MaximumZ);
    C.LayerMask=A.LayerMask|B.LayerMask;
    FVector Difference=B.Center-A.Center;Difference.Z=0;
    const double Distance=Difference.Size2D();
    if (Distance+FMath::Min(A.Radius,B.Radius)<=FMath::Max(A.Radius,B.Radius))
    {
        const auto& Larger=A.Radius>=B.Radius?A:B;C.Center=Larger.Center;C.Radius=Larger.Radius;
    }
    else
    {
        C.Radius=float((Distance+A.Radius+B.Radius)*.5);
        // Round outward. The aggregate must contain every source circle even
        // when converting the double construction back to a float radius.
        C.Radius=FMath::Max(C.Radius,A.Radius);
        if (Distance>UE_DOUBLE_SMALL_NUMBER) C.Center=A.Center+Difference*((C.Radius-A.Radius)/Distance);
        C.Radius=FMath::Max(C.Radius,float(FVector::Dist2D(C.Center,A.Center)+A.Radius));
        C.Radius=FMath::Max(C.Radius,float(FVector::Dist2D(C.Center,B.Center)+B.Radius));
        C.Radius+=.001f;
    }
    return C;
}

bool InvadesPassage(const FCircle& C,TConstArrayView<FProtectedPassage> Passages)
{
    for (const auto& P:Passages)
    {
        if (C.MaximumZ<P.MinimumZ || C.MinimumZ>P.MaximumZ) continue;
        FVector D=P.To-P.From;D.Z=0;const FVector Offset=C.Center-P.From;
        const double T=D.SizeSquared2D()>UE_DOUBLE_SMALL_NUMBER
            ?FMath::Clamp((Offset.X*D.X+Offset.Y*D.Y)/D.SizeSquared2D(),0.,1.):0.;
        if (FVector::DistSquared2D(C.Center,P.From+D*T)<FMath::Square(double(C.Radius)+P.Radius)) return true;
    }
    return false;
}
}

void BuildOreCluster(TConstArrayView<FCircle> Members,TConstArrayView<FProtectedPassage> Passages,
    float MaximumRadiusRatio,float MaximumAreaRatio,TArray<FOreAggregate>& Out,FClusteringCounters& Counters)
{
    Out.Reset();
    if (!ensure(MaximumRadiusRatio>=1 && MaximumAreaRatio>=1)) return;
    TArray<FCircle> Ordered(Members);Ordered.Sort([](const auto& A,const auto& B){return A.Id<B.Id;});
    TMap<uint64,int32> ById;
    for (int32 I=0;I<Ordered.Num();++I)
    {
        const auto& C=Ordered[I];
        if (!ensure(C.Id && C.Radius>0 && !C.Center.ContainsNaN() && !ById.Contains(C.Id))) continue;
        ById.Add(C.Id,I);
        auto& Group=Out.AddDefaulted_GetRef();Group.Circle=C;Group.Members.Add(C.Id);
        Group.MemberRadiusSquaredSum=double(C.Radius)*C.Radius;Group.MaximumMemberRadius=C.Radius;
    }
    // Stable greedy merging. Adjacency is tested against source members, not
    // just enclosing circles, so an empty gap cannot create a false bridge.
    for (int32 I=0;I<Out.Num();++I)
    {
        for (int32 J=I+1;J<Out.Num();)
        {
            auto& A=Out[I];const auto& B=Out[J];
            if (FVector::DistSquared2D(A.Circle.Center,B.Circle.Center)>FMath::Square(double(A.Circle.Radius)+B.Circle.Radius))
            {++J;continue;}
            bool Adjacent=false;
            for (uint64 AId:A.Members)
            {
                const auto& NodeA=Ordered[ById[AId]];
                for (uint64 BId:B.Members)
                {
                    const auto& NodeB=Ordered[ById[BId]];++Counters.MemberPairChecks;
                    if (FVector::DistSquared2D(NodeA.Center,NodeB.Center)<=FMath::Square(double(NodeA.Radius)+NodeB.Radius)+.0001)
                    {Adjacent=true;break;}
                }
                if (Adjacent) break;
            }
            if (!Adjacent) {++J;continue;}
            const FCircle Combined=Enclose(A.Circle,B.Circle);
            const double AreaSum=A.MemberRadiusSquaredSum+B.MemberRadiusSquaredSum;
            const float MaximumMember=FMath::Max(A.MaximumMemberRadius,B.MaximumMemberRadius);
            if (Combined.Radius>MaximumMember*MaximumRadiusRatio
                || double(Combined.Radius)*Combined.Radius>AreaSum*MaximumAreaRatio)
            {++Counters.SizeRejections;++J;continue;}
            if (InvadesPassage(Combined,Passages)) {++Counters.PassageRejections;++J;continue;}
            A.Circle=Combined;A.MemberRadiusSquaredSum=AreaSum;A.MaximumMemberRadius=MaximumMember;
            A.Members.Append(B.Members);A.Members.Sort();Out.RemoveAt(J,1,EAllowShrinking::No);++Counters.Merges;
            // A later member can connect a previously nonadjacent group. Each
            // successful pass removes one group, so the loop is bounded by N-1.
            J=I+1;
        }
    }
}
}
