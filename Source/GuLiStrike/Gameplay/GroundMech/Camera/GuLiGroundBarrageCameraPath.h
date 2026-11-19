#pragma once

#include "CoreMinimal.h"
#include "Math/InterpCurve.h"

struct FGuLiStrikeRogueCardsCardsRow;

struct FGuLiGroundBarrageCameraShot
{
    FVector EyeFromFeet = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
    float Fov = 90.f, Weight = 0.f, Flash = 0.f;
};

/** A local camera orbit sampled on the same clock as the actual cast. */
class GULISTRIKE_API FGuLiGroundBarrageCameraPath
{
public:
    bool Load(const FGuLiStrikeRogueCardsCardsRow& Card);
    bool Sample(float Seconds, FGuLiGroundBarrageCameraShot& Out) const;
    float ChargeArcScale(float Seconds) const;
    float Duration = 0.f;
private:
    FInterpCurveVector Orbit, Focus, Lens;
    FInterpCurveVector2D Effects;
};
