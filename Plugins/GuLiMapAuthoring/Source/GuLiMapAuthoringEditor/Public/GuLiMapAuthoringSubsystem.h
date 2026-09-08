#pragma once
#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "GuLiMapTypes.h"
#include "GuLiMapAuthoringSubsystem.generated.h"

class AGuLiMapMarker;

UCLASS()
class GULIMAPAUTHORINGEDITOR_API UGuLiMapAuthoringSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="GuLi|Map Authoring") TArray<UGuLiMapTypeDefinition*> ListTypes();
    UFUNCTION(BlueprintCallable, Category="GuLi|Map Authoring") AGuLiMapMarker* CreateMarker(UGuLiMapTypeDefinition* Type, FVector Location);
    UFUNCTION(BlueprintCallable, Category="GuLi|Map Authoring") FGuLiMapResult GetSnapshot();
    UFUNCTION(BlueprintCallable, Category="GuLi|Map Authoring") FGuLiMapResult UpdateMarker(const FString& MarkerId, const FString& PatchJson);
    UFUNCTION(BlueprintCallable, Category="GuLi|Map Authoring") FGuLiMapResult ValidateMap();
    UFUNCTION(BlueprintCallable, Category="GuLi|Map Authoring") FGuLiMapResult ExportMap();
    UFUNCTION(BlueprintCallable, Category="GuLi|Map Authoring") UGuLiMapTypeDefinition* CreateType(const FString& TypeId);
    UFUNCTION(BlueprintCallable, Category="GuLi|Map Authoring") FGuLiMapResult EnsurePresets();
    UFUNCTION(BlueprintCallable, Category="GuLi|Map Authoring") bool SaveAuthoringPackages();
    UFUNCTION(BlueprintCallable, Category="GuLi|Map Authoring") void OpenPanel();
    UFUNCTION(BlueprintCallable, Category="GuLi|Map Authoring") void ClosePanel();

    UWorld* EditorWorld() const;
    TArray<AGuLiMapMarker*> LoadedMarkers() const;
    bool CollectSnapshot(FGuLiMapSnapshot& Out,TArray<FGuLiMapIssue>& Issues,bool RequireSaved);
    static bool ParsePatch(const FString& Json,const FGuLiMapMarkerRecord& Original,const FTransform& Transform,FGuLiMapMarkerRecord& Out,FTransform& OutTransform,FString& Error);
};
