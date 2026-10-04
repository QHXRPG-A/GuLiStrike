#include "Gameplay/CombatEffects/GuLiFlightVisualActor.h"
#include "Gameplay/GuLiStrikeProjectile.h"
#include "Components/StaticMeshComponent.h"
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

void AGuLiFlightVisualActor::AdvanceFlight(float ServerTime, const FVector* TargetPosition)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiClientFlightPrediction);
	if (!Recipe.State.EffectId.IsValid() || bStopped) return;
	const double Until = FMath::Min(double(ServerTime),double(Recipe.State.EndTime));
	if (Recipe.ShipVisualClass.IsNull() && Prediction.Kind == EGuLiCombatEffectKind::LinearProjectile)
	{
		Prediction.Location = FVector(Recipe.State.LaunchLocation)+FVector(Recipe.State.Velocity)*FMath::Max(0.0,Until-Recipe.State.StartTime);
		PredictionTime = Until;
	}
	else
	{
		if (TargetPosition && !Prediction.bFixedPoint) Prediction.LastTargetLocation = *TargetPosition;
		// Persist the predictor over the entire lifetime. Catch-up is bounded by
		// the frozen lifetime (<=120s), never by collision-confirmation sample time.
		while (PredictionTime + 1.0/30.0 <= Until + UE_SMALL_NUMBER && !bStopped)
		{
			const float Step = 1.f/30.f;
			PredictionTime += Step;
			if (Recipe.ShipVisualClass.IsNull())
				GuLiCombatEffects::AdvanceProjectile(Prediction,float(PredictionTime-Prediction.StartTime),Step);
			else
			{
				const FVector Previous = Prediction.Location;
				FVector Velocity = Prediction.Velocity;
				Velocity.Z += Recipe.Gravity*Step;
				if (Recipe.MaximumSpeed>0) Velocity = Velocity.GetClampedToMaxSize(Recipe.MaximumSpeed);
				FVector Next = Previous+Velocity*Step;
				FHitResult Hit;
				FCollisionQueryParams Query(SCENE_QUERY_STAT(GuLiLocalFlightBounce),false,this);
				if (GetWorld()->SweepSingleByObjectType(Hit,Previous,Next,FQuat::Identity,
					FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeSphere(FMath::Max(1.f,Prediction.Motion.SweepRadius)),Query))
				{
					Next = Hit.Location+Hit.Normal*.5f;
					if (Recipe.bBounce)
					{
						const FVector NormalPart = Hit.Normal*FVector::DotProduct(Velocity,Hit.Normal);
						Velocity = (Velocity-NormalPart)*(1-FMath::Clamp(Recipe.Friction,0.f,1.f))-NormalPart*Recipe.Bounciness;
						bStopped = Velocity.Size()<Recipe.StopSpeed;
					}
					else bStopped = true;
				}
				Prediction.Location = Next; Prediction.Velocity = Velocity;
			}
		}
	}
	Prediction.SampleTime = PredictionTime;
	SetActorLocationAndRotation(GetDisplayLocation(ServerTime),FVector(Prediction.Velocity).Rotation());
}

FVector AGuLiFlightVisualActor::GetDisplayLocation(float ServerTime) const
{
	if (bStopped || (Prediction.Kind==EGuLiCombatEffectKind::LinearProjectile && Recipe.ShipVisualClass.IsNull())) return Prediction.Location;
	const double Residual = FMath::Clamp(double(ServerTime)-PredictionTime,0.0,1.0/30.0);
	if (Recipe.ShipVisualClass.IsNull() && ServerTime-Prediction.StartTime<=Prediction.Motion.LiftSeconds)
		return GuLiCombatEffects::LiftPosition(Prediction,FMath::Max(0.f,ServerTime-Prediction.StartTime));
	return FVector(Prediction.Location)+FVector(Prediction.Velocity)*Residual;
}
