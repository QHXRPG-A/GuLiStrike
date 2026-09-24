#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Stronghold/GuLiStrongholdTransitTypes.h"
#include "GuLiStrongholdTransitPresentationComponent.generated.h"
class UStaticMeshComponent;
class UNiagaraComponent;

/** Local reconstruction from server time. Never controls arrival or vehicle tasks. */
UCLASS()
class GULISTRIKE_API UGuLiStrongholdTransitPresentationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiStrongholdTransitPresentationComponent();
	void ApplyState(const FGuLiStrongholdTransitState& InState);
	virtual void TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	FGuLiStrongholdTransitState State;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Orb;
	UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Trail;
	UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Flash;
	FGuid FlashJourney;
	double FlashStart = -1;
	double ServerTime() const;
	void UpdatePresentation();
};
