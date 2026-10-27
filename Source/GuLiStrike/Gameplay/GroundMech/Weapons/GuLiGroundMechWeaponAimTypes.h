#pragma once

#include "CoreMinimal.h"
#include "GuLiGroundMechWeaponAimTypes.generated.h"

/** Fixed-step return state; replicas and restored owners use the same component-space start poses. */
USTRUCT()
struct GULISTRIKE_API FGuLiGroundMechWeaponAimState
{
    GENERATED_BODY()
    UPROPERTY() bool bForward=false;
    UPROPERTY() float ReturnAlpha=1.f;
    UPROPERTY() FQuat RightStart=FQuat::Identity;
    UPROPERTY() FQuat LeftStart=FQuat::Identity;
};
