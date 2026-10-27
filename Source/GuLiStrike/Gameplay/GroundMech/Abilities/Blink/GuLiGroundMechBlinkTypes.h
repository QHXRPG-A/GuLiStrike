#pragma once

#include "CoreMinimal.h"
#include "GuLiGroundMechBlinkTypes.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGuLiBlinkStateChanged, uint8, Phase, bool, bCoolingDown);

/** Fixed-step ability state; snapshots preserve cooldown and never replay accepted input. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiGroundMechBlinkState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) uint8 Phase=0; // 0 ready/cooldown, 1 windup, 2 concealed travel, 3 recovery
    UPROPERTY(BlueprintReadOnly) int32 Serial=0;
    UPROPERTY() uint32 LastRequest=0;
    UPROPERTY(BlueprintReadOnly) float Elapsed=0.f;
    UPROPERTY(BlueprintReadOnly) float Cooldown=0.f;
    UPROPERTY() FVector Start=FVector::ZeroVector;
    UPROPERTY() FVector End=FVector::ZeroVector;
    UPROPERTY() FVector Direction=FVector::ForwardVector;
    UPROPERTY() float Yaw=0.f;
};
