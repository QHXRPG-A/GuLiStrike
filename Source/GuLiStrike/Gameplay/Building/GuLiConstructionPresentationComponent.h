#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Building/GuLiConstructionShape.h"
#include "GuLiConstructionPresentationComponent.generated.h"

class UGuLiBuildingLifecycleComponent;
class USceneComponent;
class UNiagaraComponent;

USTRUCT()
struct FGuLiConstructionPresentationState
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<AActor> Building;
	UPROPERTY() uint32 BuildingInstance = 0;
	UPROPERTY() bool bActive = false;
	UPROPERTY() double ScanStartedAt = 0;
};

/** Builder-only adapter. The miner Blueprint's tick never writes these two Niagara components. */
UCLASS()
class GULISTRIKE_API UGuLiConstructionPresentationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiConstructionPresentationComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* Function) override;
	void SetConstructionAuthority(UGuLiBuildingLifecycleComponent* Building);
	void InitializePresentation(AActor* Actor);
	void HideBeams();
private:
	UPROPERTY(ReplicatedUsing=OnRep_State) FGuLiConstructionPresentationState State;
	UFUNCTION() void OnRep_State();
	TWeakObjectPtr<AActor> Presentation;
	TWeakObjectPtr<USceneComponent> Pivots[2];
	TWeakObjectPtr<USceneComponent> Muzzles[2];
	TWeakObjectPtr<UNiagaraComponent> Beams[2];
	FRotator TravelRotations[2];
	FGuLiConstructionSpan SelectedSpan;
	bool bShowing = false;
};
