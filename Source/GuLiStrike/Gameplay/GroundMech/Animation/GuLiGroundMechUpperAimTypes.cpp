#include "Gameplay/GroundMech/Animation/GuLiGroundMechUpperAimTypes.h"

void FGuLiGroundMechUpperAimState::Reset(float Yaw)
{
    WorldYaw=TargetYaw=FRotator::NormalizeAxis(Yaw);
    TargetPoint=FVector::ZeroVector; bInitialized=true; bHasTarget=bHasAimPose=false; ++Revision;
}

void FGuLiGroundMechUpperAimState::Stop()
{
    // Retain the last solved aiming pose while input is unavailable. Stopping
    // the motor must not drop the barrels back to the locomotion animation.
    bHasTarget=false;
}

void FGuLiGroundMechUpperAimState::Advance(float DeltaSeconds,const FVector& Origin,const FVector& Point,
    bool bValid,float ResponseSeconds,float SnapToleranceDegrees)
{
    if (!bInitialized) return;
    if (!bValid || Point.ContainsNaN() || Origin.ContainsNaN()) { Stop(); return; }
    bHasTarget=bHasAimPose=true; TargetPoint=Point;
    const FVector Offset=Point-Origin;
    if (Offset.SizeSquared2D()>UE_SMALL_NUMBER) TargetYaw=Offset.Rotation().Yaw;
    const float Error=FMath::FindDeltaAngleDegrees(WorldYaw,TargetYaw);
    // T90 response, independent of frame rate. No spring velocity or overshoot.
    const float Alpha=1.f-FMath::Exp(-FMath::Loge(10.f)*FMath::Max(0.f,DeltaSeconds)/ResponseSeconds);
    WorldYaw=FRotator::NormalizeAxis(WorldYaw+Error*Alpha);
    if (FMath::Abs(FMath::FindDeltaAngleDegrees(WorldYaw,TargetYaw))<=SnapToleranceDegrees)
        WorldYaw=TargetYaw;
}

FVector FGuLiGroundMechUpperAimState::GetDampedTarget(const FVector& Origin) const
{
    if (!bHasAimPose) return FVector::ZeroVector;
    // Rotate only the horizontal bearing; preserve target height and radial distance.
    return Origin+FRotator(0,FMath::FindDeltaAngleDegrees(TargetYaw,WorldYaw),0).RotateVector(TargetPoint-Origin);
}
