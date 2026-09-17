#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GuLiUnitRenderPolicy.generated.h"

class UMaterialInterface;
class UMeshComponent;
class UPrimitiveComponent;
class AActor;

USTRUCT()
struct FGuLiUnitMaterialVariant
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) TSoftObjectPtr<UMaterialInterface> Original;
	UPROPERTY(EditAnywhere) TSoftObjectPtr<UMaterialInterface> Variant;
};

/** Project-owned variants for non-player-piloted unit bodies. No per-unit MID allocation. */
UCLASS(Config=Game, DefaultConfig)
class UGuLiUnitRenderSettings final : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, EditAnywhere, Category="Rendering")
	TArray<FGuLiUnitMaterialVariant> MaterialVariants;

	UMaterialInterface* ResolveBodyMaterial(UMaterialInterface* Original);
private:
	// The settings CDO shares and retains the small authored set across worlds and pools.
	UPROPERTY(Transient) TMap<FSoftObjectPath, TObjectPtr<UMaterialInterface>> LoadedVariants;
};

namespace GuLiUnitRenderPolicy
{
	/** Does not affect direct-light shadows, collision, custom depth, or gameplay authority. */
	void ApplyReflectionExclusions(UPrimitiveComponent& Component);
	/** Call after assigning the body mesh/materials, before an effect caches those materials. */
	void ApplyBodyMaterials(UMeshComponent& Component);
	void Apply(UMeshComponent& Component);
	/** ChildActor creation callback; also covers nested presentation children. */
	void ApplyToActor(AActor* Actor);
}
