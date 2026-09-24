#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiConstructionShapeLibrary.generated.h"

class UGuLiConstructionShape;
class UNiagaraSystem;
class UStaticMesh;
class UMaterialInterface;

UCLASS()
class GULISTRIKEEDITOR_API UGuLiConstructionShapeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Builds stale entries only, saves only generated construction assets. JSON describes each definition. */
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Construction")
	static FString PrepareConstructionShapes(bool bForce = false);
	/** Optional provider sources/override for custom buildings; output uses the same cooked shape contract. */
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Construction")
	static UGuLiConstructionShape* BakeConstructionActor(AActor* Actor, const FString& AssetName);
	/** Owned construction systems only. Binds runtime geometry without changing vendor templates. */
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Construction")
	static bool BindConstructionMeshRenderer(UNiagaraSystem* System, FName EmitterName, FName Parameter, UStaticMesh* PreviewMesh, UMaterialInterface* Material);
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Construction")
	static bool BindConstructionSpawnCount(UNiagaraSystem* System, FName EmitterName);
};
