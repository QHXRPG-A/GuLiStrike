#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiComponentSkillQALibrary.generated.h"

class APlayerController;
class AActor;
class UGuLiCommanderSkillComponent;
class UGuLiShipBuildComponent;

/** Editor-only input and readback adapters for local client acceptance scripts. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiComponentSkillQALibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:





 UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Performance") static bool SetPerformanceViewportSize(APlayerController* Controller,int32 Width,int32 Height);
 UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Performance") static bool CapturePerformanceViewport(APlayerController* Controller,const FString& Path);







 /** Transient client presentation only; server mining tasks/resources are untouched. */
 UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Performance") static bool SetPerformanceMiningVisual(AActor* Owner,bool Active,FVector Target);
 /** Explicit transient PIE HUD submission/remount and complete line-clipping readback. */
 UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Performance") static FString SceneUIPerformanceProbe(APlayerController* Controller,const FString& Action,FVector Start,FVector End);

	/** Existing input-lifecycle boundary, used by the ground-player PIE acceptance capture. */
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Input") static void SetGMPanelOpen(APlayerController* Controller, bool bOpen);
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Input") static void FlushPlayerInput(APlayerController* Controller);
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Skills") static bool SelectRadius(APlayerController* Controller, FVector Center, int32 RequestId, bool bAdd);
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Skills") static bool SubmitSkill(UGuLiCommanderSkillComponent* Component, FGuid RequestId, FName GlobalSkillId, int64 SelectionRevision, bool bHasPoint, FVector Point);
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Skills") static void PressQ(UGuLiCommanderSkillComponent* Component, bool bHasPoint, FVector Point);
	UFUNCTION(BlueprintPure, Category="GuLiStrike|Editor|Skills") static FString SelectionSnapshot(APlayerController* Controller);
	/** Explicit PIE preparation only. Preserves health, ownership and combat rules. */
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Preparation", meta=(DevelopmentOnly))
	static bool PrepareSoldierPositions(APlayerController* Controller, const TArray<int64>& Ids, const TArray<FVector>& Positions);
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Skills") static bool CommitShipChoice(UGuLiShipBuildComponent* Component, FName NodeId, FString& Error);
	UFUNCTION(BlueprintPure, Category="GuLiStrike|Editor|Skills") static FString WingmanSnapshot(UObject* WorldContext);
};
