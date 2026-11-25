#include "Gameplay/Wingman/Movement/GuLiWingmanFlightCollision.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Gameplay/Wingman/GuLiWingmanSimulationSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/BodyInstance.h"

namespace GuLiWingmanFlightCollision
{
static FCollisionResponseParams TerrainResponses()
{
    FCollisionResponseParams Responses(ECR_Ignore);
    Responses.CollisionResponse.SetResponse(ECC_WorldStatic, ECR_Block);
    return Responses;
}

bool SweepTerrain(UWorld& World, const FVector& From, const FVector& To, float Radius, FHitResult& Hit)
{
    return World.SweepSingleByChannel(Hit, From, To, FQuat::Identity, ECC_GameTraceChannel1,
                                      FCollisionShape::MakeSphere(Radius),
                                      FCollisionQueryParams(SCENE_QUERY_STAT(WingmanTerrain)), TerrainResponses());
}

bool OverlapsTerrain(UWorld& World, const FVector& Point, float Radius)
{
    return World.OverlapBlockingTestByChannel(
        Point, FQuat::Identity, ECC_GameTraceChannel1, FCollisionShape::MakeSphere(Radius),
        FCollisionQueryParams(SCENE_QUERY_STAT(WingmanClearance)), TerrainResponses());
}

void SampleHulls(UWorld& World, float Step, TArray<FHull>& Hulls, bool bRefreshShipList)
{
    int32 Used = 0;
    auto SampleShip = [&](AGuLiStrikeShip* Ship) {
        if (!IsValid(Ship))
            return;
        UStaticMeshComponent* Mesh = Ship->GetHullMeshComponent();
        if (!Mesh || !Mesh->GetStaticMesh() || !Mesh->GetBodyInstance())
            return;

        int32 Index = Used;
        while (Index < Hulls.Num() && Hulls[Index].Ship != Ship)
            ++Index;
        const bool bNew = Index == Hulls.Num();
        if (bNew)
            Hulls.AddDefaulted();
        if (Used != Index)
            Hulls.Swap(Used, Index);
        FHull& Hull = Hulls[Used++];
        Hull.Previous = Hull.Current;
        Hull.Ship = Ship;
        Hull.Mesh = Mesh;
        Hull.Query = Mesh->GetComponentTransform();
        const FTransform MeshBase(Ship->GetBaseRotationOffset(), Ship->GetBaseTranslationOffset(),
                                  Ship->GetMesh()->GetRelativeScale3D());
        Hull.Current = Mesh->GetRelativeTransform() * MeshBase * Ship->GetActorTransform();
        if (bNew)
        {
            Hull.Previous = Hull.Current;
            Hull.Previous.AddToTranslation(-Ship->GetVelocity() * Step);
        }
        Hull.Velocity = (Hull.Current.GetLocation() - Hull.Previous.GetLocation()) / Step;
        FQuat Delta = Hull.Current.GetRotation() * Hull.Previous.GetRotation().Inverse();
        if (Delta.W < 0)
            Delta = Delta * -1.0;
        double Angle;
        Delta.GetNormalized().ToAxisAndAngle(Hull.AngularAxis, Angle);
        Hull.AngularSpeed = Angle / Step;
        Hull.Reach = Mesh->Bounds.SphereRadius + FVector::Distance(Mesh->Bounds.Origin, Hull.Query.GetLocation());
    };
    if (auto* Simulation = World.GetSubsystem<UGuLiWingmanSimulationSubsystem>())
    {
        for (const auto& Ship : Simulation->GetFlightObstacleShips(bRefreshShipList))
            SampleShip(Ship.Get());
    }
    else
    {
        // Editor clearance queries also work before a gameplay subsystem exists.
        for (TActorIterator<AGuLiStrikeShip> It(&World); It; ++It)
            SampleShip(*It);
    }
    Hulls.SetNum(Used, EAllowShrinking::No);
}

static FTransform PoseAt(const FHull& Hull, float Time)
{
    FTransform Pose = Hull.Current;
    Pose.AddToTranslation(Hull.Velocity * Time);
    if (Hull.AngularSpeed > UE_SMALL_NUMBER)
        Pose.SetRotation((FQuat(Hull.AngularAxis, Hull.AngularSpeed * Time) * Pose.GetRotation()).GetNormalized());
    return Pose;
}

bool HullContact(const FHull& Hull, const FVector& From, const FVector& To, float Radius, float StartTime,
                 float EndTime, FHitResult& Hit, FVector& EndPlane)
{
    const auto* Mesh = Hull.Mesh.Get();
    const FBodyInstance* Body = Mesh ? Mesh->GetBodyInstance() : nullptr;
    if (!Body)
        return false;
    const FTransform EndPose = PoseAt(Hull, EndTime);
    const FVector ShipStart = Hull.Current.GetLocation() + Hull.Velocity * StartTime;
    const FVector ShipEnd = EndPose.GetLocation();
    if (FMath::PointDistToSegmentSquared(FVector::ZeroVector, From - ShipStart, To - ShipEnd) >
        FMath::Square(Hull.Reach + Radius))
        return false;
    const float Span = EndTime - StartTime;
    const int32 Segments =
        FMath::Clamp(FMath::CeilToInt(Hull.AngularSpeed * Span / FMath::DegreesToRadians(6.0)), 1, 96);
    const float RotationError =
        (Hull.Reach + Radius) * (1.0 - FMath::Cos(Hull.AngularSpeed * Span / (2.0 * Segments)));
    const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius + RotationError);
    for (int32 Index = 0; Index < Segments; ++Index)
    {
        const double Alpha0 = double(Index) / Segments, Alpha1 = double(Index + 1) / Segments;
        const FTransform Pose0 = PoseAt(Hull, StartTime + Span * Alpha0),
                         Pose1 = PoseAt(Hull, StartTime + Span * Alpha1);
        const FVector QueryStart =
            Hull.Query.TransformPosition(Pose0.InverseTransformPosition(FMath::Lerp(From, To, Alpha0)));
        const FVector QueryEnd =
            Hull.Query.TransformPosition(Pose1.InverseTransformPosition(FMath::Lerp(From, To, Alpha1)));
        FMTDResult Penetration;
        FHitResult BodyHit;
        const bool bOverlapping = Body->OverlapTest(QueryStart, FQuat::Identity, Shape, &Penetration);
        if (!bOverlapping && (QueryStart.Equals(QueryEnd) ||
                              !Body->Sweep(BodyHit, QueryStart, QueryEnd, FQuat::Identity, Shape, false)))
            continue;
        const FVector Normal = bOverlapping ? FVector(Penetration.Direction) : FVector(BodyHit.Normal);
        const FVector Center = bOverlapping ? QueryStart + Normal * Penetration.Distance
                                            : FMath::Lerp(QueryStart, QueryEnd, BodyHit.Time);
        EndPlane = EndPose.TransformPosition(Hull.Query.InverseTransformPosition(Center));
        Hit.bBlockingHit = true;
        Hit.bStartPenetrating = bOverlapping;
        Hit.Time = (Index + (bOverlapping ? 0.0 : BodyHit.Time)) / Segments;
        Hit.Normal =
            EndPose.TransformVectorNoScale(Hull.Query.InverseTransformVectorNoScale(Normal)).GetSafeNormal();
        if (Hit.Normal.IsNearlyZero())
            Hit.Normal = (From - ShipStart).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        return true;
    }
    return false;
}

bool ClearPosition(UWorld& World, const TArray<FHull>& Hulls, const FVector& Point, float Radius)
{
    if (OverlapsTerrain(World, Point, Radius))
        return false;
    for (const FHull& Hull : Hulls)
    {
        FHitResult Hit;
        FVector Plane;
        if (HullContact(Hull, Point, Point, Radius, 0, 0, Hit, Plane))
            return false;
    }
    return true;
}
}
