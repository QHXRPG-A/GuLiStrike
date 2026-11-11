#include "Gameplay/Navigation/GuLiOreClusterGeometry.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"

namespace GuLiCircleAvoidance
{
bool BuildCurrentOreCluster(const FOreShapeCache& Shapes,const UGuLiResourceMapDefinition& Map,
    const FGuLiResourceClusterDefinition& Cluster,TConstArrayView<uint8> Remaining,
    uint64 Version,float MaximumRadiusRatio,float MaximumAreaRatio,float MinimumPassageRadius,
    TConstArrayView<FProtectedPassage> WorkEntrances,TArray<FCircle>& Out,FClusteringCounters& Counters)
{
    Out.Reset();TArray<FCircle> Members;Members.Reserve(Cluster.NodeCount);
    for (int32 I=0;I<Cluster.NodeCount;++I)
    {
        const int32 Index=Cluster.FirstNodeIndex+I;
        if (!Map.Nodes.IsValidIndex(Index)) return false;
        const auto& Node=Map.Nodes[Index];
        if (!Remaining.IsValidIndex(Node.NodeId-1)) return false;
        const uint8 Amount=Remaining[Node.NodeId-1];
        if (!Amount) continue;
        FCircle Circle;
        if (!Shapes.BuildNode(Node,Amount,Version,Circle)) return false;
        Members.Add(Circle);
    }
    TArray<FProtectedPassage> Protected(WorkEntrances);
    // Preserve actual gaps wide enough for the smallest configured unit. Only
    // this cluster's members are inspected; no unrelated clusters or actors.
    // Extra conservative points may reduce merging, but never close a passage.
    if (MinimumPassageRadius>0) for (int32 I=0;I<Members.Num();++I) for (int32 J=I+1;J<Members.Num();++J)
    {
        const auto& A=Members[I];const auto& B=Members[J];
        const FVector D=(B.Center-A.Center).GetSafeNormal2D();
        const double Gap=FVector::Dist2D(A.Center,B.Center)-A.Radius-B.Radius;
        if (Gap<2.*MinimumPassageRadius) continue;
        const FVector Center=A.Center+D*(A.Radius+Gap*.5);
        bool Clear=true;
        for (const auto& C:Members)
            if (FVector::DistSquared2D(C.Center,Center)<FMath::Square(double(C.Radius)+MinimumPassageRadius))
            {Clear=false;break;}
        if (Clear)
        {
            auto& P=Protected.AddDefaulted_GetRef();P.From=P.To=Center;P.Radius=MinimumPassageRadius;
            P.MinimumZ=FMath::Min(A.MinimumZ,B.MinimumZ);P.MaximumZ=FMath::Max(A.MaximumZ,B.MaximumZ);
        }
    }
    TArray<FOreAggregate> Aggregates;
    BuildOreCluster(Members,Protected,MaximumRadiusRatio,MaximumAreaRatio,Aggregates,Counters);
    Out.Reserve(Aggregates.Num());
    for (const auto& Group:Aggregates) Out.Add(Group.Circle);
    return true;
}
}
