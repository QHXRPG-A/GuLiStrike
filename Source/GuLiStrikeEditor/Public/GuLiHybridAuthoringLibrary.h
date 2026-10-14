#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiHybridAuthoringLibrary.generated.h"

/** Production-data export and authoring inspection; never starts a game world. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiHybridAuthoringLibrary final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Uses the current map, validated production catalog and the real initial-army layout. */
    UFUNCTION(BlueprintCallable, Category="GuLiStrike|Hybrid|Authoring")
    static FString ExportRuntimeManifest(int32 Population=200, const FString& File=TEXT(""));
    /** Export another authored data profile without saving or mutating the project's defaults.
     * Overrides is a JSON array of {class, property, value}; value uses config text syntax. */
    UFUNCTION(BlueprintCallable, Category="GuLiStrike|Hybrid|Authoring")
    static FString ExportRuntimeManifestWithOverrides(int32 Population,const FString& File,const FString& Overrides);
    /** Read back a file using the loaded runtime's registered contracts. */
    UFUNCTION(BlueprintCallable, Category="GuLiStrike|Hybrid|Authoring")
    static FString ValidateRuntimeManifest(const FString& File);
    /** Reads reflected assets/graphs, never binary packages or a test world.
     * Explicit paths keep the report scoped to production assets. */
    UFUNCTION(BlueprintCallable,Category="GuLiStrike|Hybrid|Authoring")
    static FString InspectProductionBlueprints(const TArray<FString>& AssetPaths);
    /** Retargets a reviewed pure call with exactly compatible pins. Requires a
     * loaded Blueprint, exact graph fingerprint and function identity. Does not
     * load/compile assets or infer authority from a unit/building/skill name. */
    UFUNCTION(BlueprintCallable,Category="GuLiStrike|Hybrid|Authoring")
    static FString RetargetProductionPureCall(const FString& AssetPath,const FString& GraphName,
        const FString& NodeGuid,const FString& ExpectedFingerprint,const FString& ExpectedFunction,
        const FString& ReplacementFunction);
    /** Requires the original map already open and a current exported manifest.
     * Saves a review entry and reads back its entity properties; no gameplay. */
    UFUNCTION(BlueprintCallable,Category="GuLiStrike|Hybrid|Authoring")
    static FString SaveHybridReviewEntry(const FString& ManifestFile,const FString& RoomId,const FString& DevelopmentIdentity,int32 Population=600);
    /** Read-only entity/binding readback of the original map; never launches. */
    UFUNCTION(BlueprintCallable,Category="GuLiStrike|Hybrid|Authoring")
    static FString ReadHybridReviewEntry();
};
