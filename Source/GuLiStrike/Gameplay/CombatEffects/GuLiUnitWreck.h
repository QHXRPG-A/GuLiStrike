#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GuLiUnitWreck.generated.h"

class UBoxComponent;
class UMaterialInterface;
class UMeshComponent;
class UPrimitiveComponent;
class UStaticMesh;

/** Client-local scrap. It owns copied visuals, never the living unit or any combat state. */
UCLASS(Transient, NotPlaceable)
class GULISTRIKE_API AGuLiUnitWreck final : public AActor
{

	GENERATED_BODY()
public:
	AGuLiUnitWreck();
	bool InitializeFromActor(AActor* Source, UMaterialInterface* Material);
	bool InitializeFromStaticMesh(UStaticMesh* Mesh, const FTransform& Transform, UMaterialInterface* Material);
	void StartFalling(const FVector& InitialVelocity, float MaximumLifetime);

private:
	void ConfigureVisual(UMeshComponent* Mesh, const FTransform& Transform, UMaterialInterface* Material);
	UFUNCTION() void HandleImpact(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);
	UPROPERTY(VisibleAnywhere, Category="Wreck") TObjectPtr<UBoxComponent> PhysicsBody;
};
