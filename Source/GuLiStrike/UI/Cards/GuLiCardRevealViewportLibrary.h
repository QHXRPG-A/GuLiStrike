#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiCardRevealViewportLibrary.generated.h"

class APlayerController;

/** Read-only desktop viewport state for Blueprint card presentation input. */
UCLASS()
class GULISTRIKE_API UGuLiCardRevealViewportLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Whether this local player's game window is active; UI keyboard focus inside it is allowed. */
	UFUNCTION(BlueprintPure, Category = "GuLiStrike|Cards")
	static bool IsCardRevealViewportFocused(const APlayerController* PlayerController);
};
