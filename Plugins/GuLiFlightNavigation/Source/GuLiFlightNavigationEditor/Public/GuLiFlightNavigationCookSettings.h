#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GuLiFlightNavigationCookSettings.generated.h"

/** Project-owned list of maps that must contain fresh, valid Flight Navigation data before cook. */
UCLASS(Config = Game, DefaultConfig)
class GULIFLIGHTNAVIGATIONEDITOR_API UGuLiFlightNavigationCookSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Long package names such as /Game/Maps/LVL_Main. */
	UPROPERTY(Config, EditAnywhere, Category = "Flight Navigation Cook Gate")
	TArray<FName> RequiredWorldPackages;
};
