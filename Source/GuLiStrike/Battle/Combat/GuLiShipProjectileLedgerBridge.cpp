// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Combat/GuLiShipProjectileLedgerBridge.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	bool IsStableTargetLess(const FGuLiTargetHandle& Lhs, const FGuLiTargetHandle& Rhs)
	{
		if (Lhs.Kind != Rhs.Kind)
		{
			return static_cast<uint8>(Lhs.Kind) < static_cast<uint8>(Rhs.Kind);
		}
		if (Lhs.AuthorityId != Rhs.AuthorityId)
		{
			return Lhs.AuthorityId < Rhs.AuthorityId;
		}
		if (Lhs.Generation != Rhs.Generation)
		{
			return Lhs.Generation < Rhs.Generation;
		}
		return Lhs.LocalId < Rhs.LocalId;
	}

	bool FindSegmentSphereTime(
		const FVector& SegmentStart,
		const FVector& SegmentEnd,
		const FVector& SphereCenter,
		const double CombinedRadius,
		double& OutTime)
	{
		OutTime = 0.0;
		if (SegmentStart.ContainsNaN() || SegmentEnd.ContainsNaN()
			|| SphereCenter.ContainsNaN() || !FMath::IsFinite(CombinedRadius)
			|| CombinedRadius <= 0.0)
		{
			return false;
		}

		const FVector Direction = SegmentEnd - SegmentStart;
		const FVector ToStart = SegmentStart - SphereCenter;
		const double RadiusSquared = CombinedRadius * CombinedRadius;
		const double C = FVector::DotProduct(ToStart, ToStart) - RadiusSquared;
		if (C <= 0.0)
		{
			return true;
		}

		const double A = FVector::DotProduct(Direction, Direction);
		if (A <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}
		const double B = FVector::DotProduct(ToStart, Direction);
		const double Discriminant = B * B - A * C;
		if (Discriminant < 0.0)
		{
			return false;
		}
		const double Time = (-B - FMath::Sqrt(Discriminant)) / A;
		if (Time < 0.0 || Time > 1.0)
		{
			return false;
		}
		OutTime = Time;
		return true;
	}

	FGuLiShipProjectileLedgerImpact CommitResolvedTarget(
		UGuLiDamageLedgerSubsystem& Ledger,
		const FGuLiShipProjectileLedgerContext& Context,
		const FGuLiTargetHandle& Target,
		const FVector& HitLocation,
		const float NormalizedSegmentTime)
	{
		FGuLiShipProjectileLedgerImpact Outcome;
		Outcome.Target = Target;
		Outcome.HitLocation = HitLocation;
		Outcome.NormalizedSegmentTime = NormalizedSegmentTime;
		if (!Target.IsValid() || Target == Context.Source)
		{
			Outcome.Target = FGuLiTargetHandle{};
			Outcome.Status = EGuLiShipProjectileLedgerImpactStatus::MissingTarget;
			return Outcome;
		}

		FGuLiDamageRequest Request;
		Request.MatchEpoch = Context.MatchEpoch;
		Request.DamageEventId = Context.DamageEventId;
		Request.ShotId = Context.ShotId;
		Request.Source = Context.Source;
		Request.Target = Target;
		Request.Damage = Context.Damage;
		Request.HitLocation = HitLocation;
		Outcome.CommitResult = Ledger.CommitDamage(Request);
		if (Outcome.CommitResult.Status == EGuLiDamageCommitStatus::Committed)
		{
			Outcome.Status = EGuLiShipProjectileLedgerImpactStatus::Committed;
		}
		else if (Outcome.CommitResult.Status == EGuLiDamageCommitStatus::Duplicate)
		{
			Outcome.Status = EGuLiShipProjectileLedgerImpactStatus::Duplicate;
		}
		else
		{
			Outcome.Status = EGuLiShipProjectileLedgerImpactStatus::Rejected;
		}
		return Outcome;
	}
}

bool FGuLiShipProjectileLedgerContext::IsWellFormed() const
{
	return MatchEpoch != 0u && ShotId.IsValid() && DamageEventId.IsValid()
		&& Source.IsValid() && FMath::IsFinite(Damage) && Damage > 0.0f;
}

bool GuLiShipProjectileLedger::BuildServerLaunchContext(
	const AActor& SourceActor,
	const float Damage,
	FGuLiShipProjectileLedgerContext& OutContext)
{
	OutContext = FGuLiShipProjectileLedgerContext{};
	UWorld* World = SourceActor.GetWorld();
	if (!World || !SourceActor.HasAuthority() || World->GetNetMode() == NM_Client
		|| !FMath::IsFinite(Damage) || Damage <= 0.0f)
	{
		return false;
	}

	const UGuLiCombatHealthComponent* SourceHealth =
		SourceActor.FindComponentByClass<UGuLiCombatHealthComponent>();
	UGuLiDamageLedgerSubsystem* Ledger = World->GetSubsystem<UGuLiDamageLedgerSubsystem>();
	if (!SourceHealth || !Ledger || Ledger->GetMatchEpoch() == 0u
		|| !SourceHealth->GetTargetHandle().IsValid())
	{
		return false;
	}

	// Require the source identity to be live in the common target directory. This
	// prevents a projectile from capturing a half-configured Ship component.
	FGuLiCombatTargetSnapshot SourceSnapshot;
	if (!Ledger->TryGetTargetSnapshot(SourceHealth->GetTargetHandle(), SourceSnapshot)
		|| !SourceSnapshot.bAlive || SourceSnapshot.CollisionActor.Get() != &SourceActor)
	{
		return false;
	}

	OutContext.MatchEpoch = Ledger->GetMatchEpoch();
	OutContext.ShotId = FGuid::NewGuid();
	OutContext.DamageEventId = FGuid::NewGuid();
	OutContext.Source = SourceHealth->GetTargetHandle();
	OutContext.Damage = Damage;
	return OutContext.IsWellFormed();
}

bool GuLiShipProjectileLedger::TryResolveServerTarget(
	UWorld& World,
	const AActor& HitActor,
	FGuLiTargetHandle& OutTarget)
{
	OutTarget = FGuLiTargetHandle{};
	if (World.GetNetMode() == NM_Client || HitActor.GetWorld() != &World)
	{
		return false;
	}

	UGuLiDamageLedgerSubsystem* Ledger = World.GetSubsystem<UGuLiDamageLedgerSubsystem>();
	if (!Ledger)
	{
		return false;
	}

	// Actor combatants take the constant-time path.
	if (const UGuLiCombatHealthComponent* Health =
		HitActor.FindComponentByClass<UGuLiCombatHealthComponent>())
	{
		FGuLiCombatTargetSnapshot Snapshot;
		const FGuLiTargetHandle Candidate = Health->GetTargetHandle();
		if (Candidate.IsValid() && Ledger->TryGetTargetSnapshot(Candidate, Snapshot)
			&& Snapshot.CollisionActor.Get() == &HitActor)
		{
			OutTarget = Candidate;
			return true;
		}
	}

	// Other adapters may expose an Actor collision proxy without owning a Health
	// Component. Resolve those through the stable target directory.
	TArray<FGuLiCombatTargetSnapshot> Snapshots;
	Ledger->GetTargetSnapshots(Snapshots);
	for (const FGuLiCombatTargetSnapshot& Snapshot : Snapshots)
	{
		if (Snapshot.Handle.IsValid() && Snapshot.CollisionActor.Get() == &HitActor)
		{
			OutTarget = Snapshot.Handle;
			return true;
		}
	}
	return false;
}

FGuLiShipProjectileLedgerImpact GuLiShipProjectileLedger::CommitServerImpact(
	UWorld& World,
	const FGuLiShipProjectileLedgerContext& Context,
	const AActor& HitActor,
	const FVector& HitLocation)
{
	FGuLiShipProjectileLedgerImpact Outcome;
	if (World.GetNetMode() == NM_Client || !Context.IsWellFormed()
		|| HitActor.GetWorld() != &World || HitLocation.ContainsNaN())
	{
		return Outcome;
	}

	UGuLiDamageLedgerSubsystem* Ledger = World.GetSubsystem<UGuLiDamageLedgerSubsystem>();
	if (!Ledger)
	{
		Outcome.Status = EGuLiShipProjectileLedgerImpactStatus::MissingLedger;
		return Outcome;
	}
	if (!TryResolveServerTarget(World, HitActor, Outcome.Target)
		|| Outcome.Target == Context.Source)
	{
		Outcome.Target = FGuLiTargetHandle{};
		Outcome.Status = EGuLiShipProjectileLedgerImpactStatus::MissingTarget;
		return Outcome;
	}
	return CommitResolvedTarget(*Ledger, Context, Outcome.Target, HitLocation, 1.0f);
}

FGuLiShipProjectileLedgerImpact GuLiShipProjectileLedger::CommitServerWingmanSweepImpact(
	UWorld& World,
	const FGuLiShipProjectileLedgerContext& Context,
	const FVector& SegmentStart,
	const FVector& SegmentEnd,
	const float ProjectileRadius)
{
	FGuLiShipProjectileLedgerImpact Outcome;
	if (World.GetNetMode() == NM_Client || !Context.IsWellFormed()
		|| SegmentStart.ContainsNaN() || SegmentEnd.ContainsNaN()
		|| !FMath::IsFinite(ProjectileRadius) || ProjectileRadius < 0.0f)
	{
		return Outcome;
	}

	UGuLiDamageLedgerSubsystem* Ledger = World.GetSubsystem<UGuLiDamageLedgerSubsystem>();
	if (!Ledger)
	{
		Outcome.Status = EGuLiShipProjectileLedgerImpactStatus::MissingLedger;
		return Outcome;
	}

	TArray<FGuLiCombatTargetSnapshot> Snapshots;
	Ledger->GetTargetSnapshots(Snapshots);
	const FGuLiCombatTargetSnapshot* BestTarget = nullptr;
	double BestTime = TNumericLimits<double>::Max();
	for (const FGuLiCombatTargetSnapshot& Snapshot : Snapshots)
	{
		if (Snapshot.Handle.Kind != EGuLiTargetKind::Wingman
			|| Snapshot.Handle == Context.Source || !Snapshot.bAlive
			|| Snapshot.Location.ContainsNaN()
			|| !FMath::IsFinite(Snapshot.CollisionRadius)
			|| Snapshot.CollisionRadius <= 0.0f)
		{
			continue;
		}

		double CandidateTime = 0.0;
		if (!FindSegmentSphereTime(
			SegmentStart,
			SegmentEnd,
			Snapshot.Location,
			static_cast<double>(ProjectileRadius + Snapshot.CollisionRadius),
			CandidateTime))
		{
			continue;
		}
		if (!BestTarget || CandidateTime < BestTime - UE_DOUBLE_KINDA_SMALL_NUMBER
			|| (FMath::IsNearlyEqual(CandidateTime, BestTime)
				&& IsStableTargetLess(Snapshot.Handle, BestTarget->Handle)))
		{
			BestTarget = &Snapshot;
			BestTime = CandidateTime;
		}
	}

	if (!BestTarget)
	{
		Outcome.Status = EGuLiShipProjectileLedgerImpactStatus::MissingTarget;
		return Outcome;
	}
	const FVector HitLocation = FMath::Lerp(SegmentStart, SegmentEnd, BestTime);
	return CommitResolvedTarget(
		*Ledger,
		Context,
		BestTarget->Handle,
		HitLocation,
		static_cast<float>(BestTime));
}
