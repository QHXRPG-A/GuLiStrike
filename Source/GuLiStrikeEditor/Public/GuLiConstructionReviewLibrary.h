#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiConstructionReviewLibrary.generated.h"

class UGuLiBuildingLifecycleComponent;

/** Construction review operations are restricted to an authoritative PIE world. Never saved into maps. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiConstructionReviewLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Construction") static bool FundReview(UWorld* World);
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Construction") static AActor* SpawnSample(UWorld* World, int32 DefinitionId, FVector Ground, float Yaw, bool bCompleted = false);
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Construction") static bool AdvanceSample(UGuLiBuildingLifecycleComponent* Lifecycle, float Progress);
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Construction") static bool JoinReviewClient();
};
