#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiPioneerQALibrary.generated.h"

/** Explicitly invoked PIE acceptance adapters. No automatic execution or saved gameplay state. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiPioneerQALibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Pioneer")
	static bool Observe(APlayerController* Controller, bool bEnabled);
	UFUNCTION(BlueprintPure, Category="GuLiStrike|Editor|Pioneer")
	static FString Snapshot(APlayerController* Controller);
	/** Selection uses the real point/radius RPC and the player's normal sequence allocation. */
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Pioneer")
	static bool Select(APlayerController* Controller, int64 SoldierId, bool bAdd = false);
	/** Skill/Move/Stop enter the existing HUD/controller path. QDown/QUp enter key input. */
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Pioneer")
	static bool Action(APlayerController* Controller, FName Command, FVector Point = FVector::ZeroVector);
	/** Replay the last self-targeted unit request with its original identity and selection revision. */
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Pioneer")
	static bool ReplayLastQ(APlayerController* Controller);
};
