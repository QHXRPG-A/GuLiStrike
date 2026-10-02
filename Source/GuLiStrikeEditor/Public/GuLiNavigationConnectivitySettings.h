#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GuLiNavigationConnectivitySettings.generated.h"

/** Maps whose entire CommanderSoldier walkable surface must be mutually reachable. */
UCLASS(Config=Game, DefaultConfig)
class GULISTRIKEEDITOR_API UGuLiNavigationConnectivitySettings final : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, EditAnywhere, Category="GuLi|Navigation")
	TArray<FName> RequiredWorldPackages;
};
