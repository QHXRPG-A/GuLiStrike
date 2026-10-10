#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Vfx/GuLiPersistentEffectVisibility.h"
#include "TimerManager.h"
#include "GuLiMiningPresentationComponent.generated.h"

class USceneComponent;

/** Typed runtime adapter for the preserved SkeletalMeshActor Blueprint and its existing visual functions. */
UCLASS()
class GULISTRIKE_API UGuLiMiningPresentationComponent final : public UActorComponent
{
	GENERATED_BODY()
public:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	bool InitializePresentation(AActor* Actor);
	bool CalculateMuzzles(const FVector& Target, const FTransform& VehiclePose, FVector& Left, FVector& Right) const;
	void AimAt(const FVector& Target);
	void ApplyMining(bool bActive, const FVector& Target);
	UFUNCTION(BlueprintPure, Category="Mining|Diagnostics") FString GetPresentationDiagnosticsJson() const;
	bool IsReady() const { return Presentation.IsValid() && Pivots[0].IsValid() && Pivots[1].IsValid(); }
private:
	void PollVisibility();
	void ApplyVisibleMining(bool bShow);
	FTimerHandle VisibilityTimer;
	FGuLiPersistentEffectVisibility Visibility;
	FVector LatestTarget = FVector::ZeroVector;
	bool bLatestActive = false, bAppliedVisible = false;
	TWeakObjectPtr<AActor> Presentation;
	TWeakObjectPtr<USceneComponent> Pivots[2];
	TWeakObjectPtr<USceneComponent> Muzzles[2];
	FVector LocalPivots[2];
	float BarrelLengths[2] = {};
	FRotator TravelRotations[2];
};
