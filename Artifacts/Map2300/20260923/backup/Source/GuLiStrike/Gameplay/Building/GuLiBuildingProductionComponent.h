#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "GuLiBuildingProductionComponent.generated.h"

UCLASS()
class GULISTRIKE_API UGuLiBuildingProductionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiBuildingProductionComponent();
	void InitializeProduction();
	virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION(BlueprintPure, Category="Building") float GetCycleSeconds() const { return CycleSeconds; }
	UFUNCTION(BlueprintPure, Category="Building") bool IsProducing() const { return bProducing; }
private:
	UPROPERTY(Replicated) float CycleSeconds = 0;
	UPROPERTY(Replicated) bool bProducing = false;
	EGuLiTeam ObservedTeam = EGuLiTeam::Unassigned;
};
