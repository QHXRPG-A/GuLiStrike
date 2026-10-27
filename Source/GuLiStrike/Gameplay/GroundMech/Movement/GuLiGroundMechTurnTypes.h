#pragma once

#include "CoreMinimal.h"
#include "GuLiGroundMechTurnTypes.generated.h"

/** Authoritative fixed-step turn state. Montage evaluation never applies root motion to physics. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiGroundMechTurnState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 Serial=0;
    UPROPERTY(BlueprintReadOnly) int32 Direction=0;
    UPROPERTY(BlueprintReadOnly) float Time=0.f;
    UPROPERTY(BlueprintReadOnly) float Duration=0.f;
    UPROPERTY(BlueprintReadOnly) float StartYaw=0.f;
    UPROPERTY(BlueprintReadOnly) float TargetDegrees=0.f;
    UPROPERTY(BlueprintReadOnly) float Progress=0.f;
};
