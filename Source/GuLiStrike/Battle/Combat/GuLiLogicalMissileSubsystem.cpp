// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Combat/GuLiLogicalMissileSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Stats/Stats.h"
#include "Subsystems/SubsystemCollection.h"

namespace
{
	constexpr float FixedStepSeconds = 1.0f / 30.0f;
	constexpr float MaximumAccumulatedSeconds = FixedStepSeconds * 4.0f;
	constexpr float CorrectionPeriodSeconds = 0.2f;
}

bool FGuLiLogicalMissileLaunchRequest::IsWellFormed() const
{
	return MatchEpoch != 0u && MissileId.IsValid() && ShotId.IsValid() && RootEventId.IsValid()
		&& WeaponBinding.IsWellFormed() && WeaponBinding.Domain == EGuLiWeaponDomain::Wingman
		&& WeaponBinding.MatchEpoch == MatchEpoch && !SkillId.IsNone()
		&& LoadoutRevision != 0u && ProfileRevision != 0u
		&& Source.IsValid() && Emitter.IsValid() && Target.IsValid()
		&& !LaunchPosition.ContainsNaN() && !LaunchDirection.ContainsNaN()
		&& !LaunchDirection.IsNearlyZero() && FMath::IsFinite(SpeedCentimetersPerSecond)
		&& SpeedCentimetersPerSecond > 0.0f && SpeedCentimetersPerSecond <= 1000000.0f
		&& FMath::IsFinite(TurnRateDegreesPerSecond) && TurnRateDegreesPerSecond >= 0.0f
		&& TurnRateDegreesPerSecond <= 1080.0f && FMath::IsFinite(SweepRadiusCentimeters)
		&& SweepRadiusCentimeters >= 0.0f && SweepRadiusCentimeters <= 10000.0f
		&& FMath::IsFinite(MaximumLifetimeSeconds) && MaximumLifetimeSeconds > 0.0f
		&& MaximumLifetimeSeconds <= 120.0f && FMath::IsFinite(Damage) && Damage > 0.0f;
}

bool UGuLiLogicalMissileSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

void UGuLiLogicalMissileSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGuLiDamageLedgerSubsystem>();
	DamageLedger = GetWorld() ? GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>() : nullptr;
}

void UGuLiLogicalMissileSubsystem::Deinitialize()
{
	ActiveMissiles.Reset();
	ActiveMissileIds.Reset();
	DamageLedger = nullptr;
	FixedStepAccumulator = 0.0;
	SimulationStepCount = 0u;
	OnLaunch.Clear();
	OnCorrection.Clear();
	OnFinished.Clear();
	Super::Deinitialize();
}

void UGuLiLogicalMissileSubsystem::Tick(const float DeltaTime)
{
	if (ActiveMissiles.IsEmpty() || !DamageLedger || !FMath::IsFinite(DeltaTime))
	{
		return;
	}
	FixedStepAccumulator = FMath::Min(
		FixedStepAccumulator + static_cast<double>(FMath::Max(0.0f, DeltaTime)),
		static_cast<double>(MaximumAccumulatedSeconds));
	while (FixedStepAccumulator >= FixedStepSeconds)
	{
		StepMissiles(FixedStepSeconds);
		FixedStepAccumulator -= FixedStepSeconds;
	}
}

TStatId UGuLiLogicalMissileSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiLogicalMissileSubsystem, STATGROUP_Tickables);
}

bool UGuLiLogicalMissileSubsystem::IsTickable() const
{
	return !IsTemplate() && !ActiveMissiles.IsEmpty();
}

bool UGuLiLogicalMissileSubsystem::LaunchMissile(const FGuLiLogicalMissileLaunchRequest& Request)
{
	if (!ValidateLaunchRequest(Request))
	{
		return false;
	}
	CreateMissileUnchecked(Request);
	const FGuLiLogicalMissileState* State = ActiveMissiles.FindByPredicate(
		[&Request](const FGuLiLogicalMissileState& Candidate)
		{
			return Candidate.MissileId == Request.MissileId;
		});
	check(State);
	const FGuLiLogicalMissileState StableEvent = *State;
	OnLaunch.Broadcast(StableEvent);
	return true;
}

bool UGuLiLogicalMissileSubsystem::ValidateLaunchRequest(
	const FGuLiLogicalMissileLaunchRequest& Request) const
{
	if (!DamageLedger || !Request.IsWellFormed()
		|| Request.MatchEpoch != DamageLedger->GetMatchEpoch()
		|| ActiveMissileIds.Contains(Request.MissileId))
	{
		return false;
	}
	FGuLiCombatTargetSnapshot SourceSnapshot;
	FGuLiCombatTargetSnapshot TargetSnapshot;
	return DamageLedger->TryGetTargetSnapshot(Request.Source, SourceSnapshot)
		&& SourceSnapshot.bAlive
		&& DamageLedger->TryGetTargetSnapshot(Request.Target, TargetSnapshot)
		&& TargetSnapshot.bAlive;
}

void UGuLiLogicalMissileSubsystem::CreateMissileUnchecked(
	const FGuLiLogicalMissileLaunchRequest& Request)
{
	FGuLiLogicalMissileState& Missile = ActiveMissiles.AddDefaulted_GetRef();
	Missile.MatchEpoch = Request.MatchEpoch;
	Missile.MissileId = Request.MissileId;
	Missile.ShotId = Request.ShotId;
	Missile.RootEventId = Request.RootEventId;
	Missile.WeaponBinding = Request.WeaponBinding;
	Missile.SkillId = Request.SkillId;
	Missile.LoadoutRevision = Request.LoadoutRevision;
	Missile.ProfileRevision = Request.ProfileRevision;
	Missile.Source = Request.Source;
	Missile.Emitter = Request.Emitter;
	Missile.Target = Request.Target;
	Missile.Position = Request.LaunchPosition;
	Missile.SpeedCentimetersPerSecond = Request.SpeedCentimetersPerSecond;
	Missile.Velocity = Request.LaunchDirection.GetSafeNormal() * Request.SpeedCentimetersPerSecond;
	Missile.TurnRateDegreesPerSecond = Request.TurnRateDegreesPerSecond;
	Missile.SweepRadiusCentimeters = Request.SweepRadiusCentimeters;
	Missile.MaximumLifetimeSeconds = Request.MaximumLifetimeSeconds;
	Missile.Damage = Request.Damage;
	ActiveMissileIds.Add(Request.MissileId);
}

bool UGuLiLogicalMissileSubsystem::CanLaunchFlightSalvo(
	const TConstArrayView<FGuLiLogicalMissileLaunchRequest> Requests) const
{
	if (!DamageLedger || Requests.IsEmpty()
		|| Requests.Num() > GULI_WINGMAN_MEMBERS_PER_FLIGHT)
	{
		return false;
	}
	TSet<FGuid> MissileIds;
	TSet<FGuLiWingmanHandle> Emitters;
	const FGuLiWingmanFlightHandle ExpectedFlight = Requests[0].Emitter.Flight;
	const FGuLiTargetHandle ExpectedTarget = Requests[0].Target;
	const FGuLiTargetHandle ExpectedSource = Requests[0].Source;
	const uint32 ExpectedEpoch = Requests[0].MatchEpoch;
	const FGuid ExpectedRootEventId = Requests[0].RootEventId;
	const FGuLiWeaponBindingKey ExpectedBinding = Requests[0].WeaponBinding;
	const FName ExpectedSkillId = Requests[0].SkillId;
	const uint32 ExpectedLoadoutRevision = Requests[0].LoadoutRevision;
	const uint32 ExpectedProfileRevision = Requests[0].ProfileRevision;
	for (const FGuLiLogicalMissileLaunchRequest& Request : Requests)
	{
		if (!ValidateLaunchRequest(Request) || Request.Emitter.Flight != ExpectedFlight
			|| Request.Target != ExpectedTarget || Request.Source != ExpectedSource
			|| Request.RootEventId != ExpectedRootEventId || !(Request.WeaponBinding == ExpectedBinding)
			|| Request.SkillId != ExpectedSkillId
			|| Request.LoadoutRevision != ExpectedLoadoutRevision
			|| Request.ProfileRevision != ExpectedProfileRevision
			|| Request.MatchEpoch != ExpectedEpoch
			|| MissileIds.Contains(Request.MissileId) || ActiveMissileIds.Contains(Request.MissileId)
			|| Emitters.Contains(Request.Emitter))
		{
			return false;
		}
		MissileIds.Add(Request.MissileId);
		Emitters.Add(Request.Emitter);
	}
	return true;
}

bool UGuLiLogicalMissileSubsystem::LaunchFlightSalvo(
	const TConstArrayView<FGuLiLogicalMissileLaunchRequest> Requests, int32& OutLaunchedCount)
{
	OutLaunchedCount = 0;
	if (!CanLaunchFlightSalvo(Requests))
	{
		return false;
	}

	// No callbacks run until every record exists. After the pure preflight there is
	// no fallible operation in this mutation block, so observers can never see a
	// partially created Flight.
	for (const FGuLiLogicalMissileLaunchRequest& Request : Requests)
	{
		CreateMissileUnchecked(Request);
		++OutLaunchedCount;
	}
	for (const FGuLiLogicalMissileLaunchRequest& Request : Requests)
	{
		const FGuLiLogicalMissileState* State = ActiveMissiles.FindByPredicate(
			[&Request](const FGuLiLogicalMissileState& Candidate)
			{
				return Candidate.MissileId == Request.MissileId;
			});
		check(State);
		const FGuLiLogicalMissileState StableEvent = *State;
		OnLaunch.Broadcast(StableEvent);
	}
	return OutLaunchedCount == Requests.Num();
}

bool UGuLiLogicalMissileSubsystem::ContainsMissile(const FGuid& MissileId) const
{
	return ActiveMissileIds.Contains(MissileId);
}

void UGuLiLogicalMissileSubsystem::StepMissiles(const float FixedDeltaSeconds)
{
	++SimulationStepCount;
	for (int32 Index = ActiveMissiles.Num() - 1; Index >= 0; --Index)
	{
		FGuLiLogicalMissileTerminalEvent Terminal;
		if (!StepMissile(ActiveMissiles[Index], FixedDeltaSeconds, Terminal))
		{
			FinishMissile(Index, Terminal);
		}
	}
}

bool UGuLiLogicalMissileSubsystem::StepMissile(
	FGuLiLogicalMissileState& Missile, const float FixedDeltaSeconds,
	FGuLiLogicalMissileTerminalEvent& OutTerminal)
{
	OutTerminal = FGuLiLogicalMissileTerminalEvent();
	OutTerminal.MatchEpoch = Missile.MatchEpoch;
	OutTerminal.MissileId = Missile.MissileId;
	OutTerminal.RootEventId = Missile.RootEventId;
	OutTerminal.WeaponBinding = Missile.WeaponBinding;
	OutTerminal.SkillId = Missile.SkillId;
	OutTerminal.ProfileRevision = Missile.ProfileRevision;
	OutTerminal.Location = Missile.Position;
	Missile.SimulationSequence = Missile.SimulationSequence == MAX_uint32
		? 1u : Missile.SimulationSequence + 1u;
	OutTerminal.SimulationSequence = Missile.SimulationSequence;
	if (!DamageLedger || DamageLedger->GetMatchEpoch() != Missile.MatchEpoch)
	{
		OutTerminal.Reason = EGuLiLogicalMissileTerminalReason::MatchEpochEnded;
		return false;
	}
	Missile.AgeSeconds += FixedDeltaSeconds;
	if (Missile.AgeSeconds > Missile.MaximumLifetimeSeconds)
	{
		OutTerminal.Reason = EGuLiLogicalMissileTerminalReason::Expired;
		return false;
	}

	FGuLiCombatTargetSnapshot TargetSnapshot;
	if (!DamageLedger->TryGetTargetSnapshot(Missile.Target, TargetSnapshot) || !TargetSnapshot.bAlive)
	{
		OutTerminal.Reason = EGuLiLogicalMissileTerminalReason::TargetLost;
		return false;
	}
	const FVector ToTarget = TargetSnapshot.Location - Missile.Position;
	if (ToTarget.IsNearlyZero())
	{
		OutTerminal.Reason = EGuLiLogicalMissileTerminalReason::Invalid;
		return false;
	}
	const FVector Direction = TurnDirection(Missile.Velocity.GetSafeNormal(), ToTarget.GetSafeNormal(),
		FMath::DegreesToRadians(Missile.TurnRateDegreesPerSecond) * FixedDeltaSeconds);
	Missile.Velocity = Direction * Missile.SpeedCentimetersPerSecond;
	const FVector Start = Missile.Position;
	const FVector End = Start + Missile.Velocity * FixedDeltaSeconds;

	bool bTargetImpact = FMath::PointDistToSegment(TargetSnapshot.Location, Start, End)
		<= FMath::Max(0.0f, TargetSnapshot.CollisionRadius) + Missile.SweepRadiusCentimeters;
	bool bBlocked = false;
	FHitResult Hit;
	if (UWorld* World = GetWorld())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GuLiLogicalMissileSweep), false);
		FGuLiCombatTargetSnapshot SourceSnapshot;
		if (DamageLedger->TryGetTargetSnapshot(Missile.Source, SourceSnapshot)
			&& SourceSnapshot.CollisionActor.IsValid())
		{
			Params.AddIgnoredActor(SourceSnapshot.CollisionActor.Get());
		}
		if (World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility,
			FCollisionShape::MakeSphere(FMath::Max(1.0f, Missile.SweepRadiusCentimeters)), Params))
		{
			bTargetImpact = TargetSnapshot.CollisionActor.IsValid()
				&& Hit.GetActor() == TargetSnapshot.CollisionActor.Get();
			bBlocked = !bTargetImpact;
		}
	}

	Missile.Position = bTargetImpact || bBlocked ? (Hit.bBlockingHit ? Hit.ImpactPoint : End) : End;
	OutTerminal.Location = Missile.Position;
	if (bBlocked)
	{
		OutTerminal.Reason = EGuLiLogicalMissileTerminalReason::Blocked;
		return false;
	}
	if (bTargetImpact)
	{
		FGuLiDamageRequest Damage;
		Damage.MatchEpoch = Missile.MatchEpoch;
		Damage.DamageEventId = Missile.MissileId;
		Damage.ShotId = Missile.ShotId;
		Damage.Source = Missile.Source;
		Damage.Emitter = Missile.Emitter;
		Damage.WeaponBinding = Missile.WeaponBinding;
		Damage.SkillId = Missile.SkillId;
		Damage.LoadoutRevision = Missile.LoadoutRevision;
		Damage.ProfileRevision = Missile.ProfileRevision;
		Damage.RootEventId = Missile.RootEventId;
		Damage.Target = Missile.Target;
		Damage.Damage = Missile.Damage;
		Damage.HitLocation = Missile.Position;
		OutTerminal.DamageResult = DamageLedger->CommitDamage(Damage);
		OutTerminal.Reason = OutTerminal.DamageResult.WasAccepted()
			? EGuLiLogicalMissileTerminalReason::Impact
			: EGuLiLogicalMissileTerminalReason::Invalid;
		return false;
	}

	Missile.CorrectionAccumulator += FixedDeltaSeconds;
	if (Missile.CorrectionAccumulator >= CorrectionPeriodSeconds)
	{
		Missile.CorrectionAccumulator = FMath::Fmod(Missile.CorrectionAccumulator, CorrectionPeriodSeconds);
		OnCorrection.Broadcast(Missile);
	}
	return true;
}

void UGuLiLogicalMissileSubsystem::FinishMissile(
	const int32 Index, const FGuLiLogicalMissileTerminalEvent& Event)
{
	if (!ActiveMissiles.IsValidIndex(Index))
	{
		return;
	}
	ActiveMissileIds.Remove(ActiveMissiles[Index].MissileId);
	ActiveMissiles.RemoveAtSwap(Index, 1, EAllowShrinking::No);
	OnFinished.Broadcast(Event);
}

FVector UGuLiLogicalMissileSubsystem::TurnDirection(
	const FVector& CurrentDirection, const FVector& DesiredDirection, const float MaximumRadians)
{
	const FVector Current = CurrentDirection.GetSafeNormal();
	const FVector Desired = DesiredDirection.GetSafeNormal();
	if (Current.IsNearlyZero())
	{
		return Desired;
	}
	if (Desired.IsNearlyZero() || MaximumRadians <= 0.0f)
	{
		return Current;
	}
	const float Dot = FMath::Clamp(static_cast<float>(FVector::DotProduct(Current, Desired)), -1.0f, 1.0f);
	const float Angle = FMath::Acos(Dot);
	if (Angle <= MaximumRadians)
	{
		return Desired;
	}
	FVector Axis = FVector::CrossProduct(Current, Desired).GetSafeNormal();
	if (Axis.IsNearlyZero())
	{
		Axis = FVector::CrossProduct(Current, FVector::UpVector).GetSafeNormal();
		if (Axis.IsNearlyZero())
		{
			Axis = FVector::RightVector;
		}
	}
	return FQuat(Axis, MaximumRadians).RotateVector(Current).GetSafeNormal();
}
