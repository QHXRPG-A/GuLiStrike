#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"
#include "Gameplay/Performance/GuLiPerformanceSubsystem.h"
#include "Engine/World.h"

bool UGuLiDynamicObstacleRegistrySubsystem::ReplaceCircleGroup(uint64 Source,
    TConstArrayView<GuLiCircleAvoidance::FCircle> Circles,bool bOreChange)
{
    check(IsInGameThread());
    FGuLiPerformanceScope Timing(GetWorld(),TEXT("Movement.GT.StaticIndexUpdateMs"));
    FBox Changed;
    if (!CircularStatic.Replace(Source,Circles,Changed)) return false;
    if (!Changed.IsValid) return true;
    const auto Min=FGuLiDynamicObstacleSnapshot::Cell(Changed.Min),Max=FGuLiDynamicObstacleSnapshot::Cell(Changed.Max);
    const uint64 CellVersion=++NextCellVersion;
    for (int32 X=Min.X;X<=Max.X;++X) for (int32 Y=Min.Y;Y<=Max.Y;++Y)
        CellVersions.Add({X,Y},CellVersion);
    PublishChange();
    NotifyStaticGeometryChanged(Changed);
    if (bOreChange)
    {
        // Revalidate nearby work poses again after asynchronous NavMesh tiles
        // have caught up. The producer publishes the same circles to navigation.
        PendingOreNavigationRegions.FindOrAdd(int32(Source&0xffffffffu),FBox(ForceInit))+=Changed;
        ++OrePublications;
    }
    return true;
}

void UGuLiDynamicObstacleRegistrySubsystem::RemoveCircleGroup(uint64 Source,bool bOreChange)
{ReplaceCircleGroup(Source,TConstArrayView<GuLiCircleAvoidance::FCircle>{},bOreChange);}
