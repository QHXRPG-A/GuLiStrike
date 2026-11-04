#pragma once

#include "CoreMinimal.h"

enum class EGuLiGroundMoveSpace : uint8 { World, Body };
enum class EGuLiGroundWeaponAimMode : uint8 { Target, Forward };
struct FGuLiHybridGroundInput
{
    FVector2D Axis=FVector2D::ZeroVector;
    FVector Aim=FVector::ZeroVector;
    bool bSprint=false,bFire=false,bRocket=false,bHasAim=false,bFaceAim=false;
    EGuLiGroundMoveSpace MoveSpace=EGuLiGroundMoveSpace::World;
    EGuLiGroundWeaponAimMode WeaponAimMode=EGuLiGroundWeaponAimMode::Target;
    uint32 BlinkRequest=0;
    // Captured with BlinkRequest; independent of the continuously updated aim heading.
    uint16 BlinkYaw=0;
    bool operator==(const FGuLiHybridGroundInput&) const = default;
};
