#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GuLiStrongholdGatePresentationComponent.generated.h"
class UGuLiStrongholdGateComponent;
class UDecalComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class GULISTRIKE_API UGuLiStrongholdGatePresentationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	void InitializePresentation(UGuLiStrongholdGateComponent& Gate);
private:
	UPROPERTY(Transient) TObjectPtr<UDecalComponent> Ground;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> AirNode;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GroundMaterial;
};
