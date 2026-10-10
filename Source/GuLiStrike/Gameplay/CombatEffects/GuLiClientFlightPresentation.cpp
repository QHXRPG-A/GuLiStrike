#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Gameplay/CombatEffects/GuLiFlightVisualActor.h"
#include "Gameplay/GroundMech/GuLiGroundMechWeaponComponent.h"
#include "Engine/World.h"
#include "NiagaraSystem.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarGuLiClientFlightDataPool(TEXT("gs.Flights.DataPool"), 1,
	TEXT("Use local prediction data slots; set before launching a comparison workload. 0 retains legacy Actor pooling."));

AGuLiFlightVisualActor* UGuLiCombatEffectPresentationSubsystem::AcquireFlightActor(const FGuLiFlightEvent& Event)
{
	if (!GetWorld() || GetWorld()->GetNetMode()==NM_DedicatedServer) return nullptr;
	if (FreeFlightActors.IsEmpty())
	{
		const auto* Settings = GetDefault<UGuLiCombatEffectSettings>();
		const int32 ConfiguredCount = FMath::Max(1,FlightActors.IsEmpty() ? Settings->ClientFlightPoolInitialCapacity : Settings->ClientFlightPoolGrowthSize);
		const int32 Count = CVarGuLiClientFlightDataPool.GetValueOnGameThread() ? FMath::Min(ConfiguredCount,64) : ConfiguredCount;
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

void UGuLiCombatEffectPresentationSubsystem::ResolveFlightConfiguration(FGuLiLocalCombatEffect& Visual)
{
	if (!Visual.LoadedDefinition && !Visual.State.ProjectileDefinition.IsNull())
		Visual.LoadedDefinition = Visual.State.ProjectileDefinition.LoadSynchronous();
	Visual.bUsesMissileCluster = Visual.LoadedDefinition && Visual.LoadedDefinition->UsesMissileClusterRendering();
	const int32 VfxId = Visual.LoadedDefinition ? Visual.LoadedDefinition->FlightVfxId : Visual.State.PlayerBulletVfxId;
	if (!Visual.LoadedFlightSystem && VfxId > 0) Visual.LoadedFlightSystem = GuLiVfx::Load<UNiagaraSystem>(this, VfxId);
	Visual.bFlightConfigurationPending = (!Visual.State.ProjectileDefinition.IsNull() && !Visual.LoadedDefinition)
		|| (VfxId > 0 && !Visual.LoadedFlightSystem);
	Visual.NextFlightConfigurationRetry = ServerTime() + .5f;
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
			if (Visual->FlightActor) Visual->FlightActor->ResetForPool();
		}
		else if (!Visual->FlightRecipe.State.EffectId.IsValid())
		{
			Visual->FlightRecipe = Event;
			ResolveFlightConfiguration(*Visual);
			const bool bDataPool = CVarGuLiClientFlightDataPool.GetValueOnGameThread() != 0;
			if (bDataPool)
			{
				const auto* Settings = GetDefault<UGuLiCombatEffectSettings>();
				const int32 Growth = ClientFlights.GetCapacity() == 0 ? Settings->ClientFlightPoolInitialCapacity : Settings->ClientFlightPoolGrowthSize;
				const auto Kind = State.Kind == EGuLiCombatEffectKind::LinearProjectile
					? EGuLiClientFlightRenderKind::Laser : EGuLiClientFlightRenderKind::Niagara;
				Visual->FlightHandle = ClientFlights.Acquire(Event, Kind, Growth);
			}
			if (!bDataPool) Visual->FlightActor = AcquireFlightActor(Event);
		}
	}
}
