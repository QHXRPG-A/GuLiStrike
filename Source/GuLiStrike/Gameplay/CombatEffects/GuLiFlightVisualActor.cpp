#include "Gameplay/CombatEffects/GuLiFlightVisualActor.h"
#include "Gameplay/CombatEffects/GuLiClientFlightPool.h"

AGuLiFlightVisualActor::AGuLiFlightVisualActor()
{
	bReplicates = false; SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;
	SetActorHiddenInGame(true);
}

void AGuLiFlightVisualActor::ActivateFlight(const FGuLiFlightEvent& Event)
{
	ResetForPool(); Recipe = Event; Prediction = Event.State; PredictionTime = Event.State.SampleTime;
	CurveCoefficients.Initialize(Event.State);
}

void AGuLiFlightVisualActor::ResetForPool()
{
	SetOwner(nullptr); SetInstigator(nullptr); Tags.Reset();
	Recipe = {}; Prediction = {}; PredictionTime = 0;
}

void AGuLiFlightVisualActor::AdvanceFlight(float ServerTime, const FVector* TargetPosition)
{
	GuLiClientFlight::Advance(Prediction, PredictionTime, Recipe, ServerTime, TargetPosition,
		GuLiCombatEffects::ShouldPrecomputeCurves() ? &CurveCoefficients : nullptr);
}

FVector AGuLiFlightVisualActor::GetDisplayLocation(float ServerTime) const
{
	return GuLiClientFlight::DisplayLocation(Prediction, PredictionTime, ServerTime,
		GuLiCombatEffects::ShouldPrecomputeCurves() ? &CurveCoefficients : nullptr);
}
