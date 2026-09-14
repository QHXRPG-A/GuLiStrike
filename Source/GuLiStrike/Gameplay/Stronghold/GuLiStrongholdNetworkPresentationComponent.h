#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GuLiStrongholdNetworkPresentationComponent.generated.h"
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class GULISTRIKE_API UGuLiStrongholdNetworkPresentationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiStrongholdNetworkPresentationComponent();
	virtual void TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
private:
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Lines;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
};
