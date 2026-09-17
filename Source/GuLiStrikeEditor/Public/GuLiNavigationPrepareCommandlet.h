#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "GuLiNavigationPrepareCommandlet.generated.h"

UCLASS()
class UGuLiNavigationPrepareCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UGuLiNavigationPrepareCommandlet();
	virtual int32 Main(const FString& Params) override;
};
