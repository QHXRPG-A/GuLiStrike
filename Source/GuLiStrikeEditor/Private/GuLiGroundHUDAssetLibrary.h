#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiGroundHUDAssetLibrary.generated.h"

class UFont;
class UFontFace;
class UGuLiGroundHUDWidget;
class APlayerController;

/** Authoring operations for the Ground HUD's imported presentation assets. */
UCLASS()
class UGuLiGroundHUDAssetLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Configure an existing runtime composite font; empty result means success. */
    UFUNCTION(BlueprintCallable, Category="GuLiStrike|GroundHUD|Editor")
    static FString ConfigureFont(UFont* Font, UFontFace* Medium, UFontFace* Bold);
    UFUNCTION(BlueprintCallable, Category="GuLiStrike|GroundHUD|Editor")
    static FString ReadMotionState(UGuLiGroundHUDWidget* HUD);
    /** Explicit PIE preparation/input only; never called by the shipping HUD. */
    UFUNCTION(BlueprintCallable, Category="GuLiStrike|GroundHUD|Editor")
    static bool MotionProbeAction(APlayerController* Controller,FName Action);
    UFUNCTION(BlueprintCallable, Category="GuLiStrike|GroundHUD|Editor")
    static FString ReadMotionTargets(APlayerController* Controller);
};
