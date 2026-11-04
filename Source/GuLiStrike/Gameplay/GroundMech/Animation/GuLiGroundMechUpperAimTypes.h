#pragma once
#include "CoreMinimal.h"
#include "GuLiGroundMechUpperAimTypes.generated.h"

/** World-space turret motor. Authority snapshots and display prediction use the same response. */
USTRUCT()
struct GULISTRIKE_API FGuLiGroundMechUpperAimState
{
    GENERATED_BODY()
    UPROPERTY() float WorldYaw=0.f;
    UPROPERTY() float TargetYaw=0.f;
    UPROPERTY() FVector TargetPoint=FVector::ZeroVector;
    UPROPERTY() bool bInitialized=false;
    UPROPERTY() bool bHasTarget=false;
    UPROPERTY() bool bHasAimPose=false;
    UPROPERTY() uint32 Revision=0;

    void Reset(float Yaw);
    void Stop();
    void Advance(float DeltaSeconds,const FVector& Origin,const FVector& Point,bool bValid,
                 float ResponseSeconds,float SnapToleranceDegrees);
    FVector GetDampedTarget(const FVector& Origin) const;
};
