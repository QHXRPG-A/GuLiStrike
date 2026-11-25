#pragma once

#include "CoreMinimal.h"
class AGuLiStrikeShip;
class UStaticMeshComponent;
class UWorld;

// The live physics body's transform may include network smoothing. Queries map the
// simulated ship-relative segment into that body without moving the visible mesh.
namespace GuLiWingmanFlightCollision
{
struct FHull
{
    TWeakObjectPtr<AGuLiStrikeShip> Ship;
    TWeakObjectPtr<UStaticMeshComponent> Mesh;
    FTransform Previous, Current, Query;
    FVector Velocity = FVector::ZeroVector;
    FVector AngularAxis = FVector::UpVector;
    double AngularSpeed = 0;
    double Reach = 0;
};
void SampleHulls(UWorld& World, float Step, TArray<FHull>& Hulls, bool bRefreshShipList = false);
bool SweepTerrain(UWorld& World, const FVector& From, const FVector& To, float Radius, FHitResult& Hit);
bool OverlapsTerrain(UWorld& World, const FVector& Point, float Radius);
bool HullContact(const FHull& Hull, const FVector& From, const FVector& To, float Radius, float StartTime,
                 float EndTime, FHitResult& Hit, FVector& EndPlane);
bool ClearPosition(UWorld& World, const TArray<FHull>& Hulls, const FVector& Point, float Radius);
}
