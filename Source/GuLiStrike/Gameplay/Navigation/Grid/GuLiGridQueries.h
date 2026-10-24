#pragma once
#include "Gameplay/Navigation/Grid/GuLiGroundGridSubsystem.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
struct FGuLiHybridObjectState;

namespace GuLiGrid
{
GULISTRIKE_API TSharedPtr<const FGuLiGridSnapshot,ESPMode::ThreadSafe> AuthoredSnapshot(UWorld& World);
GULISTRIKE_API void ObserveRemoteBody(UWorld& World,const FGuLiHybridObjectState& Object,const FVector& Position,int32 Size,bool bGround);
GULISTRIKE_API void ObserveLocalBody(UWorld& World,const FGuLiHybridObjectKey& Key,const FVector& Position,int32 Size);
/** A committed displacement is still physically phased until its endpoint has acknowledged application. */
GULISTRIKE_API bool HoldsDisplacement(const FGuLiHybridObjectState& Object);
GULISTRIKE_API bool PendingDisplacementAt(const FGuLiHybridObjectState& Object,const FGuLiGridSnapshot& Grid,
    const FRect& Rect,int32 Size);
inline bool ProjectBody(UWorld& World,const FVector& Point,int32 Size,const FVector& Extent,FNavLocation& Out,const ANavigationData* Legacy=nullptr)
{
    if (const auto S=AuthoredSnapshot(World))
    {
        FVector P;
        if (!S->Project(Point,Size,Extent,P)) return false;
        auto* N=FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
        if (!N || !N->ProjectPointToNavigation(P,Out,FVector(.1,.1,Extent.Z),Legacy)
            || !Out.NodeRef || FVector::DistSquared2D(P,Out.Location)>.01) return false;
        Out.Location.X=P.X;Out.Location.Y=P.Y;return true;
    }
    auto* N=FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
    return N && N->ProjectPointToNavigation(Point,Out,Extent,Legacy);
}
inline bool Unoccupied(UGuLiGroundGridSubsystem& G,const FVector& Point,int32 Size,const FGuLiHybridObjectKey& Ignore={})
{
    const auto S=G.Snapshot();if (!S) return false;
    const auto A=S->AnchorAt(Point,Size);if (!S->Fits(A,Size)) return false;
    TArray<FGuLiHybridObjectKey> Hits;G.Traffic().Query(FRect::At(A,Size),Ignore,Hits);return Hits.IsEmpty();
}
}
