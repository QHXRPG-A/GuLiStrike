#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiRogueCardQALibrary.generated.h"

/** Editor-only adapters for the explicitly requested card/cleanup/performance acceptance. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiRogueCardQALibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|Editor|RogueCards")
	static bool Action(APlayerController* Controller,FName Command,int32 Index=0);
	UFUNCTION(BlueprintPure,Category="GuLiStrike|Editor|RogueCards")
	static FString Snapshot(APlayerController* Controller);
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|Editor|RogueCards")
	static void Replay(APlayerController* Controller,FGuid Session,const FString& CardId,bool bReady);
	UFUNCTION(BlueprintPure,Category="GuLiStrike|Editor|RogueCards")
	static FString FrameTimings(APlayerController* Controller);
	/** Transient PIE-only real Mass units for the requested 100/500/1000 comparison. */
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|Editor|RogueCards")
	static FString BuildFixture(APlayerController* Controller,int32 Count,FVector Center,float Spacing=400.f,bool bCompactLayout=false);
	UFUNCTION(BlueprintPure,Category="GuLiStrike|Editor|RogueCards")
	static FString UpgradeSlots(APlayerController* Controller);
	/** Manual PIE preparation: performance populations or the compact WM01 Q guidance cases. */
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|Editor|RogueCards")
	static FString BuildMissileFixture(APlayerController* Controller,int32 Count,int32 ProjectilesPerSalvo,FVector Center);
	UFUNCTION(BlueprintCallable,Category="GuLiStrike|Editor|RogueCards")
	static FString FireMissileFixture(APlayerController* Controller);
	UFUNCTION(BlueprintPure,Category="GuLiStrike|Editor|RogueCards")
	static FString MissileMetrics(APlayerController* Controller);
};
