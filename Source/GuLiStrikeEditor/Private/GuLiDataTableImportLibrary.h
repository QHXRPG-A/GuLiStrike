#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiDataTableImportLibrary.generated.h"

class UDataTable;

/** Editor data-pipeline import that preserves the identity of referenced tables. */
UCLASS()
class UGuLiDataTableImportLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** An empty result means success. Invalid input leaves the destination unchanged. */
    UFUNCTION(BlueprintCallable, Category="GuLiStrike|Data|Editor")
    static TArray<FString> UpdateFromJson(UDataTable* Table, const FString& Json);
};
