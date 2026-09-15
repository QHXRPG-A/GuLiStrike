#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiComponentSkillQALibrary.generated.h"

class APlayerController;
class UGuLiCommanderSkillComponent;
class UGuLiShipBuildComponent;

/** Editor-only adapters exercise native RPC callspace from Python acceptance scripts. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiComponentSkillQALibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Skills") static bool StartPIE(int32 Mode, int32 Clients);
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Skills") static bool SelectRadius(APlayerController* Controller, FVector Center, int32 RequestId, bool bAdd);
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Skills") static bool SubmitSkill(UGuLiCommanderSkillComponent* Component, FGuid RequestId, FName GlobalSkillId, int64 SelectionRevision, bool bHasPoint, FVector Point);
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Skills") static void PressQ(UGuLiCommanderSkillComponent* Component, bool bHasPoint, FVector Point);
	UFUNCTION(BlueprintPure, Category="GuLiStrike|Editor|Skills") static FString SelectionSnapshot(APlayerController* Controller);
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Skills") static bool CommitShipChoice(UGuLiShipBuildComponent* Component, FName NodeId, FString& Error);
	UFUNCTION(BlueprintPure, Category="GuLiStrike|Editor|Skills") static FString WingmanSnapshot(UObject* WorldContext);
};
