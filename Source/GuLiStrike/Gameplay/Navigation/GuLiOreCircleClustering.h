#pragma once

#include "Gameplay/Navigation/GuLiCircleAvoidance.h"

namespace GuLiCircleAvoidance
{
struct FProtectedPassage
{
    FVector From = FVector::ZeroVector;
    FVector To = FVector::ZeroVector;
    float Radius = 0;
    float MinimumZ = -FLT_MAX;
    float MaximumZ = FLT_MAX;
};

struct FOreAggregate
{
    FCircle Circle;
    TArray<uint64,TInlineAllocator<8>> Members;
    double MemberRadiusSquaredSum = 0;
    float MaximumMemberRadius = 0;
};

struct FClusteringCounters
{
    uint32 MemberPairChecks = 0;
    uint32 Merges = 0;
    uint32 SizeRejections = 0;
    uint32 PassageRejections = 0;
};

// Input is exactly one original ore cluster, with its CURRENT stage radii.
// Empty/depleted nodes are omitted by the caller. No world/actor traversal.
void BuildOreCluster(TConstArrayView<FCircle> Members,
    TConstArrayView<FProtectedPassage> Passages, float MaximumRadiusRatio,
    float MaximumAreaRatio, TArray<FOreAggregate>& Out, FClusteringCounters& Counters);
}
