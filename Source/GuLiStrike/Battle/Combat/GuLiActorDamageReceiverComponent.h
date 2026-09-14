#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GuLiActorDamageReceiverComponent.generated.h"

class UGuLiCombatHealthComponent;
class UDamageType;
class AController;

/** Connects standard Actor damage to the shared damage ledger. Health, feedback and destruction stay separate. */
UCLASS(ClassGroup=(GuLiStrike), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiActorDamageReceiverComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiActorDamageReceiverComponent();
	UPROPERTY(EditAnywhere, Category="Combat") bool bDestroyOwnerOnDeath = true;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UFUNCTION() void ReceiveDamage(AActor* Actor, float Damage, const UDamageType* Type, AController* Instigator, AActor* Causer);
	UFUNCTION() void HandleDeath();
	UPROPERTY() TObjectPtr<UGuLiCombatHealthComponent> Health;
};
