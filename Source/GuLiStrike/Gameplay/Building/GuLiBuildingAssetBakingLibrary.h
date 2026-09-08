// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiBuildingAssetBakingLibrary.generated.h"

class UStaticMesh;

/** Editor bridge used by the deterministic building-asset bake script. */
UCLASS()
class GULISTRIKE_API UGuLiBuildingAssetBakingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "GuLiStrike|Editor|Building", meta = (DevelopmentOnly))
	static bool BakeRenderGeometryScale(
		UStaticMesh* SourceMesh,
		UStaticMesh* TargetMesh,
		FVector BuildScale,
		bool bEnableNanite);
};
