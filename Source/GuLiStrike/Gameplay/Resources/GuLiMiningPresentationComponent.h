#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GuLiMiningPresentationComponent.generated.h"

class USceneComponent;

/** Typed runtime adapter for the preserved SkeletalMeshActor Blueprint and its existing visual functions. */
UCLASS()
class GULISTRIKE_API UGuLiMiningPresentationComponent final : public UActorComponent
{
	GENERATED_BODY()
public:
	bool InitializePresentation(AActor* Actor);
	bool CalculateMuzzles(const FVector& Target, const FTransform& VehiclePose, FVector& Left, FVector& Right) const;
	void AimAt(const FVector& Target);
	void ApplyMining(bool bActive, const FVector& Target);
	bool IsReady() const { return Presentation.IsValid() && Pivots[0].IsValid() && Pivots[1].IsValid(); }
private:
	TWeakObjectPtr<AActor> Presentation;
	TWeakObjectPtr<USceneComponent> Pivots[2];
	TWeakObjectPtr<USceneComponent> Muzzles[2];
	FVector LocalPivots[2];
	float BarrelLengths[2] = {};
	FRotator TravelRotations[2];
};
