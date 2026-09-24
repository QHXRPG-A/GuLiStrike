#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GuLiBuildingShieldComponent.generated.h"

/** A generator shares its own shield pool with friendly targets in range. */
UCLASS()
class GULISTRIKE_API UGuLiBuildingShieldComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiBuildingShieldComponent();
	void InitializeShield();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION(BlueprintPure, Category="Building") bool IsShieldEnabled() const { return bEnabled; }
private:
	UPROPERTY(Replicated) bool bEnabled = false;
	bool bGenerator = false;
	void RegisterBarrier();
	bool HasSupply() const;
};
