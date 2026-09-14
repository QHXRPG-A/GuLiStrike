#include "Gameplay/CombatEffects/GuLiProjectilePoolSubsystem.h"

#include "Gameplay/CombatEffects/GuLiCombatEffectDefinition.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Subsystems/SubsystemCollection.h"

namespace
{
	constexpr double ProjectileGridCellSize = 20000.0;
	FIntVector ProjectileGridCell(const FVector& P)
	{ return FIntVector(FMath::FloorToInt(P.X / ProjectileGridCellSize), FMath::FloorToInt(P.Y / ProjectileGridCellSize), FMath::FloorToInt(P.Z / ProjectileGridCellSize)); }
	// Earliest contact of two swept spheres, expressed in relative coordinates.
	bool SphereContact(const FVector& RelativeStart, const FVector& RelativeDelta, double Radius, double& Alpha)
	{
		const double C = RelativeStart.SizeSquared() - FMath::Square(Radius);
		if (C <= 0) { Alpha = 0; return true; }
		const double A = RelativeDelta.SizeSquared();
		if (A <= UE_SMALL_NUMBER) return false;
		const double B = FVector::DotProduct(RelativeStart, RelativeDelta);
		const double Discriminant = B * B - A * C;
		if (B >= 0 || Discriminant < 0) return false;
		Alpha = (-B - FMath::Sqrt(Discriminant)) / A;
		return Alpha >= 0 && Alpha <= 1;
	}
}

bool UGuLiProjectilePoolSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

void UGuLiProjectilePoolSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGuLiDamageLedgerSubsystem>();
	Ledger = GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	Grow(FMath::Max(1, GetDefault<UGuLiCombatEffectSettings>()->ProjectilePoolInitialCapacity));
	Stats.Expansions = 0;
}

void UGuLiProjectilePoolSubsystem::Grow(const int32 Count)
{
	const int32 Begin = Slots.Num();
	Slots.AddDefaulted(Count);
	FreeSlots.Reserve(Slots.Num()); ActiveSlots.Reserve(Slots.Num()); StepHandles.Reserve(Slots.Num());
	for (int32 Index = Slots.Num() - 1; Index >= Begin; --Index) FreeSlots.Add(Index);
	Stats.Capacity = Slots.Num(); ++Stats.Expansions;
}

void UGuLiProjectilePoolSubsystem::Clear()
{
	for (const int32 Index : ActiveSlots)
	{
		if (Ledger) Ledger->ReleaseEffectSource(Slots[Index].SourceLease);
		const uint32 Generation = Slots[Index].Generation;
		Slots[Index] = {}; Slots[Index].Generation = Generation;
	}
	ActiveSlots.Reset(); FreeSlots.Reset(Slots.Num()); ById.Reset(); StepHandles.Reset();
	for (int32 Index = Slots.Num() - 1; Index >= 0; --Index) FreeSlots.Add(Index);
	PreviousTargets.Reset(); Targets.Reset(); Snapshots.Reset(); SpatialGrid.Reset(); Candidates.Reset();
	PreviousSnapshotTime = 0; Stats = {}; Stats.Capacity = Slots.Num();
}

void UGuLiProjectilePoolSubsystem::BeginEpoch(const uint32 NewEpoch)
{
	if (!Ledger || NewEpoch == 0 || NewEpoch <= Epoch || Ledger->GetMatchEpoch() != NewEpoch) return;
	Clear(); Epoch = NewEpoch;
}

void UGuLiProjectilePoolSubsystem::Deinitialize()
{
	Clear(); OnState.Clear(); Slots.Reset(); FreeSlots.Reset(); Ledger = nullptr;
	Super::Deinitialize();
}

bool UGuLiProjectilePoolSubsystem::Matches(const FGuLiProjectilePoolHandle Handle) const
{
	return Handle.IsValid() && Handle.MatchEpoch == Epoch && Slots.IsValidIndex(Handle.Slot)
		&& Slots[Handle.Slot].Generation == Handle.Generation && Slots[Handle.Slot].ActiveIndex != INDEX_NONE;
}

FGuLiProjectilePoolHandle UGuLiProjectilePoolSubsystem::Launch(const FGuLiPooledProjectileLaunch& R)
{
	if (!Ledger || !GetWorld() || GetWorld()->GetNetMode() == NM_Client) return {};
	BeginEpoch(Ledger->GetMatchEpoch());
	if (Epoch == 0 || R.Context.MatchEpoch != Epoch || !R.Context.ShotId.IsValid()
		|| !R.Context.Emitter.IsValid() || !R.Context.Source.IsValid() || ById.Contains(R.Context.ShotId)
		|| R.Position.ContainsNaN() || R.Direction.ContainsNaN() || R.Direction.IsNearlyZero() || R.MuzzleOffset.ContainsNaN()
		|| !FMath::IsFinite(R.Speed) || R.Speed <= 0 || R.Speed > 1000000
		|| !FMath::IsFinite(R.Lifetime) || R.Lifetime < 0.01f || R.Lifetime > 120
		|| !FMath::IsFinite(R.MaximumDistance) || R.MaximumDistance <= 0
		|| !FMath::IsFinite(R.SweepRadius) || R.SweepRadius < 0 || R.SweepRadius > 10000
		|| !FMath::IsFinite(R.ServerTime) || !FMath::IsFinite(R.Context.Damage) || R.Context.Damage <= 0) return {};
	FGuLiCombatTargetSnapshot Source;
	if (!Ledger->TryGetTargetSnapshot(R.Context.Source, Source) || !Source.bAlive
		|| (Source.Team != EGuLiTeam::Red && Source.Team != EGuLiTeam::Blue)) return {};
	FGuid Lease = R.SourceLease;
	if (Lease.IsValid()) { if (!Ledger->RetainEffectSource(Lease)) return {}; }
	else Lease = Ledger->AcquireEffectSource(R.Context.Source);
	if (!Lease.IsValid()) return {};
	if (FreeSlots.IsEmpty()) Grow(FMath::Max(1, GetDefault<UGuLiCombatEffectSettings>()->ProjectilePoolGrowthSize));
	const int32 Index = FreeSlots.Pop(EAllowShrinking::No);
	FSlot& Slot = Slots[Index];
	Slot.Generation = Slot.Generation == MAX_uint32 ? 1 : Slot.Generation + 1;
	Slot.ActiveIndex = ActiveSlots.Add(Index); Slot.SourceLease = Lease; Slot.Context = R.Context; Slot.SweepRadius = R.SweepRadius;
	FGuLiCombatEffectState& State = Slot.State;
	State = {}; State.Kind = EGuLiCombatEffectKind::LinearProjectile;
	State.EffectId = R.Context.ShotId; State.MatchEpoch = Epoch; State.Sequence = 1;
	State.Source = GuLiCombatTargets::MakeWingmanTargetHandle(R.Context.Emitter); State.SourceTeam = Source.Team;
	State.LaunchLocation = State.Location = R.Position; State.LaunchDirection = R.Direction.GetSafeNormal();
	State.Motion.Speed = R.Speed; State.Velocity = FVector(State.LaunchDirection) * R.Speed;
	State.MuzzleOffset = R.MuzzleOffset;
	State.StartTime = State.SampleTime = State.ActivationTime = R.ServerTime;
	State.EndTime = R.ServerTime + FMath::Min(R.Lifetime, R.MaximumDistance / R.Speed);
	FGuLiProjectilePoolHandle Handle; Handle.Slot = Index; Handle.Generation = Slot.Generation; Handle.MatchEpoch = Epoch;
	ById.Add(State.EffectId, Handle); Stats.Active = ActiveSlots.Num();
	Stats.PeakActive = FMath::Max(Stats.PeakActive, Stats.Active); ++Stats.Launched;
	const FGuLiCombatEffectState Copy = State; OnState.Broadcast(Copy, true);
	return Handle;
}

bool UGuLiProjectilePoolSubsystem::Query(const FGuLiProjectilePoolHandle Handle, FGuLiCombatEffectState& OutState) const
{
	if (!Matches(Handle)) { OutState = {}; return false; }
	OutState = Slots[Handle.Slot].State; return true;
}

bool UGuLiProjectilePoolSubsystem::QueryById(const FGuid& Id, FGuLiCombatEffectState& OutState) const
{
	const auto* Handle = ById.Find(Id);
	if (!Handle) { OutState = {}; return false; }
	return Query(*Handle, OutState);
}

void UGuLiProjectilePoolSubsystem::AppendActiveSnapshot(TArray<FGuLiCombatEffectState>& OutStates) const
{
	for (const int32 Index : ActiveSlots) OutStates.Add(Slots[Index].State);
}

bool UGuLiProjectilePoolSubsystem::Release(const FGuLiProjectilePoolHandle Handle, const EGuLiCombatEffectEndReason Reason)
{
	if (!Matches(Handle)) return false;
	return Retire(Handle, Reason, Slots[Handle.Slot].State.Location, GetWorld()->GetTimeSeconds());
}

bool UGuLiProjectilePoolSubsystem::Retire(const FGuLiProjectilePoolHandle Handle, EGuLiCombatEffectEndReason Reason,
	const FVector& Position, const float Time, const FGuLiTargetHandle* HitTarget)
{
	if (!Matches(Handle)) return false;
	const FSlot Copy = Slots[Handle.Slot];
	// Detach before any damage/event callback. Re-entrant allocation cannot reuse this generation.
	ById.Remove(Copy.State.EffectId);
	ActiveSlots.RemoveAtSwap(Copy.ActiveIndex, 1, EAllowShrinking::No);
	if (ActiveSlots.IsValidIndex(Copy.ActiveIndex)) Slots[ActiveSlots[Copy.ActiveIndex]].ActiveIndex = Copy.ActiveIndex;
	Slots[Handle.Slot] = {}; Slots[Handle.Slot].Generation = Copy.Generation; FreeSlots.Add(Handle.Slot);
	Stats.Active = ActiveSlots.Num(); ++Stats.Recycled;
	if (HitTarget)
	{
		FGuLiDamageRequest Damage;
		Damage.MatchEpoch = Copy.Context.MatchEpoch; Damage.DamageEventId = Copy.State.EffectId;
		Damage.ShotId = Copy.Context.ShotId; Damage.RootEventId = Copy.Context.RootEventId;
		Damage.Source = Copy.Context.Source; Damage.Emitter = Copy.Context.Emitter; Damage.Target = *HitTarget;
		Damage.WeaponBinding = Copy.Context.WeaponBinding; Damage.SkillId = Copy.Context.SkillId;
		Damage.LoadoutRevision = Copy.Context.LoadoutRevision; Damage.ProfileRevision = Copy.Context.ProfileRevision;
		Damage.Damage = Copy.Context.Damage; Damage.HitLocation = Position;
		const auto Result = Ledger->CommitEffectDamage(Damage, Copy.SourceLease);
		// A death callback may synchronously start another round. Do not publish or count an old-round impact there.
		if (Handle.MatchEpoch != Epoch || Handle.MatchEpoch != Ledger->GetMatchEpoch())
		{
			Ledger->ReleaseEffectSource(Copy.SourceLease);
			return true;
		}
		if (Result.Status == EGuLiDamageCommitStatus::Committed) ++Stats.Hits;
		else Reason = EGuLiCombatEffectEndReason::Cancelled;
	}
	else if (Reason == EGuLiCombatEffectEndReason::Blocked) ++Stats.Blocked;
	Ledger->ReleaseEffectSource(Copy.SourceLease);
	FGuLiCombatEffectState Terminal = Copy.State;
	Terminal.Phase = EGuLiCombatEffectPhase::Finished; Terminal.EndReason = Reason;
	Terminal.Location = Position; Terminal.SampleTime = FMath::Max(Time, Terminal.StartTime); ++Terminal.Sequence;
	OnState.Broadcast(Terminal, true);
	return true;
}

void UGuLiProjectilePoolSubsystem::BuildSpatialIndex(const float Now)
{
	Ledger->GetTargetSnapshots(Snapshots); Targets.Reset(Snapshots.Num()); SpatialGrid.Reset();
	for (const auto& Snapshot : Snapshots)
	{
		if (!Snapshot.bAlive || Snapshot.Location.ContainsNaN() || !FMath::IsFinite(Snapshot.CollisionRadius) || Snapshot.CollisionRadius < 0) continue;
		FTargetMotion& Target = Targets.AddDefaulted_GetRef(); Target.Snapshot = Snapshot;
		const FVector* Previous = PreviousTargets.Find(Snapshot.Handle);
		Target.Previous = Previous && Now > PreviousSnapshotTime ? *Previous : Snapshot.Location;
		const FBox Box = FBox(Target.Previous.ComponentMin(Snapshot.Location), Target.Previous.ComponentMax(Snapshot.Location)).ExpandBy(Snapshot.CollisionRadius);
		const FIntVector Min = ProjectileGridCell(Box.Min), Max = ProjectileGridCell(Box.Max);
		for (int32 X = Min.X; X <= Max.X; ++X) for (int32 Y = Min.Y; Y <= Max.Y; ++Y) for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
			SpatialGrid.FindOrAdd(FIntVector(X, Y, Z)).Add(Targets.Num() - 1);
	}
}

void UGuLiProjectilePoolSubsystem::Step(const float Now)
{
	if (bStepping || !Ledger || !FMath::IsFinite(Now)) return;
	BeginEpoch(Ledger->GetMatchEpoch());
	if (Epoch == 0 || ActiveSlots.IsEmpty()) { PreviousTargets.Reset(); PreviousSnapshotTime = Now; return; }
	TGuardValue<bool> Guard(bStepping, true);
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiProjectilePool);
	const double Started = FPlatformTime::Seconds(); const uint32 StepEpoch = Epoch;
	BuildSpatialIndex(Now);
	FCollisionQueryParams WorldParams(SCENE_QUERY_STAT(GuLiPooledLaser), false);
	// Build the ignore set once per simulation step, not once per projectile.
	for (const auto& Target : Snapshots) if (Target.CollisionActor.IsValid()) WorldParams.AddIgnoredActor(Target.CollisionActor.Get());
	FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	StepHandles.Reset(ActiveSlots.Num());
	for (const int32 Index : ActiveSlots)
	{
		auto& Handle = StepHandles.AddDefaulted_GetRef(); Handle.Slot = Index; Handle.Generation = Slots[Index].Generation; Handle.MatchEpoch = Epoch;
	}
	// StepHandles is separate from the active list, which can change during damage callbacks.
	for (int32 StepIndex = 0; StepIndex < StepHandles.Num() && Epoch == StepEpoch; ++StepIndex)
	{
		const auto Handle = StepHandles[StepIndex];
		if (!Matches(Handle)) continue;
		const FSlot Copy = Slots[Handle.Slot]; const auto& State = Copy.State;
		const float EndTime = FMath::Min(Now, State.EndTime);
		if (EndTime <= State.SampleTime) continue;
		const FVector Start = State.Location;
		const FVector End = FVector(State.LaunchLocation) + FVector(State.Velocity) * (EndTime - State.StartTime);
		const FBox Box = FBox(Start.ComponentMin(End), Start.ComponentMax(End)).ExpandBy(Copy.SweepRadius);
		const FIntVector Min = ProjectileGridCell(Box.Min), Max = ProjectileGridCell(Box.Max); Candidates.Reset();
		for (int32 X = Min.X; X <= Max.X; ++X) for (int32 Y = Min.Y; Y <= Max.Y; ++Y) for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
			if (const auto* CellTargets = SpatialGrid.Find(FIntVector(X, Y, Z))) for (const int32 Index : *CellTargets) Candidates.Add(Index);
		double FirstAlpha = 2; int32 FirstTarget = INDEX_NONE;
		const float SnapshotSpan = Now - PreviousSnapshotTime;
		const float StartAlpha = SnapshotSpan > UE_SMALL_NUMBER ? FMath::Clamp((State.SampleTime - PreviousSnapshotTime) / SnapshotSpan, 0.0f, 1.0f) : 1;
		const float EndAlpha = SnapshotSpan > UE_SMALL_NUMBER ? FMath::Clamp((EndTime - PreviousSnapshotTime) / SnapshotSpan, 0.0f, 1.0f) : 1;
		for (const int32 Index : Candidates)
		{
			const auto& Candidate = Targets[Index]; const auto& Target = Candidate.Snapshot;
			if (!Target.bAlive || Target.Team == State.SourceTeam || Target.Team == EGuLiTeam::Unassigned
				|| Target.Handle == State.Source || Target.Handle == Copy.Context.Source) continue;
			++Stats.CandidateChecks;
			const FVector TargetStart = FMath::Lerp(Candidate.Previous, Target.Location, StartAlpha);
			const FVector TargetEnd = FMath::Lerp(Candidate.Previous, Target.Location, EndAlpha);
			double Alpha;
			if (SphereContact(Start - TargetStart, (End - Start) - (TargetEnd - TargetStart), Copy.SweepRadius + Target.CollisionRadius, Alpha)
				&& (Alpha < FirstAlpha || (Alpha == FirstAlpha && Index < FirstTarget)))
			{
				FGuLiCombatTargetSnapshot Fresh;
				if (Ledger->TryGetTargetSnapshot(Target.Handle, Fresh) && Fresh.bAlive && Fresh.Team == Target.Team)
				{ FirstAlpha = Alpha; FirstTarget = Index; }
			}
		}
		FHitResult Hit;
		const bool bBlocked = GetWorld()->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity, Objects,
			FCollisionShape::MakeSphere(FMath::Max(1.0f, Copy.SweepRadius)), WorldParams) && Hit.Time <= FirstAlpha;
		if (bBlocked)
		{
			Retire(Handle, EGuLiCombatEffectEndReason::Blocked, FMath::Lerp(Start, End, Hit.Time),
				FMath::Lerp(State.SampleTime, EndTime, Hit.Time));
		}
		else if (FirstTarget != INDEX_NONE)
		{
			const auto HitHandle = Targets[FirstTarget].Snapshot.Handle;
			Retire(Handle, EGuLiCombatEffectEndReason::Impact, FMath::Lerp(Start, End, FirstAlpha),
				FMath::Lerp(State.SampleTime, EndTime, FirstAlpha), &HitHandle);
		}
		else if (EndTime >= State.EndTime) Retire(Handle, EGuLiCombatEffectEndReason::Expired, End, EndTime);
		else if (Matches(Handle)) { Slots[Handle.Slot].State.Location = End; Slots[Handle.Slot].State.SampleTime = EndTime; }
	}
	if (Epoch == StepEpoch)
	{
		PreviousTargets.Reset(); PreviousTargets.Reserve(Targets.Num());
		for (const auto& Target : Targets) PreviousTargets.Add(Target.Snapshot.Handle, Target.Snapshot.Location);
		PreviousSnapshotTime = Now;
		Stats.LastStepMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
	}
}
