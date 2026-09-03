// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"

class AActor;
class UWorld;

/**
 * Immutable server launch identity carried by one legacy Ship projectile.
 *
 * It deliberately stores stable combat handles rather than Actor pointers. The
 * two GUIDs live for the projectile's entire lifetime, so duplicate engine hit
 * callbacks can only produce one Damage Ledger commit.
 */
struct GULISTRIKE_API FGuLiShipProjectileLedgerContext
{
	uint32 MatchEpoch = 0u;
	FGuid ShotId;
	FGuid DamageEventId;
	FGuLiTargetHandle Source;
	float Damage = 0.0f;

	bool IsWellFormed() const;
};

enum class EGuLiShipProjectileLedgerImpactStatus : uint8
{
	MissingContext = 0,
	MissingLedger,
	MissingTarget,
	Committed,
	Duplicate,
	Rejected
};

struct GULISTRIKE_API FGuLiShipProjectileLedgerImpact
{
	EGuLiShipProjectileLedgerImpactStatus Status =
		EGuLiShipProjectileLedgerImpactStatus::MissingContext;
	FGuLiTargetHandle Target;
	FGuLiDamageCommitResult CommitResult;
	FVector HitLocation = FVector::ZeroVector;
	float NormalizedSegmentTime = 1.0f;

	/** A registered combat target was hit, even when the ledger rejected damage. */
	bool HasResolvedTarget() const { return Target.IsValid(); }
	bool WasAccepted() const
	{
		return Status == EGuLiShipProjectileLedgerImpactStatus::Committed
			|| Status == EGuLiShipProjectileLedgerImpactStatus::Duplicate;
	}
};

/** Narrow adapter used by the retained Actor projectile chain. */
namespace GuLiShipProjectileLedger
{
	/** Authority only. Captures the Ship TargetHandle, current epoch and one-shot IDs. */
	GULISTRIKE_API bool BuildServerLaunchContext(
		const AActor& SourceActor,
		float Damage,
		FGuLiShipProjectileLedgerContext& OutContext);

	/** Authority only. Resolves Actor collision back to the common target directory. */
	GULISTRIKE_API bool TryResolveServerTarget(
		UWorld& World,
		const AActor& HitActor,
		FGuLiTargetHandle& OutTarget);

	/** Authority only. Repeating the same context is idempotent in the Damage Ledger. */
	GULISTRIKE_API FGuLiShipProjectileLedgerImpact CommitServerImpact(
		UWorld& World,
		const FGuLiShipProjectileLedgerContext& Context,
		const AActor& HitActor,
		const FVector& HitLocation);

	/**
	 * Authority only. Sweeps one retained Ship projectile segment against the
	 * server target directory's Accepted Wingman spheres. The earliest stable
	 * time-of-impact wins, so Mass Wingmen need no collision Actor.
	 */
	GULISTRIKE_API FGuLiShipProjectileLedgerImpact CommitServerWingmanSweepImpact(
		UWorld& World,
		const FGuLiShipProjectileLedgerContext& Context,
		const FVector& SegmentStart,
		const FVector& SegmentEnd,
		float ProjectileRadius);
}
