#pragma once

#include "CoreMinimal.h"
#include "GuLiUnitMovementLayer.generated.h"

/** Commander ground-navigation domains. Ship and Wingman flight do not use this enum. */
UENUM(BlueprintType)
enum class EGuLiUnitMovementLayer : uint8
{
    Ground = 0,
    LowAir = 1
};

class AActor;
class UWorld;
struct FCollisionQueryParams;

namespace GuLiUnitMovement
{
GULISTRIKE_API TOptional<EGuLiUnitMovementLayer> LayerForActor(const AActor* Actor);
/** Exclude only registered ground/low-air bodies; static world and flight actors retain their responses. */
GULISTRIKE_API void IgnoreOtherLayerBodies(UWorld& World,FCollisionQueryParams& Params,
    const FVector& Center,float Radius,EGuLiUnitMovementLayer Layer);
inline bool IsValid(EGuLiUnitMovementLayer Layer, float AltitudeCm)
{
    return FMath::IsFinite(AltitudeCm)
        && (Layer == EGuLiUnitMovementLayer::Ground ? AltitudeCm == 0.f
            : Layer == EGuLiUnitMovementLayer::LowAir && AltitudeCm >= 3000.f && AltitudeCm <= 9900.f);
}
inline FVector GroundPosition(const FVector& WorldPosition, float AltitudeCm)
{ return WorldPosition - FVector(0, 0, AltitudeCm); }
inline FVector WorldPosition(const FVector& GroundPosition, float AltitudeCm)
{ return GroundPosition + FVector(0, 0, AltitudeCm); }
}
