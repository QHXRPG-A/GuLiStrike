// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "Gameplay/Units/GuLiUnitMovementLayer.h"
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

	/** Immutable movement category assigned by the shared engineering attachment path. */
	UPROPERTY(Transient)
	bool bEngineering = false;
	UPROPERTY(Transient) EGuLiUnitMovementLayer MovementLayer = EGuLiUnitMovementLayer::Ground;
	UPROPERTY(Transient) float AltitudeCentimeters = 0.f;
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
	UPROPERTY(Transient) bool bPhased = false;

	UPROPERTY(Transient)
	float WreckSecondsRemaining = 0.0f;

	bool IsAlive() const;
};

/** Snapshot-driven client mirror. It intentionally carries no movement or avoidance tag. */
USTRUCT()
struct GULISTRIKE_API FGuLiClientSnapshotMirrorMassTag : public FMassTag
{
	GENERATED_BODY()
};

/** Marks the authoritative unit archetype. Client mirrors must never carry this tag. */
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
