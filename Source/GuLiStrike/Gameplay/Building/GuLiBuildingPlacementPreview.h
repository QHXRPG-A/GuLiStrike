// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GuLiBuildingPlacementPreview.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class UStaticMeshComponent;
class UMeshComponent;
class UDecalComponent;
struct FGuLiBuildingDefinition;

/** Owning-client-only cosmetic ghost. It has no collision, navigation or replication. */
UCLASS(Config=Game, NotBlueprintable, NotPlaceable, Transient)
class GULISTRIKE_API AGuLiBuildingPlacementPreview : public AActor
{
	GENERATED_BODY()

public:
	AGuLiBuildingPlacementPreview();

	bool Configure(const FGuLiBuildingDefinition& Definition, int32 PreviewVfxId);
	void SetPlacementTransform(const FVector& GroundLocation, float YawDegrees);
	void SetPlacementValidity(bool bValid);
	FVector GetSceneUIFootprintExtent() const { return SceneUIFootprintExtent; }
	bool IsSceneUIPlacementValid() const { return bLastPlacementValidity; }
	const TArray<TObjectPtr<UMeshComponent>>& GetSceneUIMeshes() const { return PreviewMeshes; }

private:
	UPROPERTY(Config) TSoftObjectPtr<UMaterialInterface> SceneUIMaskMaterial;
	UPROPERTY(VisibleAnywhere, Category = "Building")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Building")
	TObjectPtr<UDecalComponent> GridDecal;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMeshComponent>> PreviewMeshes;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GridMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewMaterialInstance;

	bool bHasPlacementValidity = false;
	FVector SceneUIFootprintExtent = FVector::ZeroVector;
	bool bLastPlacementValidity = false;
};
