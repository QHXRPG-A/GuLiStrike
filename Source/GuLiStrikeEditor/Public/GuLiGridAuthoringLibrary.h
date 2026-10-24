#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiGridAuthoringLibrary.generated.h"

/** Project-owned bridge. The map authoring plugin has no dependency on the navigation runtime. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiGridAuthoringLibrary final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Explicit 1/0 status keeps OutError visible in Python; bool + out parameters becomes None on failure.
    UFUNCTION(BlueprintCallable,Category="GuLiStrike|Editor|Ground Grid")
    static int32 ImportPreparedGrid(const FString& Directory,FString& OutError,bool bSavePackage=true);
    /** Imports and reads back the new terrain before retiring the previous Landscape. Does not save. */
    UFUNCTION(BlueprintCallable,Category="GuLiStrike|Editor|Ground Grid")
    static int32 ImportPreparedLandscape(const FString& Directory,FString& OutError);
    UFUNCTION(BlueprintCallable,Category="GuLiStrike|Editor|Ground Grid")
    static int32 FinalizeMigratedResources(FString& OutError,bool bSavePackage=true);
    /** Structured editor data only; does not start a world, render, or save a package. */
    UFUNCTION(BlueprintCallable,Category="GuLiStrike|Editor|Ground Grid")
    static int32 ReadMigrationState(FString& OutJson,FString& OutError);
};
