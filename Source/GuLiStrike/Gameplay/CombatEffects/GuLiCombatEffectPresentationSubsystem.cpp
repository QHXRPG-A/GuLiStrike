#include "Gameplay/CombatEffects/GuLiCombatEffectPresentationSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "NiagaraComponent.h"
#include "NiagaraDataChannel.h"
#include "NiagaraDataChannelAccessContext.h"
#include "NiagaraDataChannelAccessor.h"
#include "NiagaraDataChannelAsset.h"
#include "NiagaraDataChannelFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Subsystems/SubsystemCollection.h"

DEFINE_LOG_CATEGORY_STATIC(LogGuLiCombatEffectVisuals, Log, All);
static TAutoConsoleVariable<int32> CVarGuLiCombatEffectVisuals(TEXT("gs.CombatEffects.Visuals"), 1,
	TEXT("Local combat VFX only: 0 disables rendering without changing server combat. Used for scoped A/B acceptance."));

bool UGuLiCombatEffectPresentationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
}

void UGuLiCombatEffectPresentationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGuLiCommanderDataSubsystem>();
	CommanderData = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>();
}

bool UGuLiCombatEffectPresentationSubsystem::IsTickable() const { return !IsTemplate() && GetWorld() && GetWorld()->IsGameWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer; }
TStatId UGuLiCombatEffectPresentationSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiCombatEffectPresentationSubsystem, STATGROUP_Tickables); }

void UGuLiCombatEffectPresentationSubsystem::Deinitialize()
{
	ResetVisuals(); PoseProviders.Reset(); Catalog = nullptr; CommanderData = nullptr; Super::Deinitialize();
}

void UGuLiCombatEffectPresentationSubsystem::ResetVisuals()
{
	TArray<FGuid> Ids; Visuals.GenerateKeyArray(Ids);
	for (const FGuid& Id : Ids) RemoveVisual(Id, true);
	ResetLaserPool();
	if (Gunfire) { Gunfire->DeactivateImmediate(); Gunfire->ReleaseToPool(); Gunfire = nullptr; }
	for (const auto& Item : Retiring) if (IsValid(Item.Component)) { Item.Component->DeactivateImmediate(); Item.Component->ReleaseToPool(); }
	Retiring.Reset(); PendingShots.Reset(); ActiveMuzzles.Reset(); SeenShots.Reset(); ShotOrder.Reset(); Tombstones.Reset(); TombstoneOrder.Reset();
	GunfireBounds = FBox(ForceInit);
	PreviousGunfireBounds = FBox(ForceInit);
	GunfireBoundsResetTime = 0.0f;
	NextMuzzleRefreshTime = 0.0f;
}

void UGuLiCombatEffectPresentationSubsystem::BeginEpoch(uint32 NewEpoch)
{
	// Match epochs are monotonic in the existing battle protocol. A late property/RPC cannot roll back a newer epoch.
	if (NewEpoch == 0 || NewEpoch <= Epoch) return;
	ResetVisuals(); Epoch = NewEpoch; Counters = {};
}

float UGuLiCombatEffectPresentationSubsystem::ServerTime() const
{
	const AGameStateBase* State = GetWorld()->GetGameState();
	return State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

UGuLiCombatEffectCatalog* UGuLiCombatEffectPresentationSubsystem::GetCatalog()
{
	if (!Catalog) Catalog = GetDefault<UGuLiCombatEffectSettings>()->Catalog.LoadSynchronous();
	return Catalog;
}

void UGuLiCombatEffectPresentationSubsystem::RegisterPoseResolver(EGuLiTargetKind Kind, UObject* Owner, FPoseResolver Resolver)
{
	if (Owner && Resolver) PoseProviders.Add(Kind, {Owner, MoveTemp(Resolver)});
}

void UGuLiCombatEffectPresentationSubsystem::UnregisterPoseResolver(EGuLiTargetKind Kind, const UObject* Owner)
{
	if (const auto* Provider = PoseProviders.Find(Kind); Provider && Provider->Owner.Get() == Owner) PoseProviders.Remove(Kind);
}

bool UGuLiCombatEffectPresentationSubsystem::ResolvePose(const FGuLiTargetHandle& Target, FTransform& Transform, int32& UnitTypeId) const
{
    if (Target.Kind == EGuLiTargetKind::Ship)
    {
        AActor* Actor = ShipPoseCache.FindRef(Target).Get();
        if (!Actor)
        {
            for (TActorIterator<AActor> It(GetWorld()); It; ++It)
                if (const auto* Health = It->FindComponentByClass<UGuLiCombatHealthComponent>(); Health && Health->GetTargetHandle() == Target)
                { Actor = *It; ShipPoseCache.Add(Target, Actor); break; }
        }
        if (!Actor) return false;
        const auto* Health = Actor->FindComponentByClass<UGuLiCombatHealthComponent>();
        if (!Health || !Health->IsAlive()) return false;
        Transform = Actor->GetActorTransform(); UnitTypeId = 0; return true;
    }
	const auto* Provider = PoseProviders.Find(Target.Kind);
	return Provider && Provider->Owner.IsValid() && Provider->Resolve && Provider->Resolve(Target, Transform, UnitTypeId);
}

bool UGuLiCombatEffectPresentationSubsystem::ResolveMuzzlePosition(const FGuLiCombatShotCue& Cue, FVector& Position) const
{
	FTransform SourcePose;
	int32 ResolvedUnitType = Cue.UnitTypeId;
	if (!ResolvePose(Cue.Source, SourcePose, ResolvedUnitType)) return false;
	// Authority resolved the Excel mount once and replicated that compact local point.
	// Reapplying it to the current presentation pose keeps sustained muzzle flashes attached.
	Position = SourcePose.TransformPosition(Cue.MuzzleOffset);
	return !Position.ContainsNaN();
}

bool UGuLiCombatEffectPresentationSubsystem::ResolveTargetPosition(const FGuLiCombatShotCue& Cue, FVector& Position) const
{
	FTransform TargetPose;
	int32 TargetUnitType = 0;
	if (!ResolvePose(Cue.Target, TargetPose, TargetUnitType)) return false;
	const FVector* AimOffset = CommanderData && CommanderData->IsWeaponMountCatalogValid()
		&& TargetUnitType > 0 && TargetUnitType <= MAX_uint16
		? CommanderData->FindAimOffset(static_cast<uint16>(TargetUnitType)) : nullptr;
	Position = TargetPose.TransformPosition(AimOffset ? *AimOffset : FVector::ZeroVector);
	return !Position.ContainsNaN();
}

void UGuLiCombatEffectPresentationSubsystem::ResolveShotEndpoints(const FGuLiCombatShotCue& Cue, FVector& Start, FVector& End) const
{
	FVector PresentedStart;
	FVector PresentedEnd;
	// Both ends must come from the same presentation tick. Never mix one freshly
	// resolved endpoint with the other endpoint from the accepted server cue.
	if (ResolveMuzzlePosition(Cue, PresentedStart) && ResolveTargetPosition(Cue, PresentedEnd))
	{
		Start = PresentedStart;
		End = PresentedEnd;
		return;
	}
	Start = Cue.Start;
	End = Cue.End;
}

double UGuLiCombatEffectPresentationSubsystem::ClosestLocalCameraDistanceSquared(FVector Location) const
{
	double Closest = TNumericLimits<double>::Max();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController()) continue;
		FVector Camera; FRotator Rotation; PC->GetPlayerViewPoint(Camera, Rotation);
		Closest = FMath::Min(Closest, FVector::DistSquared(Camera, Location));
	}
	return Closest;
}

bool UGuLiCombatEffectPresentationSubsystem::IsVisibleLocation(FVector Location) const
{
	return Catalog && ClosestLocalCameraDistanceSquared(Location)
		<= FMath::Square(static_cast<double>(Catalog->MaximumVisualDistance));
}

UNiagaraComponent* UGuLiCombatEffectPresentationSubsystem::SpawnPooled(
	UNiagaraSystem* System, FVector Location, float Scale, float Radius, FRotator Rotation)
{
	if (!System || CVarGuLiCombatEffectVisuals.GetValueOnGameThread() == 0) return nullptr;
	UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), System, Location,
		Rotation, FVector(Scale), false, false, ENCPoolMethod::ManualRelease, false);
	if (Component)
	{
		Component->SetCastShadow(false);
		Component->SetVariableFloat(TEXT("User.Radius"), Radius);
		Component->SetVariableLinearColor(TEXT("User.Tint"), Catalog ? Catalog->GunfireTint : FLinearColor::White);
		Component->Activate(true);
	}
	return Component;
}

void UGuLiCombatEffectPresentationSubsystem::Retire(UNiagaraComponent* Component, float Seconds, bool bDeactivate)
{
	if (!IsValid(Component)) return;
	if (bDeactivate) Component->Deactivate();
	Retiring.Add({Component, static_cast<float>(GetWorld()->GetTimeSeconds()) + FMath::Max(0.0f, Seconds)});
}

void UGuLiCombatEffectPresentationSubsystem::RemoveVisual(const FGuid& Id, bool bImmediate)
{
	FGuLiLocalCombatEffect Visual;
	if (!Visuals.RemoveAndCopyValue(Id, Visual)) return;
	if (Visual.LaserSlot != INDEX_NONE) FreeLaserSlot(Visual.LaserSlot);
	float Tail = 0.5f;
	if (UGuLiProjectileEffectDefinition* Definition = Visual.State.ProjectileDefinition.Get()) Tail = Definition->TrailFadeSeconds;
	Retire(Visual.Flight, bImmediate ? 0 : Tail);
	Retire(Visual.Waiting, bImmediate ? 0 : 0.2f);
	Retire(Visual.ActiveLoop, bImmediate ? 0 : 0.5f);
}

void UGuLiCombatEffectPresentationSubsystem::ApplyState(const FGuLiCombatEffectState& State, bool bFromSnapshot)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return;
	++Counters.ReceivedStates;
	if (!State.IsWellFormed() || State.MatchEpoch < Epoch) { ++Counters.RejectedStates; return; }
	if (State.MatchEpoch > Epoch) BeginEpoch(State.MatchEpoch);
	if (Tombstones.Contains(State.EffectId)) { ++Counters.RejectedStates; return; }
	FGuLiLocalCombatEffect* Existing = Visuals.Find(State.EffectId);
	// A snapshot batch may arrive before the live multicast in the same network frame.
	// Only the multicast authorizes a fresh one-shot; late-join snapshots never replay it.
	if (Existing && !bFromSnapshot && State.Kind == EGuLiCombatEffectKind::SpellField
		&& State.Phase != EGuLiCombatEffectPhase::Finished && Existing->bSuppressOldBurst
		&& ServerTime() - State.ActivationTime < 0.5f)
	{
		Existing->bSuppressOldBurst = false;
		Existing->bActivationPlayed = false;
	}
	if (Existing && !GuLiCombatEffects::IsNewerState(State, Existing->State)) { ++Counters.RejectedStates; return; }
	if (State.Phase == EGuLiCombatEffectPhase::Finished)
	{
		if (State.Kind == EGuLiCombatEffectKind::LinearProjectile && Existing)
		{
			Existing->State.Location = State.Location; Existing->State.Phase = State.Phase;
			Existing->State.EndReason = State.EndReason; Existing->State.SampleTime = State.SampleTime;
			Existing->LaserFadeUntil = GetWorld()->GetTimeSeconds() + 0.04f;
		}
		else RemoveVisual(State.EffectId, State.EndReason == EGuLiCombatEffectEndReason::EpochEnded);
		Tombstones.Add(State.EffectId, State.Sequence); TombstoneOrder.Add(State.EffectId);
		if (TombstoneOrder.Num() > 8192)
		{
			for (int32 Index = 0; Index < 1024; ++Index) Tombstones.Remove(TombstoneOrder[Index]);
			TombstoneOrder.RemoveAt(0, 1024, EAllowShrinking::No);
		}
		return;
	}
	// Never resurrect an expired projectile/burst or replay one after a long network stall.
	if ((State.Kind == EGuLiCombatEffectKind::Projectile
		|| State.Kind == EGuLiCombatEffectKind::LinearProjectile
		|| State.Kind == EGuLiCombatEffectKind::SustainedHitscan)
		&& ServerTime() > State.EndTime + 0.25f) return;
	GetCatalog();
	if (!Existing)
	{
		FGuLiLocalCombatEffect Visual; Visual.State = State; Visual.RenderLocation = State.Location;
		if (State.Kind == EGuLiCombatEffectKind::LinearProjectile)
		{
			Visual.LaserSlot = AllocateLaserSlot();
			if (!bFromSnapshot && ServerTime() - State.StartTime < 0.35f)
				Visual.LaserMuzzleUntil = GetWorld()->GetTimeSeconds() + (Catalog ? Catalog->LaserMuzzleSeconds : 0.05f);
		}
		if (bFromSnapshot && State.Kind == EGuLiCombatEffectKind::SustainedHitscan)
		{
			const double Elapsed = FMath::Clamp(
				static_cast<double>(ServerTime() - State.StartTime), 0.0,
				static_cast<double>(State.EndTime - State.StartTime));
			Visual.NextGunShotOrdinal = FMath::Max(0,
				FMath::FloorToInt(Elapsed * State.FireRateHz + 1.e-6) + 1);
		}
		Visual.bSuppressOldBurst = bFromSnapshot && State.Kind == EGuLiCombatEffectKind::SpellField
			&& State.Phase != EGuLiCombatEffectPhase::Waiting;
		Visuals.Add(State.EffectId, MoveTemp(Visual));
	}
	else Existing->State = State;
}

void UGuLiCombatEffectPresentationSubsystem::QueueSustainedGunfire(const float Now, const bool bEnabled)
{
	for (auto& Pair : Visuals)
	{
		FGuLiLocalCombatEffect& Visual = Pair.Value;
		const FGuLiCombatEffectState& State = Visual.State;
		if (State.Kind != EGuLiCombatEffectKind::SustainedHitscan
			|| Now < State.StartTime || Now >= State.EndTime || State.FireRateHz <= 0.0f)
		{
			continue;
		}
		const int32 LatestOrdinal = FMath::FloorToInt(
			static_cast<double>(Now - State.StartTime) * State.FireRateHz + 1.e-6);
		if (LatestOrdinal < Visual.NextGunShotOrdinal) continue;
		// A stalled or late-joining client jumps directly to the latest scheduled shot.
		// Historical tracers are deliberately never replayed.
		Visual.NextGunShotOrdinal = LatestOrdinal + 1;
		// Wingman bolts now arrive as actual pooled launch records; never also draw a full hitscan segment.
		if (State.Source.Kind == EGuLiTargetKind::Wingman) continue;
		if (!bEnabled || PendingShots.Num() >= 2048) continue;
		FGuLiCombatShotCue Cue;
		Cue.MatchEpoch = State.MatchEpoch;
		Cue.ShotId = GuLiCombatEffects::DamageId(State.EffectId, LatestOrdinal, State.Target);
		Cue.Source = State.Source;
		Cue.Target = State.Target;
		Cue.SlotId = State.SlotId;
		Cue.MuzzleOffset = State.MuzzleOffset;
		Cue.KeepAliveSeconds = FMath::Clamp(0.75f / State.FireRateHz, 0.01f, 0.15f);
		Cue.Start = State.Location;
		Cue.End = State.LastTargetLocation;
		Cue.ServerTime = Now;
		if (SeenShots.Contains(Cue.ShotId)) continue;
		SeenShots.Add(Cue.ShotId);
		ShotOrder.Add(Cue.ShotId);
		PendingShots.Add(Cue);
		const FActiveMuzzleKey Key{Cue.Source, Cue.SlotId, 0};
		FActiveMuzzleVisual& Muzzle = ActiveMuzzles.FindOrAdd(Key);
		Muzzle.Cue = Cue;
		const FVector Direction = (FVector(Cue.End) - FVector(Cue.Start)).GetSafeNormal();
		if (!Direction.IsNearlyZero()) Muzzle.LastDirection = Direction;
		Muzzle.ExpireServerTime = FMath::Max(Muzzle.ExpireServerTime, Now + Cue.KeepAliveSeconds);
		++Counters.SynthesizedGunShots;
	}
	if (ShotOrder.Num() > 8192)
	{
		const int32 RemoveCount = ShotOrder.Num() - 4096;
		for (int32 Index = 0; Index < RemoveCount; ++Index) SeenShots.Remove(ShotOrder[Index]);
		ShotOrder.RemoveAt(0, RemoveCount, EAllowShrinking::No);
	}
}

void UGuLiCombatEffectPresentationSubsystem::ApplyCorrection(const FGuLiCombatEffectCorrection& Correction)
{
	// Loss or reordering may deliver a correction before the reliable create. Only
	// a create/snapshot may establish an effect; a correction can never resurrect it.
	auto* Visual = Visuals.Find(Correction.EffectId);
	if (Correction.MatchEpoch != Epoch || !Visual || Visual->State.Kind != EGuLiCombatEffectKind::Projectile
		|| Correction.Sequence <= Visual->State.Sequence || !FMath::IsFinite(Correction.SampleTime)
		|| Correction.SampleTime < Visual->State.SampleTime || Correction.Location.ContainsNaN()
		|| Correction.Velocity.ContainsNaN() || Correction.LastTargetLocation.ContainsNaN()) return;
	Visual->State.Sequence=Correction.Sequence; Visual->State.SampleTime=Correction.SampleTime;
	Visual->State.Location=Correction.Location; Visual->State.Velocity=Correction.Velocity;
	Visual->State.LastTargetLocation=Correction.LastTargetLocation;
}

void UGuLiCombatEffectPresentationSubsystem::ApplyShots(const TArray<FGuLiCombatShotCue>& Cues)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return;
	const float Now = ServerTime();
	for (const auto& Cue : Cues)
	{
		if (Cue.MatchEpoch > Epoch) BeginEpoch(Cue.MatchEpoch);
		++Counters.ReceivedShots;
		if (Cue.MatchEpoch != Epoch || !Cue.ShotId.IsValid() || Cue.Start.ContainsNaN() || Cue.End.ContainsNaN()
			|| !FMath::IsFinite(Cue.ServerTime) || Now - Cue.ServerTime > 0.35f || Cue.ServerTime - Now > 0.5f
			|| SeenShots.Contains(Cue.ShotId) || PendingShots.Num() >= 2048)
		{ ++Counters.DroppedShots; continue; }
		SeenShots.Add(Cue.ShotId); ShotOrder.Add(Cue.ShotId); PendingShots.Add(Cue);
		const FActiveMuzzleKey Key{Cue.Source, Cue.SlotId, Cue.MuzzleIndex};
		FActiveMuzzleVisual& Muzzle = ActiveMuzzles.FindOrAdd(Key);
		if (!Muzzle.Cue.ShotId.IsValid() || Cue.ServerTime >= Muzzle.Cue.ServerTime)
		{
			Muzzle.Cue = Cue;
			const FVector Direction = (FVector(Cue.End) - FVector(Cue.Start)).GetSafeNormal();
			if (!Direction.IsNearlyZero()) Muzzle.LastDirection = Direction;
		}
		const float HoldSeconds = Cue.Source.Kind == EGuLiTargetKind::Wingman
            ? FMath::Clamp(Cue.KeepAliveSeconds, 0.01f, 0.15f) : (Catalog ? Catalog->MuzzleActivityHoldSeconds : 2.0f);
		Muzzle.ExpireServerTime = FMath::Max(Muzzle.ExpireServerTime, Now + HoldSeconds);
	}
	if (ShotOrder.Num() > 8192)
	{
		const int32 RemoveCount = ShotOrder.Num() - 4096;
		for (int32 Index = 0; Index < RemoveCount; ++Index) SeenShots.Remove(ShotOrder[Index]);
		ShotOrder.RemoveAt(0, RemoveCount, EAllowShrinking::No);
	}
}

void UGuLiCombatEffectPresentationSubsystem::FlushGunfire()
{
	const float ServerNow = ServerTime();
	for (auto It = ActiveMuzzles.CreateIterator(); It; ++It)
	{
		if (ServerNow > It.Value().ExpireServerTime) It.RemoveCurrent();
	}
	if (!GetCatalog()) { PendingShots.Reset(); return; }
	const float LocalNow = GetWorld()->GetTimeSeconds();
	const float MuzzleInterval = 1.0f / FMath::Max(Catalog->MuzzleRefreshRate, 1.0f);
	const bool bRefreshMuzzles = !ActiveMuzzles.IsEmpty() && LocalNow >= NextMuzzleRefreshTime;
	if (PendingShots.IsEmpty() && !bRefreshMuzzles) return;
	if (CVarGuLiCombatEffectVisuals.GetValueOnGameThread() == 0) { PendingShots.Reset(); return; }
	UNiagaraDataChannelAsset* Channel = Catalog->GunfireChannel.LoadSynchronous();
	UNiagaraSystem* System = Catalog->GunfireSystem.LoadSynchronous();
	if (!Channel || !Channel->Get() || !System)
	{
		if (!bChannelWarning) { UE_LOG(LogGuLiCombatEffectVisuals, Warning, TEXT("Gunfire System/Data Channel is missing; no placeholder renderer will be spawned.")); bChannelWarning = true; }
		Counters.DroppedShots += PendingShots.Num(); PendingShots.Reset(); return;
	}
	if (LocalNow >= GunfireBoundsResetTime)
	{
		PreviousGunfireBounds = GunfireBounds;
		GunfireBounds = FBox(ForceInit);
		GunfireBoundsResetTime = LocalNow + 0.2f;
	}
	struct FGunfireRow
	{
		FVector Position = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		float Length = 1.0f;
		float Width = 1.0f;
		float Lifetime = 0.075f;
		float VisualIntensity = 1.0f;
		float LightBrightness = 0.0f;
		float LightRadius = 0.0f;
		double CameraDistanceSquared = TNumericLimits<double>::Max();
		bool bStrobeOn = false;
		int32 Mode = 0; // 0: exact hitscan tracer, 1: persistent muzzle flame
	};
	TArray<FGunfireRow> Rows;
	Rows.Reserve(PendingShots.Num() + (bRefreshMuzzles ? ActiveMuzzles.Num() : 0));
	int32 TracerRows = 0;
	for (const FGuLiCombatShotCue& Cue : PendingShots)
	{
		FVector Start;
		FVector End;
		ResolveShotEndpoints(Cue, Start, End);
		const FVector Delta = End - Start;
		const float Length = Delta.Size();
		if (!FMath::IsFinite(Length) || Length <= 1.0f)
		{
			++Counters.DroppedShots;
			continue;
		}
		const double CameraDistanceSquared = FMath::Min(
			ClosestLocalCameraDistanceSquared(Start), ClosestLocalCameraDistanceSquared(End));
		if (CameraDistanceSquared > FMath::Square(static_cast<double>(Catalog->MaximumVisualDistance)))
		{ ++Counters.DroppedShots; continue; }
		FGunfireRow& Row = Rows.AddDefaulted_GetRef();
		Row.Position = (Start + End) * 0.5;
		Row.Direction = Delta / Length;
		Row.Length = Length;
		Row.Width = Catalog->TracerWidth;
		Row.Lifetime = Catalog->TracerLifetime;
		Row.VisualIntensity = 1.25f;
		Row.CameraDistanceSquared = CameraDistanceSquared;
		Row.Mode = 0;
		++TracerRows;
		GunfireBounds += Start;
		GunfireBounds += End;
	}
	PendingShots.Reset();
	if (bRefreshMuzzles)
	{
		NextMuzzleRefreshTime = LocalNow + MuzzleInterval;
		for (auto& Pair : ActiveMuzzles)
		{
			FActiveMuzzleVisual& Muzzle = Pair.Value;
			FVector Start = Muzzle.Cue.Start;
			ResolveMuzzlePosition(Muzzle.Cue, Start);
			FVector Target;
			if (ResolveTargetPosition(Muzzle.Cue, Target))
			{
				const FVector CurrentDirection = (Target - Start).GetSafeNormal();
				if (!CurrentDirection.IsNearlyZero()) Muzzle.LastDirection = CurrentDirection;
			}
			const double CameraDistanceSquared = ClosestLocalCameraDistanceSquared(Start);
			if (Muzzle.LastDirection.IsNearlyZero()
				|| CameraDistanceSquared > FMath::Square(static_cast<double>(Catalog->MaximumVisualDistance))) continue;
			FGunfireRow& Row = Rows.AddDefaulted_GetRef();
			Row.Direction = Muzzle.LastDirection;
			Row.Length = Catalog->MuzzleLength;
			Row.Width = Catalog->MuzzleWidth;
			Row.Lifetime = FMath::Max(Catalog->MuzzleParticleLifetime, MuzzleInterval * 1.5f);
			const float HashPhase = static_cast<float>(GetTypeHash(Pair.Key) & 0xffffu) / 65535.0f;
			const float StrobePhase = FMath::Frac(ServerNow * Catalog->MuzzleStrobeRate + HashPhase);
			Row.bStrobeOn = StrobePhase < Catalog->MuzzleStrobeDutyCycle;
			Row.VisualIntensity = Row.bStrobeOn ? 2.2f : 0.65f;
			Row.CameraDistanceSquared = CameraDistanceSquared;
			Row.Mode = 1;
			Row.Position = Start + Row.Direction * (Row.Length * 0.5f);
			GunfireBounds += Start;
			GunfireBounds += Start + Row.Direction * Row.Length;
		}
	}
	if (Rows.IsEmpty()) return;
	TArray<int32> TracerLightCandidates;
	TArray<int32> MuzzleLightCandidates;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (Rows[Index].Mode == 0) TracerLightCandidates.Add(Index);
		else if (Rows[Index].bStrobeOn) MuzzleLightCandidates.Add(Index);
	}
	auto SortNearest = [&Rows](TArray<int32>& Indices)
	{
		Indices.Sort([&Rows](const int32 Lhs, const int32 Rhs)
		{ return Rows[Lhs].CameraDistanceSquared < Rows[Rhs].CameraDistanceSquared; });
	};
	SortNearest(TracerLightCandidates);
	SortNearest(MuzzleLightCandidates);
	for (int32 Index = 0; Index < FMath::Min(Catalog->MaximumTracerLightsPerFrame, TracerLightCandidates.Num()); ++Index)
	{
		FGunfireRow& Row = Rows[TracerLightCandidates[Index]];
		Row.LightBrightness = Catalog->TracerLightBrightness;
		Row.LightRadius = Catalog->TracerLightRadius;
	}
	for (int32 Index = 0; Index < FMath::Min(Catalog->MaximumMuzzleLightsPerFrame, MuzzleLightCandidates.Num()); ++Index)
	{
		FGunfireRow& Row = Rows[MuzzleLightCandidates[Index]];
		Row.LightBrightness = Catalog->MuzzleLightBrightness;
		Row.LightRadius = Catalog->MuzzleLightRadius;
	}
	if (!Gunfire) Gunfire = SpawnPooled(System, FVector::ZeroVector, 1.0f);
	if (!Gunfire) { Counters.DroppedShots += TracerRows; return; }
	FNDCAccessContextInst Access(Channel->Get()->GetAccessContextType());
	UNiagaraDataChannelWriter* Writer = UNiagaraDataChannelLibrary::WriteToNiagaraDataChannel_WithContext(
		GetWorld(), Channel, Access, Rows.Num(), false, true, true, TEXT("GuLiCommanderGunfire"));
	if (!Writer) { Counters.DroppedShots += TracerRows; return; }
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FGunfireRow& Row = Rows[Index];
		Writer->WritePosition(TEXT("Position"), Index, Row.Position);
		Writer->WriteVector(TEXT("Direction"), Index, Row.Direction);
		Writer->WriteFloat(TEXT("Length"), Index, Row.Length);
		Writer->WriteInt(TEXT("Mode"), Index, Row.Mode);
		Writer->WriteLinearColor(TEXT("Tint"), Index, Catalog->GunfireTint);
		Writer->WriteFloat(TEXT("Lifetime"), Index, Row.Lifetime);
		Writer->WriteFloat(TEXT("Width"), Index, Row.Width);
		Writer->WriteFloat(TEXT("VisualIntensity"), Index, Row.VisualIntensity);
		Writer->WriteFloat(TEXT("LightBrightness"), Index, Row.LightBrightness);
		Writer->WriteFloat(TEXT("LightRadius"), Index, Row.LightRadius);
	}
	Gunfire->SetSystemFixedBounds((GunfireBounds + PreviousGunfireBounds).ExpandBy(500.0));
	Counters.WrittenShots += TracerRows;
}

void UGuLiCombatEffectPresentationSubsystem::UpdateField(FGuLiLocalCombatEffect& Visual, float Now)
{
	UGuLiSpellFieldDefinition* Definition = Visual.State.FieldDefinition.LoadSynchronous();
	if (!Definition) return;
	const FVector Position = Visual.State.Location;
	// Radius is frozen from SpellFields on the server. Scale every phase from the
	// authored reference radius so a table edit changes gameplay and visuals together.
	const float FieldScale = FMath::Clamp(Visual.State.Radius / FMath::Max(Definition->Radius, 1.0f), 0.05f, 20.0f);
	const bool bVisible = IsVisibleLocation(Position);
	if (Now < Visual.State.ActivationTime)
	{
		if (!Visual.Waiting && bVisible) Visual.Waiting = SpawnPooled(Definition->WaitingSystem.LoadSynchronous(), Position, FieldScale, Visual.State.Radius);
		return;
	}
	if (Visual.Waiting) { Retire(Visual.Waiting, 0.2f); Visual.Waiting = nullptr; }
	if (!Visual.bActivationPlayed)
	{
		Visual.bActivationPlayed = true;
		if (!Visual.bSuppressOldBurst && bVisible && Definition->ActivationVariants.IsValidIndex(Visual.State.VariantIndex))
		{
			const auto& Variant = Definition->ActivationVariants[Visual.State.VariantIndex];
			if (Now - Visual.State.ActivationTime < 0.5f)
			{
				FRotator Rotation = FRotator::ZeroRotator;
				if (Variant.bRandomYaw)
				{
					FRandomStream Random(Visual.State.RandomSeed ^ 0x57494e47);
					Rotation.Yaw = Random.FRandRange(-180.0f, 180.0f);
				}
				if (UNiagaraComponent* Burst = SpawnPooled(Variant.System.LoadSynchronous(), Position,
					Variant.Scale * FieldScale, Visual.State.Radius, Rotation))
				{ Retire(Burst, Variant.MaximumLifetime, false); ++Counters.BurstsPlayed; }
				for (const FGuLiEffectVisualLayer& Layer : Variant.AdditionalLayers)
				{
					if (!FMath::IsFinite(Layer.Scale) || Layer.Scale <= 0.0f) continue;
					if (UNiagaraComponent* Burst = SpawnPooled(Layer.System.LoadSynchronous(), Position,
						Layer.Scale * FieldScale, Visual.State.Radius, Rotation))
					{ Retire(Burst, Variant.MaximumLifetime, false); ++Counters.BurstsPlayed; }
				}
			}
		}
	}
	if (Now < Visual.State.EndTime && bVisible)
	{
		if (!Visual.ActiveLoop) Visual.ActiveLoop = SpawnPooled(Definition->ActiveLoopSystem.LoadSynchronous(), Position, FieldScale, Visual.State.Radius);
	}
	else if (Visual.ActiveLoop) { Retire(Visual.ActiveLoop, Definition->DissipationSeconds); Visual.ActiveLoop = nullptr; }
}

void UGuLiCombatEffectPresentationSubsystem::Tick(float DeltaTime)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return;
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCombatEffects_Presentation);
	const double Started = FPlatformTime::Seconds();
	const float Now = ServerTime();
	const bool bEnabled = CVarGuLiCombatEffectVisuals.GetValueOnGameThread() != 0;
	GetCatalog(); QueueSustainedGunfire(Now, bEnabled); FlushGunfire();
	if (!bEnabled && Gunfire) { Retire(Gunfire, 0); Gunfire = nullptr; }
	TArray<FGuid> Expired;
	for (auto& Pair : Visuals)
	{
		auto& Visual = Pair.Value;
		if (Visual.State.Kind == EGuLiCombatEffectKind::LinearProjectile)
		{
			if ((Visual.State.Phase == EGuLiCombatEffectPhase::Finished && GetWorld()->GetTimeSeconds() >= Visual.LaserFadeUntil)
				|| (Visual.State.Phase != EGuLiCombatEffectPhase::Finished && Now >= Visual.State.EndTime)) Expired.Add(Pair.Key);
			continue;
		}
		if (!bEnabled)
		{
			Retire(Visual.Flight, 0); Visual.Flight = nullptr;
			Retire(Visual.Waiting, 0); Visual.Waiting = nullptr;
			Retire(Visual.ActiveLoop, 0); Visual.ActiveLoop = nullptr;
			Visual.bActivationPlayed = Now >= Visual.State.ActivationTime;
			continue;
		}
		if (Visual.State.Kind == EGuLiCombatEffectKind::SpellField)
		{
			UpdateField(Visual, Now);
			if (Now > Visual.State.EndTime + 30.0f) Expired.Add(Pair.Key);
			continue;
		}
		if (Visual.State.Kind == EGuLiCombatEffectKind::SustainedHitscan)
		{
			if (Now > Visual.State.EndTime + 0.25f) Expired.Add(Pair.Key);
			continue;
		}
		if (Now > Visual.State.EndTime + 0.25f) { Expired.Add(Pair.Key); continue; }
		UGuLiProjectileEffectDefinition* Definition = Visual.State.ProjectileDefinition.LoadSynchronous();
		if (!Definition) continue;
		FGuLiCombatEffectState Prediction = Visual.State;
		float Remaining = FMath::Clamp(Now - Prediction.SampleTime, 0.0f, 0.3f);
		float PredictionTime = Prediction.SampleTime;
		while (Remaining > UE_SMALL_NUMBER)
		{
			const float Step = FMath::Min(Remaining, 1.0f / 30.0f); PredictionTime += Step;
			GuLiCombatEffects::AdvanceProjectile(Prediction, PredictionTime - Prediction.StartTime, Step); Remaining -= Step;
		}
		Visual.RenderLocation = FMath::VInterpTo(Visual.RenderLocation, FVector(Prediction.Location), DeltaTime, 25.0f);
		if (IsVisibleLocation(Visual.RenderLocation))
		{
			if (!Visual.Flight) Visual.Flight = SpawnPooled(Definition->FlightSystem.LoadSynchronous(), Visual.RenderLocation, Definition->VisualScale);
			if (Visual.Flight)
			{
				Visual.Flight->SetWorldLocationAndRotation(Visual.RenderLocation, FVector(Prediction.Velocity).Rotation());
				Visual.Flight->SetVariableVec3(TEXT("User.Velocity"), Prediction.Velocity);
			}
		}
		else if (Visual.Flight) { Retire(Visual.Flight, Definition->TrailFadeSeconds); Visual.Flight = nullptr; }
	}
	for (const FGuid& Id : Expired) RemoveVisual(Id, false);
	UpdateLaserPool(Now, bEnabled);
	const float LocalNow = GetWorld()->GetTimeSeconds();
	for (int32 Index = Retiring.Num() - 1; Index >= 0; --Index)
	{
		UNiagaraComponent* Component = Retiring[Index].Component;
		if (!IsValid(Component) || Component->IsComplete() || LocalNow >= Retiring[Index].ReleaseTime)
		{
			if (IsValid(Component)) { Component->DeactivateImmediate(); Component->ReleaseToPool(); }
			Retiring.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}
	Counters.LastUpdateMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
}

FGuLiCombatEffectVisualCounters UGuLiCombatEffectPresentationSubsystem::GetCounters() const
{
	FGuLiCombatEffectVisualCounters Result = Counters;
	Result.ComponentCount = IsValid(Gunfire) ? 1 : 0;
	for (const auto& Block : LaserBlocks) Result.ComponentCount += IsValid(Block.Component) ? 1 : 0;
	for (const auto& Pair : Visuals) Result.ComponentCount += (IsValid(Pair.Value.Flight) ? 1 : 0) + (IsValid(Pair.Value.Waiting) ? 1 : 0) + (IsValid(Pair.Value.ActiveLoop) ? 1 : 0);
	for (const auto& Item : Retiring) Result.ComponentCount += IsValid(Item.Component) ? 1 : 0;
	return Result;
}

TArray<FGuLiCombatEffectState> UGuLiCombatEffectPresentationSubsystem::GetEffectStates() const
{
	TArray<FGuLiCombatEffectState> Result;
	for (const auto& Pair : Visuals) Result.Add(Pair.Value.State);
	return Result;
}
