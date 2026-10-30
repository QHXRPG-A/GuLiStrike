#include "Network/GuLiNetworkSettings.h"
#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"
#include "Gameplay/CombatEffects/GuLiFlightVisualActor.h"
#include "Gameplay/GroundMech/Weapons/GuLiGroundMechWeaponComponent.h"
#include "Engine/World.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarGuLiClientFlightDataPool(TEXT("gs.Flights.DataPool"), 1,
	TEXT("Use local prediction data slots; set before launching a comparison workload. 0 retains legacy Actor pooling."));

AGuLiFlightVisualActor* UGuLiCombatEffectPresentationSubsystem::AcquireFlightActor(const FGuLiFlightEvent& Event)
{
	if (!GetWorld() || !UGuLiNetworkSettings::CanPresent(GetWorld())) return nullptr;
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
	ResolveGroundTrailConfiguration(Visual);
}

void UGuLiCombatEffectPresentationSubsystem::ResolveGroundTrailConfiguration(FGuLiLocalCombatEffect& Visual)
{
	if (Visual.State.Kind != EGuLiCombatEffectKind::LinearProjectile || Visual.State.Source.Kind != EGuLiTargetKind::GroundActor) return;
	FGuLiStrikeVfxEffectsRow Row;
	if (!UGuLiVfxRegistrySubsystem::Query(this, Visual.State.PlayerBulletVfxId, Row)) return;
	Visual.GroundTrailLength = Row.TrailLengthCm;
	Visual.GroundTrailHold = Row.TrailHoldSeconds; Visual.GroundTrailFade = Row.TrailFadeSeconds;
	Visual.GroundTrailBend = Row.TrailBendCm; Visual.GroundTrailWidth = 12.f * Row.Scale.Y;
}

bool UGuLiCombatEffectPresentationSubsystem::UpdateGroundTrail(FGuLiLocalCombatEffect& Visual, float Now, bool bEnabled)
{
	const auto& State = Visual.State;
	const bool bTerminal = State.Phase == EGuLiCombatEffectPhase::Finished;
	const float Speed = Visual.bInstantGroundSegment?1000000000.f:FMath::Max(1.f, float(FVector(State.Velocity).Size()));
	const FVector Direction = FVector(State.LaunchDirection).GetSafeNormal();
	const FVector Origin = State.LaunchLocation;
	if (Visual.GroundTrailEndedAt < 0 && (bTerminal || Now >= State.EndTime))
	{
		// Store the impact in world space. Neither the character nor camera can move it.
		Visual.GroundTrailEndedAt = bTerminal ? FMath::Clamp(State.SampleTime, State.StartTime, Now) : State.EndTime;
		Visual.GroundTrailEnd = bTerminal ? FVector(State.Location) : Origin + FVector(State.Velocity) * (State.EndTime-State.StartTime);
	}
	const bool bEnded = Visual.GroundTrailEndedAt >= 0;
	const float EndAge = bEnded ? FMath::Max(0.f, Now-Visual.GroundTrailEndedAt) : 0.f;
	const bool bExpired = bEnded && EndAge >= Visual.GroundTrailHold+Visual.GroundTrailFade;
	if (bEnded) Visual.RenderLocation = Visual.GroundTrailEnd;
	else if (!Visual.bHasPrediction) Visual.RenderLocation = Origin + FVector(State.Velocity) * FMath::Max(0.f,Now-State.StartTime);
	const float Distance = FMath::Max(0.f, float(FVector::DotProduct(Visual.RenderLocation-Origin,Direction)));
	const float Canvas = 2.f*(Visual.GroundTrailBend+Visual.GroundTrailWidth);
    const float ChunkLength=Visual.bInstantGroundSegment?FMath::Max(Visual.GroundTrailLength,Distance):Visual.GroundTrailLength;
	const int32 First=FMath::Max(0,FMath::FloorToInt((Distance-ChunkLength)/ChunkLength));
	const int32 Last=FMath::FloorToInt(FMath::Max(0.f,Distance-.01f)/ChunkLength);
	const FRotator Rotation=Direction.Rotation();
	if (Visual.GroundTrailFacing.IsNearlyZero())
	{
		FVector View=Origin-FVector::UpVector; FRotator ViewRotation;
		if (auto* PC=GetWorld()->GetFirstPlayerController()) PC->GetPlayerViewPoint(View,ViewRotation);
		FVector Normal=FVector::VectorPlaneProject(View-Origin,Direction).GetSafeNormal();
		if (Normal.IsNearlyZero()) Normal=Rotation.RotateVector(FVector::RightVector);
		// Freeze the curve's plane once. Later camera motion does not swivel the trace.
		Visual.GroundTrailFacing=Rotation.UnrotateVector(Normal);
	}
	for (int32 I=Visual.GroundTrailSegments.Num()-1;I>=0;--I)
	{
		const auto& Piece=Visual.GroundTrailSegments[I];
		if (!bEnabled || bExpired || Piece.Index<First || Piece.Index>Last)
		{
			Retire(Piece.Component,0); Visual.GroundTrailSegments.RemoveAtSwap(I,1,EAllowShrinking::No);
		}
	}
	if (!bEnabled || bExpired || Distance<=1) return bExpired;
	// At most two fixed spatial pieces cover the bounded recent trail. No component
	// follows the bullet: reveal/fade advances over immutable world-space geometry.
	for (int32 Index=First;Index<=Last;++Index)
	{
		const FVector Anchor=Origin+Direction*(Index*ChunkLength);
		FBox Bounds(ForceInit); Bounds+=Anchor; Bounds+=Anchor+Direction*ChunkLength; Bounds=Bounds.ExpandBy(Canvas);
		int32 Found=Visual.GroundTrailSegments.IndexOfByPredicate([Index](const auto& Piece){return Piece.Index==Index;});
		if (!IsVisibleBounds(Bounds))
		{
			if (Found!=INDEX_NONE) { Retire(Visual.GroundTrailSegments[Found].Component,0); Visual.GroundTrailSegments.RemoveAtSwap(Found); }
			continue;
		}
		const bool bNew=Found==INDEX_NONE;
		if (bNew)
		{
			UNiagaraComponent* Component=SpawnPooled(State.PlayerBulletVfxId,Anchor,1.f,0,Rotation,NAME_None,false);
			if (!Component) continue;
			Found=Visual.GroundTrailSegments.AddDefaulted();
			auto& Piece=Visual.GroundTrailSegments[Found];Piece.Index=Index;Piece.Component=Component;
			Component->SetWorldScale3D(FVector::OneVector);
			Component->SetVariableVec2(TEXT("User.TrailSize"),FVector2D(Canvas,ChunkLength));
			Component->SetVariableVec3(TEXT("User.TrailFacing"),Visual.GroundTrailFacing);
			Component->SetVariableVec4(TEXT("User.TrailStyle"),FVector4(Visual.GroundTrailWidth,Visual.GroundTrailBend,Visual.GroundTrailHold,Visual.GroundTrailFade));
			Component->SetVariableVec4(TEXT("User.TrailShape"),FVector4(ChunkLength,Canvas,Index*ChunkLength,0));
		}
		Visual.GroundTrailSegments[Found].Component->SetVariableVec4(TEXT("User.TrailData"),FVector4(ChunkLength,Distance,EndAge,Speed));
		if (bNew) Visual.GroundTrailSegments[Found].Component->Activate(true);
	}
	return bExpired;
}

void UGuLiCombatEffectPresentationSubsystem::ApplyFlightEvent(const FGuLiFlightEvent& Event)
{
	const auto& State = Event.State;
	if (!State.IsWellFormed() || !GuLiFlightWire::IsFlight(State.Kind)) return;
	if (State.MatchEpoch!=Epoch) BeginEpoch(State.MatchEpoch);
	if (State.MatchEpoch!=Epoch || Tombstones.Contains(State.EffectId)) return;
    if(Event.bInstantGroundSegment)
    {
        if(Event.bBootstrap||Visuals.Contains(State.EffectId)||ServerTime()-State.StartTime>.5f)return;
        auto& V=Visuals.Add(State.EffectId);V.State=State;V.State.Phase=EGuLiCombatEffectPhase::Finished;V.State.SampleTime=State.StartTime;V.State.Location=Event.InstantEnd;V.RenderLocation=Event.InstantEnd;V.bInstantGroundSegment=true;
        ResolveGroundTrailConfiguration(V);
        V.GroundTrailLength=FMath::Max(1.f,V.GroundTrailLength);
        if(Event.ImpactVfxId>0)if(auto* Burst=SpawnPooled(Event.ImpactVfxId,Event.InstantEnd,1.f,0)){Retire(Burst,.2f,false);++Counters.MachineGunImpactsPlayed;}
        Tombstones.Add(State.EffectId,State.Sequence);TombstoneOrder.Add(State.EffectId);
        if(TombstoneOrder.Num()>8192){for(int32 I=0;I<1024;++I)Tombstones.Remove(TombstoneOrder[I]);TombstoneOrder.RemoveAt(0,1024,EAllowShrinking::No);}
        return;
    }
	const bool Finished = State.Phase==EGuLiCombatEffectPhase::Finished;
	if (Finished && !Event.bBootstrap && ServerTime()-State.SampleTime<=.5f)
	{
		if (Event.ImpactVfxId>0)
			if (auto* Burst=SpawnPooled(Event.ImpactVfxId,State.Location,1.f,0,FVector(Event.ImpactNormal).Rotation()))
			{ Retire(Burst,Event.ImpactVfxId==GuLiVfxIds::GroundMechImpact?.2f:3.f,false); ++Counters.MachineGunImpactsPlayed; }
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
					{ Weapon->PlayFlightShot(State.EffectId,Event.Muzzle.ServerTime,Event.Muzzle.MuzzleIndex,Event.Muzzle.FrozenMuzzleVfxId); break; }
		}
		else ApplyShots({Event.Muzzle});
	}
	ApplyState(State,Event.bBootstrap,Finished && (Event.ImpactVfxId>0 || Event.bUseCatalogImpact || Event.bFrozenGroundImpact));
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
