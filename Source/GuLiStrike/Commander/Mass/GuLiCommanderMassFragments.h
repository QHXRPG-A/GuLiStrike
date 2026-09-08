// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "GuLiCommanderMassFragments.generated.h"

/** Stable per-match Soldier identity. It is independent of selection and order formations. */
USTRUCT()
struct GULISTRIKE_API FGuLiMassIdentityFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FGuLiSoldierId SoldierId;

	UPROPERTY(Transient)
	EGuLiTeam Team = EGuLiTeam::Unassigned;
};

/** Server-authoritative health and the five-second destroyed presentation window. */
USTRUCT()
struct GULISTRIKE_API FGuLiMassHealthFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	float Health = 100.0f;

	UPROPERTY(Transient)
	bool bDead = false;

	UPROPERTY(Transient)
	float WreckSecondsRemaining = 0.0f;

	bool IsAlive() const;
};

/** Authoritative base stats. Resolved skill profiles own damage, range and attack rate. */
USTRUCT()
struct GULISTRIKE_API FGuLiMassSoldierStatsFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	float MaxHealth = 100.0f;

	UPROPERTY(Transient)
	float Defense = 0.0f;

};

/** The latest accepted server order for one Soldier. */
USTRUCT()
struct GULISTRIKE_API FGuLiMassOrderFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	uint32 ActiveOrderId = 0u;

	UPROPERTY(Transient)
	uint32 OrderRevision = 0u;

	UPROPERTY(Transient)
	FVector FormationTarget = FVector::ZeroVector;

	UPROPERTY(Transient)
	bool bHasMoveTarget = false;
};

/** Elastic formation slot and its current server-computed world target. */
USTRUCT()
struct GULISTRIKE_API FGuLiMassSlotTargetFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FVector LocalOffset = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector WorldTarget = FVector::ZeroVector;
};

/** Latest project-owned predictive avoidance acceleration for staggered 10 Hz authority use. */
USTRUCT()
struct GULISTRIKE_API FGuLiMassAvoidanceOutputFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FVector Value = FVector::ZeroVector;
};

/**
 * Per-participant scheduling and diagnostics for Commander predictive avoidance.
 * Counters are cumulative for the lifetime of the Mass entity and are read only by GM diagnostics.
 */
USTRUCT()
struct GULISTRIKE_API FGuLiMassAvoidanceStateFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	uint32 LastProcessedOrderRevision = 0u;

	UPROPERTY(Transient)
	uint64 LastSolveSequence = 0u;

	UPROPERTY(Transient)
	uint64 SolveCount = 0u;

	UPROPERTY(Transient)
	uint64 ForcedSolveCount = 0u;

	UPROPERTY(Transient)
	uint64 CandidateCount = 0u;

	UPROPERTY(Transient)
	uint64 ColliderEvaluationCount = 0u;

	UPROPERTY(Transient)
	int32 MaximumObservedBucketOccupancy = 0;
};

/** Opt-in marker for server-side entities represented in the Commander predictive-avoidance grid. */
USTRUCT()
struct GULISTRIKE_API FGuLiMassAvoidanceParticipantTag : public FMassTag
{
	GENERATED_BODY()
};

/** Snapshot-driven client mirror. It intentionally carries no movement or avoidance tag. */
USTRUCT()
struct GULISTRIKE_API FGuLiClientSnapshotMirrorMassTag : public FMassTag
{
	GENERATED_BODY()
};

/** Marks the exact 500-member server archetype. Client mirrors must never carry this tag. */
USTRUCT()
struct GULISTRIKE_API FGuLiServerAuthorityMassTag : public FMassTag
{
	GENERATED_BODY()
};

/** Alternating composition tags permit safe replacement of const-shared movement values. */
USTRUCT()
struct GULISTRIKE_API FGuLiMassRuntimeTuningEvenTag : public FMassTag
{
	GENERATED_BODY()
};

USTRUCT()
struct GULISTRIKE_API FGuLiMassRuntimeTuningOddTag : public FMassTag
{
	GENERATED_BODY()
};
