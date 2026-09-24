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
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
private:
	uint32 AppliedRevision = 0;
	void ClearPresentation();
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Primitives;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
};
