#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiMeshLODInspectionLibrary.generated.h"

class UStaticMesh;

/** Editor-only inspection of material inputs in the existing mesh resource LODs. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiMeshLODInspectionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Presentation|Validation")
	static FString InspectStaticMeshLODInputs(UStaticMesh* Mesh);
};
