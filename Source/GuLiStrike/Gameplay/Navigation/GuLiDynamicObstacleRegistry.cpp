// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"

#include "Engine/World.h"

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
	check(IsValidObstacle(Obstacle));
	checkf(NextHandle < 0x80000000u, TEXT("Dynamic obstacle handle space exhausted."));
	const FGuLiDynamicObstacleHandle Handle{NextHandle++};
	FGuLiDynamicObstacle Stored = Obstacle;
	Stored.Handle = Handle;
	const int32 Index = Obstacles.Add(MoveTemp(Stored));
	IndexByHandle.Add(Handle.Value, Index);
	PublishChange();
	return Handle;
}

bool UGuLiDynamicObstacleRegistrySubsystem::UpdateObstacle(
	const FGuLiDynamicObstacleHandle Handle,
	const FGuLiDynamicObstacle& Obstacle)
{
	if (!Handle.IsValid() || !IsValidObstacle(Obstacle)) return false;
	const int32* Index = IndexByHandle.Find(Handle.Value);
	if (!Index || !Obstacles.IsValidIndex(*Index)) return false;
	FGuLiDynamicObstacle& Stored = Obstacles[*Index];
	const bool bChanged = !Stored.Location.Equals(Obstacle.Location, 0.1f)
		|| !FMath::IsNearlyEqual(Stored.RadiusCentimeters, Obstacle.RadiusCentimeters, 0.1f)
		|| Stored.Kind != Obstacle.Kind
		|| Stored.Team != Obstacle.Team;
	if (!bChanged) return true;
	Stored.Location = Obstacle.Location;
	Stored.RadiusCentimeters = Obstacle.RadiusCentimeters;
	Stored.Kind = Obstacle.Kind;
	Stored.Team = Obstacle.Team;
	PublishChange();
	return true;
}

void UGuLiDynamicObstacleRegistrySubsystem::UnregisterObstacle(
	const FGuLiDynamicObstacleHandle Handle)
{
	check(Handle.IsValid());
	const int32 RemovedIndex = IndexByHandle.FindAndRemoveChecked(Handle.Value);
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
