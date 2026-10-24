#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiMapDesignAuthoringLibrary.generated.h"

USTRUCT(BlueprintType)
struct FGuLiMapDesignResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly) bool bRolledBack = false;
    UPROPERTY(BlueprintReadOnly) FString Json;
    UPROPERTY(BlueprintReadOnly) FString Message;
};

/** Editor-only bridge. The offline browser never invokes this automatically. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiMapDesignAuthoringLibrary final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Source height/weights and geometry fingerprints; no save, rendering or gameplay. */
    UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Map Design")
    static FGuLiMapDesignResult ReadSourceBaseline();

    /** Hash, target, authoring and current 49-outpost gameplay validation, without mutation. */
    UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Map Design")
    static FGuLiMapDesignResult PreviewDesignImport(const FString& Directory);

    /** Applies a reviewed export and rebuilds production data. Saves only after readback succeeds. */
    UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Map Design")
    static FGuLiMapDesignResult ApplyDesignImport(const FString& Directory, bool bPrepareNavigation=true, bool bSavePackages=true);
};
