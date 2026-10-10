#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiComponentSkillQALibrary.generated.h"

class APlayerController;
class AActor;
class UGuLiCommanderSkillComponent;
class UGuLiShipBuildComponent;

/** Editor-only adapters exercise native RPC callspace from Python acceptance scripts. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiComponentSkillQALibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Skills") static bool StartPIE(int32 Mode, int32 Clients);
	/** Explicit flight acceptance: Commander, Ground, Air, Commander through real slot allocation. */
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Skills") static bool StartMixedFlightPIE();
 /** Fixed-size two-client dedicated PIE. Mixed uses real Ground/Air seats at initial login. Transient settings only. */
 UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Performance") static bool StartPerformancePIE(bool MixedFlights=false, int32 Width=1280,int32 Height=720);
 UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Performance") static bool SetPerformanceViewportSize(APlayerController* Controller,int32 Width,int32 Height);
 UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Performance") static bool CapturePerformanceViewport(APlayerController* Controller,const FString& Path);
 /** Explicit PIE measurement link. No config/packet/protocol changes; transient drivers only. */
 UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Performance") static FString SetPerformanceBandwidth(UObject* WorldContext,int32 BytesPerSecond=250000);
 /** Explicit PIE fixture only: normal authority spawns/navigation/orders, no persistent data edits. */
 UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Performance") static FString PreparePerformancePopulation(UObject* WorldContext,int32 Population=600,bool Moving=true,FVector CombatCenter=FVector::ZeroVector);
 UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Performance") static FString PerformancePopulationSnapshot(UObject* WorldContext);
 /** 0 stops; +/-1 orders east/west; +/-2 orders a short north/south march for turn review. */
 UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Performance") static bool OrderPerformancePopulation(UObject* WorldContext,int32 Direction=1);
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
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Skills") static bool CommitShipChoice(UGuLiShipBuildComponent* Component, FName NodeId, FString& Error);
	UFUNCTION(BlueprintPure, Category="GuLiStrike|Editor|Skills") static FString WingmanSnapshot(UObject* WorldContext);
};
