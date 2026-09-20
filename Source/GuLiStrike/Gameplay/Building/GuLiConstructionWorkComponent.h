#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GuLiConstructionWorkComponent.generated.h"
class UGuLiBuildingLifecycleComponent;
UCLASS()
class GULISTRIKE_API UGuLiConstructionWorkComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiConstructionWorkComponent();
	bool AssignBuilding(UGuLiBuildingLifecycleComponent& Building);
	bool PrepareBuilding(const UGuLiBuildingLifecycleComponent& Building, FVector& OutPosition, float& OutPathLength) const;
	void StopWork();
	virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* Function) override;
	UGuLiBuildingLifecycleComponent* GetTarget() const { return Target.Get(); }
private:
	TWeakObjectPtr<UGuLiBuildingLifecycleComponent> Target;
	FVector WorkPosition = FVector::ZeroVector;
};
