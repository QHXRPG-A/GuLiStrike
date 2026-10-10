#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GuLiPerformanceReviewActor.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;

/** Saved, opt-in comparison fixture. It never fires gameplay weapons or switches the VFX registry. */
UCLASS()
class GULISTRIKE_API AGuLiPerformanceReviewActor final : public AActor
{
	GENERATED_BODY()
public:
	AGuLiPerformanceReviewActor();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UFUNCTION(BlueprintCallable, Category="Performance Review") void StartComparison();
	UFUNCTION(BlueprintCallable, Category="Performance Review") void StopComparison();
	UPROPERTY(EditAnywhere, Category="Performance Review") TSoftObjectPtr<UNiagaraSystem> System;
	UPROPERTY(EditAnywhere, Category="Performance Review") FVector BaseScale = FVector::OneVector;
	UPROPERTY(EditAnywhere, Category="Performance Review") FVector RelativeBeamEnd = FVector(1600,0,0);
	UPROPERTY(EditAnywhere, Category="Performance Review") bool bLaser = true;
	/** Explicit client-only load through the production catalog impact entry. Defaults off. */
	UPROPERTY(EditAnywhere, Category="Performance Review") bool bCatalogImpacts=false;
	/** Opt-in muzzle protocol comparison; synthetic visual cues never affect gameplay. */
	UPROPERTY(EditAnywhere, Category="Performance Review") bool bCatalogMuzzles=false;
	UPROPERTY(EditAnywhere, Category="Performance Review",meta=(ClampMin="0",ClampMax="2")) int32 MuzzleMode=2;
	UPROPERTY(EditAnywhere, Category="Performance Review") bool bHeavyMuzzle=false;
	UPROPERTY(EditAnywhere, Category="Performance Review") FVector MuzzleFollowVelocity=FVector(600,0,0);
	/** Opt-in saved QA path through the same client bounds/detail policy. Zero retains the older fixture. */
	UPROPERTY(EditAnywhere, Category="Performance Review") int32 PolicyEffectId=0;
	UPROPERTY(EditAnywhere, Category="Performance Review",meta=(ClampMin="0")) float ImpactEventsPerSecond=200;
	UPROPERTY(EditAnywhere, Category="Performance Review", meta=(ClampMin="1",ClampMax="128")) int32 EffectCount = 1;
	UPROPERTY(EditAnywhere, Category="Performance Review", meta=(ClampMin="0.01")) float ActiveSeconds = 2.4f;
	UPROPERTY(EditAnywhere, Category="Performance Review", meta=(ClampMin="0.02")) float CycleSeconds = 4.f;
private:
	UPROPERTY() TObjectPtr<UNiagaraComponent> Effect;
	UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> AdditionalEffects;
	float Elapsed = 0;
	double PendingImpactEvents=0;
	int32 ImpactSerial=0;
	bool bActive = false;
	bool bPolicyVisible=true;
	uint8 PolicyDetail=0;
	double PolicyNextCheck=0, PolicyOffscreenSince=-1, PolicyLastTransition=0;
	void UpdatePolicy();
};
