// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GuLiBuildingPlacementPreview.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class UStaticMeshComponent;
struct FGuLiBuildingDefinition;

/** Owning-client-only cosmetic ghost. It has no collision, navigation or replication. */
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class GULISTRIKE_API AGuLiBuildingPlacementPreview : public AActor
{
	GENERATED_BODY()

public:
	AGuLiBuildingPlacementPreview();

	bool Configure(const FGuLiBuildingDefinition& Definition, UMaterialInterface* PreviewMaterial);
	void SetPlacementTransform(const FVector& GroundLocation, float YawDegrees);
	void SetPlacementValidity(bool bValid);

private:
	UPROPERTY(VisibleAnywhere, Category = "Building")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Building")
	TObjectPtr<UStaticMeshComponent> PreviewMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterialInstance;

	bool bHasPlacementValidity = false;
	bool bLastPlacementValidity = false;
};
