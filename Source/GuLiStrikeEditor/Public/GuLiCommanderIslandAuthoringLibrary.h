#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiCommanderIslandAuthoringLibrary.generated.h"

/** Editor-only dependency checks for the commander island import. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiCommanderIslandAuthoringLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Returns an empty string when required mesh/material and animation contracts pass. */
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Island")
	static FString ValidateOutpostPresentationAssets();
};
