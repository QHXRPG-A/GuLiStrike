#pragma once
#include "Components/Button.h"
#include "GuLiCommanderTaskButton.generated.h"

/** Mouse-only HUD actions must not steal commander keyboard focus. */
UCLASS()
class UGuLiCommanderTaskButton final : public UButton
{
	GENERATED_BODY()
public:
	UGuLiCommanderTaskButton();
};
