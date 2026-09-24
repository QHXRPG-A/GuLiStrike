// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"

#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiObstacles, Log, All);

namespace
{
	bool IsValidObstacle(const FGuLiDynamicObstacle& Obstacle)
	{
		return !Obstacle.Location.ContainsNaN()
			&& FMath::IsFinite(Obstacle.RadiusCentimeters)
			&& Obstacle.RadiusCentimeters > 0.0f;
	}
}

bool UGuLiDynamicObstacleRegistrySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

void UGuLiDynamicObstacleRegistrySubsystem::Deinitialize()
{
	ObstaclesChanged.Clear();
	StaticRegionChanged.Clear(); RegionChanges.Reset();
	Obstacles.Reset();
	IndexByHandle.Reset();
	NextHandle = 1u;
	Revision = 0u;
	Super::Deinitialize();
}

FGuLiDynamicObstacleHandle UGuLiDynamicObstacleRegistrySubsystem::RegisterObstacle(
	const FVector& Location,
	const float RadiusCentimeters)
{
	FGuLiDynamicObstacle Obstacle;
	Obstacle.Location = Location;
	Obstacle.RadiusCentimeters = RadiusCentimeters;
	return RegisterObstacle(Obstacle);
}

FGuLiDynamicObstacleHandle UGuLiDynamicObstacleRegistrySubsystem::RegisterObstacle(
	const FGuLiDynamicObstacle& Obstacle)
{
	if (!IsValidObstacle(Obstacle))
	{
		UE_LOG(LogGuLiObstacles, Verbose, TEXT("Rejected invalid obstacle registration."));
		return {};
	}
	checkf(NextHandle < 0x80000000u, TEXT("Dynamic obstacle handle space exhausted."));
	const FGuLiDynamicObstacleHandle Handle{NextHandle++};
	FGuLiDynamicObstacle Stored = Obstacle;
	Stored.Handle = Handle;
	const int32 Index = Obstacles.Add(MoveTemp(Stored));
	IndexByHandle.Add(Handle.Value, Index);
	PublishStaticRegion(Obstacle);
	PublishChange();
	return Handle;
}

EGuLiObstacleUpdateResult UGuLiDynamicObstacleRegistrySubsystem::UpdateObstacle(
	const FGuLiDynamicObstacleHandle Handle,
	const FGuLiDynamicObstacle& Obstacle)
{
	if (!IsValidObstacle(Obstacle))
	{
		UE_LOG(LogGuLiObstacles, Verbose, TEXT("Rejected invalid update for obstacle %u; retaining last valid data."), Handle.Value);
		return EGuLiObstacleUpdateResult::InvalidData;
	}
	const int32* Index = IndexByHandle.Find(Handle.Value);
	if (!Index) return EGuLiObstacleUpdateResult::NotFound;
	checkSlow(Obstacles.IsValidIndex(*Index));
	FGuLiDynamicObstacle& Stored = Obstacles[*Index];
	const bool bChanged = !Stored.Location.Equals(Obstacle.Location, 0.1f)
		|| !FMath::IsNearlyEqual(Stored.RadiusCentimeters, Obstacle.RadiusCentimeters, 0.1f)
		|| Stored.Kind != Obstacle.Kind
		|| Stored.Team != Obstacle.Team;
	if (!bChanged) return EGuLiObstacleUpdateResult::Unchanged;
	PublishStaticRegion(Stored);
	Stored.Location = Obstacle.Location;
	Stored.RadiusCentimeters = Obstacle.RadiusCentimeters;
	Stored.Kind = Obstacle.Kind;
	Stored.Team = Obstacle.Team;
	PublishStaticRegion(Stored);
	PublishChange();
	return EGuLiObstacleUpdateResult::Updated;
}

void UGuLiDynamicObstacleRegistrySubsystem::UnregisterObstacle(
	const FGuLiDynamicObstacleHandle Handle)
{
	check(Handle.IsValid());
	const int32 RemovedIndex = IndexByHandle.FindAndRemoveChecked(Handle.Value);
	PublishStaticRegion(Obstacles[RemovedIndex]);
	const int32 LastIndex = Obstacles.Num() - 1;
	if (RemovedIndex != LastIndex)
	{
		const uint32 MovedHandle = Obstacles[LastIndex].Handle.Value;
		Obstacles.RemoveAtSwap(RemovedIndex, 1, EAllowShrinking::No);
		IndexByHandle.FindChecked(MovedHandle) = RemovedIndex;
	}
	else
	{
		Obstacles.RemoveAt(LastIndex, 1, EAllowShrinking::No);
	}
	PublishChange();
}

void UGuLiDynamicObstacleRegistrySubsystem::PublishChange()
{
	Revision = Revision == MAX_uint32 ? 1u : Revision + 1u;
	ObstaclesChanged.Broadcast(Revision);
}

void UGuLiDynamicObstacleRegistrySubsystem::PublishStaticRegion(const FGuLiDynamicObstacle& Obstacle)
{
	if (Obstacle.Kind != EGuLiDynamicObstacleKind::StaticWorld) return;
	const FBox Bounds = FBox::BuildAABB(Obstacle.Location,FVector(Obstacle.RadiusCentimeters,Obstacle.RadiusCentimeters,100000));
	NotifyStaticGeometryChanged(Bounds);
}
void UGuLiDynamicObstacleRegistrySubsystem::NotifyStaticGeometryChanged(const FBox& Bounds)
{
	if (!Bounds.IsValid) return;
	RegionChanges.Add({++StaticRevision,Bounds});
	if (RegionChanges.Num()>128) RegionChanges.RemoveAt(0,1,EAllowShrinking::No);
	StaticRegionChanged.Broadcast(Bounds);
}
bool UGuLiDynamicObstacleRegistrySubsystem::HasStaticChangesSince(uint32 Since, const FBox& Bounds) const
{
	if (Since==StaticRevision) return false;
	if (RegionChanges.IsEmpty() || Since+1<RegionChanges[0].Revision) return true;
	for (const auto& Change : RegionChanges) if (Change.Revision>Since && Change.Bounds.Intersect(Bounds)) return true;
	return false;
}
