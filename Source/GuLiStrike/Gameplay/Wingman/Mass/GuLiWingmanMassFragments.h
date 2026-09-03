// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "MassEntityTypes.h"
#include "GuLiWingmanMassFragments.generated.h"

/** Stable network identity for one locally simulated entity. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanIdentityFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FGuLiWingmanHandle Handle;
};

/** Current reliable ability projection version consumed by this entity. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanAbilityFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	uint32 AbilitySetRevision = 0u;

	UPROPERTY(Transient)
	uint32 FormationCommandRevision = 0u;

	UPROPERTY(Transient)
	uint64 FormationDefinitionChecksum = 0u;
};

/** Numeric DataAsset projection used by processors; never authored locally. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanTuningFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FGuLiWingmanFormationRuntimeConfig Formation;

	UPROPERTY(Transient)
	FGuLiWingmanWeaponRuntimeConfig BasicWeapon;
};

/** The Ship canonical state copied to the client simulation cut; never authored by a processor. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanCarrierFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FTransform Transform = FTransform::Identity;

	UPROPERTY(Transient)
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(Transient)
	FGuLiCarrierSourceRef Source;
};

/** Data-driven double-ring slot. Formation processors only produce guidance. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanFormationSlotFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	float RadiusCentimeters = 60000.0f;

	UPROPERTY(Transient)
	float HeightCentimeters = 15000.0f;

	UPROPERTY(Transient)
	float PhaseRadians = 0.0f;

	UPROPERTY(Transient)
	float AngularSpeedRadiansPerSecond = 0.08f;

	UPROPERTY(Transient)
	bool bClockwise = true;
};

/** Guidance target. This fragment cannot directly change the entity Transform. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanGuidanceFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FVector DesiredPosition = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector DesiredForward = FVector::ForwardVector;

	UPROPERTY(Transient)
	float DesiredSpeedCentimetersPerSecond = 4500.0f;
};

/**
 * Read-only input consumed by FormationGuidance while a Flight is catching up
 * or recovering. The group behavior runner is the only writer: it publishes
 * one waypoint cut from an asynchronously produced, smoothed FlightNav path.
 *
 * This fragment deliberately contains no path container and no UObject. All
 * five members of a Flight receive the same immutable waypoint/goal cut, then
 * preserve their formation-relative offset in the guidance processor.
 */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanNavigationGuidanceFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FVector Waypoint = FVector::ZeroVector;

	UPROPERTY(Transient)
	FVector PathGoal = FVector::ZeroVector;

	UPROPERTY(Transient)
	uint32 AbilitySetRevision = 0u;

	UPROPERTY(Transient)
	uint32 FormationCommandRevision = 0u;

	UPROPERTY(Transient)
	uint32 RequestSerial = 0u;

	UPROPERTY(Transient)
	uint16 PathPointIndex = 0u;

	UPROPERTY(Transient)
	bool bHasPath = false;

	/** True means async navigation failed safely and formation/local sweep remain authoritative. */
	UPROPERTY(Transient)
	bool bUsingSafeFallback = false;
};

/** Separation/obstacle acceleration generated independently of flight integration. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanAvoidanceFragment : public FMassFragment
{
	GENERATED_BODY()

	/**
	 * Steering acceleration derived only from an immediately sweep-safe heading.
	 * Normal formation convergence/separation can keep this non-zero; it is not
	 * an emergency-state signal by itself.
	 */
	UPROPERTY(Transient)
	FVector Acceleration = FVector::ZeroVector;

	/** Feasible heading selected by obstacle probes; Integration remains the sole Transform writer. */
	UPROPERTY(Transient)
	FVector SafeDirection = FVector::ForwardVector;

	/** Last FlightNav waypoint (or current baked-nav point) validated from the current position. */
	UPROPERTY(Transient)
	FVector LastVerifiedSafePoint = FVector::ZeroVector;

	/** 3D uniform-grid cell occupied during the last avoidance evaluation. */
	UPROPERTY(Transient)
	FIntVector SpatialCell = FIntVector::ZeroValue;

	/** Group-scoped grid size used to produce SpatialCell. */
	UPROPERTY(Transient)
	float SpatialCellSizeCentimeters = 0.0f;

	/** Consecutive time without a heading clear for the complete forward look-ahead. */
	UPROPERTY(Transient)
	float ConsecutiveBlockedSeconds = 0.0f;

	/** Hysteresis timer used to leave controlled recovery only after a stable reopen. */
	UPROPERTY(Transient)
	float ConsecutiveClearSeconds = 0.0f;

	/** Bounded cadence for revalidating the cached FlightNav recovery point. */
	UPROPERTY(Transient)
	float RecoveryPointValidationAccumulator = 0.0f;

	UPROPERTY(Transient)
	uint32 VerifiedWaypointRequestSerial = 0u;

	UPROPERTY(Transient)
	uint16 VerifiedWaypointPathPointIndex = 0u;

	/** Number of same-group spatial-hash entries examined on the last evaluation. */
	UPROPERTY(Transient)
	uint16 SpatialNeighborTests = 0u;

	/** Number of neighbors inside this entity's separation radius. */
	UPROPERTY(Transient)
	uint16 SeparationNeighborCount = 0u;

	/** Number of deterministic obstacle headings swept on the last evaluation. */
	UPROPERTY(Transient)
	uint16 HeadingProbeCount = 0u;

	UPROPERTY(Transient)
	bool bHasSafeDirection = false;

	UPROPERTY(Transient)
	bool bHasVerifiedSafePoint = false;

	UPROPERTY(Transient)
	bool bRecoveryPointCurrentlyValid = false;

	UPROPERTY(Transient)
	bool bControlledRecovery = false;

	UPROPERTY(Transient)
	bool bDetectedWorldStatic = false;

	UPROPERTY(Transient)
	bool bDetectedWorldDynamic = false;

	/** At least one deterministic heading probe crossed or violated baked FlightNav. */
	UPROPERTY(Transient)
	bool bDetectedFlightNavBoundary = false;
};

/** Per-entity fixed-wing state; the integration processor is its only writer. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanFlightDynamicsFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(Transient)
	float BankDegrees = 0.0f;

	UPROPERTY(Transient)
	float FixedStepAccumulator = 0.0f;

	UPROPERTY(Transient)
	float ModeEvaluationAccumulator = 0.0f;

	UPROPERTY(Transient)
	EGuLiWingmanFlightMode Mode = EGuLiWingmanFlightMode::Orbit;

	UPROPERTY(Transient)
	bool bAlive = true;
};

/** Independent basic-weapon state. A shot is not a GA activation. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanWeaponStateFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	double NextBasicFireSeconds = 0.0;

	UPROPERTY(Transient)
	uint32 DomainFireSequence = 0u;

	UPROPERTY(Transient)
	FGuLiTargetHandle Target;

	UPROPERTY(Transient)
	FGuLiAcceptedStateRef SourceAcceptedState;

	/** Ability projection that authorized SourceAcceptedState. Prevents an old pose cut firing after a config switch. */
	UPROPERTY(Transient)
	uint32 SourceAcceptedAbilitySetRevision = 0u;

	UPROPERTY(Transient)
	uint32 SourceAcceptedFormationCommandRevision = 0u;

	UPROPERTY(Transient)
	uint64 SourceAcceptedFormationDefinitionChecksum = 0u;
};

/** Owner entities run Guidance/Avoidance/Integration only on Client/Standalone execution flags. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanOwnerMassTag : public FMassTag
{
	GENERATED_BODY()
};

/** Remote entities are presentation mirrors and never enter owner simulation queries. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanRemoteMassTag : public FMassTag
{
	GENERATED_BODY()
};
