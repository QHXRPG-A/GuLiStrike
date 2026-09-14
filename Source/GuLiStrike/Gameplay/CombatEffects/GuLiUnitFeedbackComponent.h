#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GuLiUnitFeedbackComponent.generated.h"

class UGuLiCombatHealthComponent;
class UMeshComponent;
class AGuLiUnitWreck;

/** Event-only feedback for Actor units. A health component supplies confirmed changes, or the Actor supplies standard damage/destruction events. */
UCLASS(ClassGroup=(GuLiStrike), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiUnitFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiUnitFeedbackComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Unit Feedback") bool bPlayDestructionEffect = true;
private:
	UFUNCTION() void HandleHealth(float Health, float Maximum);
	UFUNCTION() void HandleDeath();
	UFUNCTION() void HandleDestroyed(AActor* Actor);
	UFUNCTION() void HandleAnyDamage(AActor* Actor, float Damage, const UDamageType* Type, AController* Instigator, AActor* Causer);
	UFUNCTION(NetMulticast, Unreliable) void MulticastHit();
	void HideLivingMeshes();
	TArray<TWeakObjectPtr<UMeshComponent>> HiddenLivingMeshes;
	TWeakObjectPtr<AGuLiUnitWreck> Wreck;
	TWeakObjectPtr<UGuLiCombatHealthComponent> HealthComponent;
	float PreviousHealth = 0;
	bool bHasHealthBaseline = false;
	bool bDestructionPlayed = false;
};
