#pragma once
#include "Gameplay/Wingman/GuLiWingmanRuntimeTypes.h"
#include "Gameplay/Wingman/Movement/GuLiWingmanFlightCollision.h"

namespace GuLiWingmanPredictiveFlight
{
struct FPose
{
    FVector Position, Forward;
    float Speed;
};
void Integrate(FPose& Pose, const FVector& Desired, float DesiredSpeed,
               const FGuLiWingmanFormationRuntimeConfig& Tuning, float Dt);
void Choose(UWorld& World, const TArray<GuLiWingmanFlightCollision::FHull>& Hulls, FGuLiWingmanRuntimeState& State,
            const FPose& Pose, const FVector& Requested, float SpeedLimit, float Dt);
}
