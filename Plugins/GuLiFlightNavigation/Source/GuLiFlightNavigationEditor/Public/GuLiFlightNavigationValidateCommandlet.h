#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "GuLiFlightNavigationValidateCommandlet.generated.h"

/** Read-only pre-cook commandlet. Usage: -run=GuLiFlightNavigationValidate */
UCLASS()
class GULIFLIGHTNAVIGATIONEDITOR_API UGuLiFlightNavigationValidateCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UGuLiFlightNavigationValidateCommandlet();

	virtual int32 Main(const FString& Params) override;
};
