#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiGroundRogueDebugLibrary.generated.h"

class APlayerController;
/** Explicit editor preparation and read-only evidence for the Ground review area. */
UCLASS()
class GULISTRIKE_API UGuLiGroundRogueDebugLibrary final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
#if WITH_EDITOR
    UFUNCTION(BlueprintCallable, Category="Ground|Editor", meta=(WorldContext="WorldContextObject"))
    static FString ReadState(UObject* WorldContextObject,FVector Center,float RadiusCentimeters=10000);
    UFUNCTION(BlueprintCallable, Category="Ground|Editor")
    static bool PrepareShooter(APlayerController* Controller,FVector Location,FVector Aim);
    UFUNCTION(BlueprintCallable, Category="Ground|Editor")
    static FString CheckRouteBoundaries();
#endif
};
