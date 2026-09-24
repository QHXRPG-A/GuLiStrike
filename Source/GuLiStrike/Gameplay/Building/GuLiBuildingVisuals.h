#pragma once

#include "CoreMinimal.h"

class AActor;
class UMeshComponent;
class USceneComponent;
class UMaterialInterface;
struct FGuLiBuildingDefinition;

/** Mesh-only copies never execute the source Blueprint or its collision/ramp logic. */
namespace GuLiBuildingVisuals
{
	inline constexpr float FactoryScale = 0.2f;
	GULISTRIKE_API UMeshComponent* CopyMesh(AActor& Owner, USceneComponent& Parent,
		UMeshComponent& Source, const FTransform& RelativeTransform, UMaterialInterface* Override,
		bool bFollowPose = false);
	GULISTRIKE_API bool CreatePreviewMeshes(AActor& Owner, USceneComponent& Parent,
		const FGuLiBuildingDefinition& Definition, UMaterialInterface* Material,
		TArray<TObjectPtr<UMeshComponent>>& OutMeshes);
	GULISTRIKE_API UClass* ResolveFactoryPresentation(const UObject* Context);
}
