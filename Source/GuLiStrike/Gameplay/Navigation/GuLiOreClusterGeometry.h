#pragma once

#include "Gameplay/Navigation/GuLiCircularBoundaries.h"
#include "Gameplay/Navigation/GuLiOreCircleClustering.h"

struct FGuLiResourceClusterDefinition;
class UGuLiResourceMapDefinition;

namespace GuLiCircleAvoidance
{
/** Pure producer input: no world traversal and no asset access during a batch. */
bool BuildCurrentOreCluster(const FOreShapeCache& Shapes,const UGuLiResourceMapDefinition& Map,
    const FGuLiResourceClusterDefinition& Cluster,TConstArrayView<uint8> Remaining,
    uint64 Version,float MaximumRadiusRatio,float MaximumAreaRatio,float MinimumPassageRadius,
    TConstArrayView<FProtectedPassage> WorkEntrances,TArray<FCircle>& Out,FClusteringCounters& Counters);
}
