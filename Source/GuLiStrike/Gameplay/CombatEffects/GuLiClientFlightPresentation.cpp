#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Gameplay/CombatEffects/GuLiFlightVisualActor.h"
#include "Gameplay/GroundMech/GuLiGroundMechWeaponComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

AGuLiFlightVisualActor* UGuLiCombatEffectPresentationSubsystem::AcquireFlightActor(const FGuLiFlightEvent& Event)
{
	if (!GetWorld() || GetWorld()->GetNetMode()==NM_DedicatedServer) return nullptr;
	if (FreeFlightActors.IsEmpty())
	{
		const auto* Settings = GetDefault<UGuLiCombatEffectSettings>();
		const int32 Count = FMath::Max(1,FlightActors.IsEmpty() ? Settings->ClientFlightPoolInitialCapacity : Settings->ClientFlightPoolGrowthSize);
		FActorSpawnParameters Parameters; Parameters.ObjectFlags |= RF_Transient;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (int32 Index=0; Index<Count; ++Index)
			if (auto* Actor = GetWorld()->SpawnActor<AGuLiFlightVisualActor>(FVector::ZeroVector,FRotator::ZeroRotator,Parameters))
			{ Actor->ResetForPool(); FlightActors.Add(Actor); FreeFlightActors.Add(Actor); }
	}
	if (FreeFlightActors.IsEmpty()) return nullptr;
	AGuLiFlightVisualActor* Actor = FreeFlightActors.Pop(EAllowShrinking::No); Actor->ActivateFlight(Event); return Actor;
}

void UGuLiCombatEffectPresentationSubsystem::ReleaseFlightActor(AGuLiFlightVisualActor* Actor)
{
	if (!IsValid(Actor) || FreeFlightActors.Contains(Actor)) return;
	Actor->ResetForPool(); FreeFlightActors.Add(Actor);
}

void UGuLiCombatEffectPresentationSubsystem::ApplyFlightEvent(const FGuLiFlightEvent& Event)
{
	const auto& State = Event.State;
	if (!State.IsWellFormed() || !GuLiFlightWire::IsFlight(State.Kind)) return;
	if (State.MatchEpoch!=Epoch) BeginEpoch(State.MatchEpoch);
	if (State.MatchEpoch!=Epoch || Tombstones.Contains(State.EffectId)) return;
	const bool Finished = State.Phase==EGuLiCombatEffectPhase::Finished;
	if (Finished && !Event.bBootstrap && ServerTime()-State.SampleTime<=.5f)
	{
		if (Event.ImpactVfxId>0)
			if (auto* Burst=SpawnPooled(Event.ImpactVfxId,State.Location,1.f,0,FVector(Event.ImpactNormal).Rotation())) Retire(Burst,3.f,false);
		// The terminal recipe identifies logical missiles explicitly, including when
		// their local flight has already expired. Authored impact fields stay distinct.
		if (Event.bUseCatalogImpact)
		{ auto Impact=State; Impact.Kind=EGuLiCombatEffectKind::LinearProjectile; PlayMachineGunImpact(Impact); }
	}
	// A bootstrap and live create can be adjacent on the same ordered channel.
	// Bootstrap is silent; the live event still supplies its one accepted muzzle.
	if (!Finished && Event.bHasMuzzle && !Event.bBootstrap)
	{
		if (State.Source.Kind==EGuLiTargetKind::GroundActor)
		{
			for (TActorIterator<APawn> It(GetWorld()); It; ++It)
				if (auto* Weapon=It->FindComponentByClass<UGuLiGroundMechWeaponComponent>(); Weapon && Weapon->SourceHandle==State.Source)
				{ Weapon->PlayFlightShot(State.EffectId,Event.Muzzle.ServerTime); break; }
		}
		else ApplyShots({Event.Muzzle});
	}
	ApplyState(State,Event.bBootstrap);
	if (auto* Visual = Visuals.Find(State.EffectId))
	{
		if (Finished)
		{
			if (Visual->FlightActor) { Visual->FlightActor->SetActorLocation(State.Location); Visual->FlightActor->ShowFlight(false); }
		}
		else if (!Visual->FlightActor) Visual->FlightActor = AcquireFlightActor(Event);
	}
}
