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
	uint8 Health = 100u;

	UPROPERTY(Transient)
	bool bDead = false;

	UPROPERTY(Transient)
	float WreckSecondsRemaining = 0.0f;

	bool IsAlive() const;
};

/** Authoritative data-bearing stats. Combat systems do not consume these yet. */
USTRUCT()
struct GULISTRIKE_API FGuLiMassSoldierStatsFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	uint8 MaxHealth = 100u;

	UPROPERTY(Transient)
	float AttackPower = 0.0f;

	UPROPERTY(Transient)
	float Defense = 0.0f;

	UPROPERTY(Transient)
	float AttackRangeCentimeters = 0.0f;
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

/** Latest Epic Mass avoidance acceleration captured once per world frame for 30 Hz authority use. */
USTRUCT()
struct GULISTRIKE_API FGuLiMassAvoidanceOutputFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FVector Value = FVector::ZeroVector;
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
