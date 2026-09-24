#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiLandscapeAuthoringLibrary.generated.h"

class ALandscape;
class ULandscapeLayerInfoObject;

/** Persistent Landscape authoring operations used by map migration scripts. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiLandscapeAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Register UE 5.7 target layers before importing weights; LandscapeInfo::Layers is only a cache. */
	UFUNCTION(BlueprintCallable, Category = "GuLi|Landscape")
	static bool RegisterTargetLayers(ALandscape* Landscape, const TArray<ULandscapeLayerInfoObject*>& LayerInfos);

	/** Import all layers together from interleaved uint8 samples, in LayerInfos order, over the full landscape extent. */
	UFUNCTION(BlueprintCallable, Category = "GuLi|Landscape")
	static bool ImportTargetLayerWeights(ALandscape* Landscape, const TArray<ULandscapeLayerInfoObject*>& LayerInfos, const FString& WeightFile);
};
