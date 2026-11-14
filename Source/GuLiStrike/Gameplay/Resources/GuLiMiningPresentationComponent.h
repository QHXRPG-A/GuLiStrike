#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Vfx/GuLiPersistentEffectVisibility.h"
#include "TimerManager.h"
#include "GuLiMiningPresentationComponent.generated.h"

class AGuLiQuadrupedMinerPresentation;

/** Central mining origin and visibility adapter for the formal quadruped. */
UCLASS()
class GULISTRIKE_API UGuLiMiningPresentationComponent final : public UActorComponent
{
	GENERATED_BODY()
public:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	bool InitializePresentation(AActor* Actor);
	bool CalculateMuzzle(const FTransform& VehiclePose, FVector& Muzzle) const;
	void RefreshDisplayedAim();
	void ApplyMining(bool bActive, const FVector& Target);
	UFUNCTION(BlueprintPure, Category="Mining|Diagnostics") FString GetPresentationDiagnosticsJson() const;
	bool IsReady() const;
private:
	void PollVisibility();
	void ApplyVisibleMining(bool bShow);
	FTimerHandle VisibilityTimer;
	FGuLiPersistentEffectVisibility Visibility;
	FVector LatestTarget = FVector::ZeroVector;
	bool bLatestActive = false, bAppliedVisible = false;
	TWeakObjectPtr<AGuLiQuadrupedMinerPresentation> Presentation;
	FVector LocalMuzzle = FVector::ZeroVector;
};
