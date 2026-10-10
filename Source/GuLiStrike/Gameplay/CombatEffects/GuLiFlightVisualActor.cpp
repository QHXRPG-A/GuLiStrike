#include "Gameplay/CombatEffects/GuLiFlightVisualActor.h"
#include "Gameplay/GuLiStrikeProjectile.h"
#include "Gameplay/CombatEffects/GuLiClientFlightPool.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

AGuLiFlightVisualActor::AGuLiFlightVisualActor()
{
	bReplicates = false; SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LocalFlightMesh"));
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("FlightRoot")));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false); Mesh->SetCastShadow(false);
}

void AGuLiFlightVisualActor::ActivateFlight(const FGuLiFlightEvent& Event)
{
	ResetForPool(); Recipe = Event; Prediction = Event.State; PredictionTime = Event.State.SampleTime;
	CurveCoefficients.Initialize(Event.State);
	SetActorLocationAndRotation(Prediction.Location, FVector(Prediction.Velocity).Rotation());
	if (UClass* Class = Event.ShipVisualClass.LoadSynchronous())
	{
		SetActorScale3D(Event.VisualScale);
		if (const auto* Template = Class->GetDefaultObject<AGuLiStrikeProjectile>()->GetFlightMesh())
		{
			Mesh->SetStaticMesh(Template->GetStaticMesh());
			Mesh->SetRelativeTransform(Template->GetRelativeTransform());
			for (int32 Index=0; Index<Template->GetNumMaterials(); ++Index) Mesh->SetMaterial(Index,Template->GetMaterial(Index));
		}
	}
}

void AGuLiFlightVisualActor::ResetForPool()
{
	SetActorHiddenInGame(true);
	Mesh->SetStaticMesh(nullptr); Mesh->EmptyOverrideMaterials(); Mesh->SetRelativeTransform(FTransform::Identity);
	SetActorScale3D(FVector::OneVector); SetActorTransform(FTransform::Identity);
	SetOwner(nullptr); SetInstigator(nullptr); Tags.Reset();
	Recipe = {}; Prediction = {}; PredictionTime = 0; bStopped = false;
}

void AGuLiFlightVisualActor::ShowFlight(bool bVisible) { SetActorHiddenInGame(!bVisible); }

void AGuLiFlightVisualActor::AdvanceFlight(float ServerTime, const FVector* TargetPosition, bool bApplyTransform)
{
	GuLiClientFlight::Advance(Prediction, PredictionTime, bStopped, Recipe, GetWorld(), this, ServerTime, TargetPosition,
		GuLiCombatEffects::ShouldPrecomputeCurves() ? &CurveCoefficients : nullptr);
	// Empty legacy pool entries have no rendering consumer for an Actor transform.
	if (bApplyTransform && Mesh->GetStaticMesh()) SetActorLocationAndRotation(GetDisplayLocation(ServerTime), FVector(Prediction.Velocity).Rotation());
}

void AGuLiFlightVisualActor::ApplyPrediction(const FGuLiCombatEffectState& State, const FVector& DisplayLocation, bool bApplyTransform)
{
	Prediction = State;
	if (bApplyTransform && Mesh->GetStaticMesh()) SetActorLocationAndRotation(DisplayLocation, FVector(State.Velocity).Rotation());
}

FBox AGuLiFlightVisualActor::GetDisplayBounds(const FVector& DisplayLocation, const FVector& Velocity) const
{
	if (!Mesh->GetStaticMesh()) return FBox(ForceInit);
	return Mesh->GetStaticMesh()->GetBoundingBox().TransformBy(Mesh->GetRelativeTransform()
		* FTransform(Velocity.Rotation(),DisplayLocation,GetActorScale3D()));
}

FVector AGuLiFlightVisualActor::GetDisplayLocation(float ServerTime) const
{
	return GuLiClientFlight::DisplayLocation(Prediction, PredictionTime, bStopped, Recipe, ServerTime,
		GuLiCombatEffects::ShouldPrecomputeCurves() ? &CurveCoefficients : nullptr);
}
