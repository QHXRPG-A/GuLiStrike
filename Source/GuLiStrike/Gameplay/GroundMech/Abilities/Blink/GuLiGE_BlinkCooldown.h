#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GuLiGE_BlinkCooldown.generated.h"

/** A tag projection of Hybrid cooldown state. It has no independent world-time timer. */
UCLASS()
class UGuLiGE_BlinkCooldown : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UGuLiGE_BlinkCooldown();
};
