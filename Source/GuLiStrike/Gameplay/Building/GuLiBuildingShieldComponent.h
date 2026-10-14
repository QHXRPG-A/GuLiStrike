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
	UFUNCTION(BlueprintPure, Category="Building") bool IsShieldEnabled() const;
private:
	bool bGenerator = false;
};
