#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/CombatEffects/GuLiFlightEvent.h"
#include "GuLiFlightVisualActor.generated.h"

class UStaticMeshComponent;

/** Local pool entry. Never replicates, simulates damage, or receives gameplay hit callbacks. */
UCLASS(NotBlueprintable, Transient)
class GULISTRIKE_API AGuLiFlightVisualActor final : public AActor
{
	GENERATED_BODY()
public:
	AGuLiFlightVisualActor();
	void ActivateFlight(const FGuLiFlightEvent& Event);
	void ResetForPool();
	void AdvanceFlight(float ServerTime, const FVector* TargetPosition);
	void ShowFlight(bool bVisible);
	const FGuLiCombatEffectState& GetPrediction() const { return Prediction; }
	FVector GetDisplayLocation(float ServerTime) const;
private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY() FGuLiFlightEvent Recipe;
	UPROPERTY() FGuLiCombatEffectState Prediction;
	double PredictionTime = 0;
	bool bStopped = false;
};
