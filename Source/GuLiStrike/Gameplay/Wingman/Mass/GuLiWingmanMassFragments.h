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

	UPROPERTY(Transient)
	FName WingmanTypeId = TEXT("DefaultWingman");
};

/** Current reliable ability projection version consumed by this entity. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanAbilityFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	uint32 AbilitySetRevision = 0u;

	UPROPERTY(Transient)
	uint32 LoadoutRevision = 0u;

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
	float RadiusCentimeters = 30000.0f;

	UPROPERTY(Transient)
	float HeightCentimeters = 7500.0f;

	UPROPERTY(Transient)
	float PhaseRadians = 0.0f;

	UPROPERTY(Transient)
	float AngularSpeedRadiansPerSecond = 0.08f;

	UPROPERTY(Transient)
	bool bClockwise = true;
};

/** Stable per-member state for the v1 deterministic SwarmOrbit flow field. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanSwarmAgentFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	uint32 FlightSeed = 1u;

	UPROPERTY(Transient)
	uint32 AgentSeed = 1u;

	/** Simulation time is an integer protocol tick; wall-clock time is never sampled. */
	UPROPERTY(Transient)
	uint32 FlowSimulationTick = 1u;

	UPROPERTY(Transient)
	float FlowStepAccumulator = 0.0f;

	UPROPERTY(Transient)
	float PreferredRadiusBaseCentimeters = 36000.0f;

	UPROPERTY(Transient)
	float PreferredVerticalBiasCentimeters = 0.0f;

	UPROPERTY(Transient)
	FVector BaseOrbitAxis = FVector::UpVector;

	UPROPERTY(Transient)
	FVector NoiseDomainOffset = FVector::ZeroVector;

	UPROPERTY(Transient)
	float SwirlSign = 1.0f;
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

	/** Canonical SwarmOrbit output consumed directly by safety and integration. */
	UPROPERTY(Transient)
	FVector PreferredVelocity = FVector::ZeroVector;

	UPROPERTY(Transient)
	float DesiredSpeedCentimetersPerSecond = 4500.0f;

	UPROPERTY(Transient)
	bool bUsesVelocityField = false;

	/** Attack steering keeps physical separation, but does not align with the flock. */
	UPROPERTY(Transient)
	bool bAttackGuidance = false;
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

	/** Sweep-safe heading reachable within Integration's next fixed step. */
	UPROPERTY(Transient)
	FVector NextStepSafeDirection = FVector::ForwardVector;

	/** Last FlightNav waypoint (or current baked-nav point) validated from the current position. */
	UPROPERTY(Transient)
	FVector LastVerifiedSafePoint = FVector::ZeroVector;

	/**
	 * Stable heading captured when the current finite-turn path first becomes
	 * non-executable. A stopped fixed-wing member may rotate toward this heading
	 * without translating; the normal next-step FlightNav gate still decides when
	 * translation may resume.
	 */
	UPROPERTY(Transient)
	FVector RecoveryEscapeDirection = FVector::ForwardVector;

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

	/**
	 * Maximum speed that can still decelerate before the currently measured
	 * FlightNav/physical boundary. Zero is a valid emergency-stop limit.
	 */
	UPROPERTY(Transient)
	float NavigationSpeedLimitCentimetersPerSecond = 0.0f;

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

	/** True only when NextStepSafeDirection obeys the next fixed-step turn limit. */
	UPROPERTY(Transient)
	bool bHasNextStepSafeDirection = false;

	/** True when Integration must obey NavigationSpeedLimitCentimetersPerSecond. */
	UPROPERTY(Transient)
	bool bHasNavigationSpeedLimit = false;

	UPROPERTY(Transient)
	bool bHasVerifiedSafePoint = false;

	UPROPERTY(Transient)
	bool bHasRecoveryEscapeDirection = false;

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
	// Pose capture reads the Mass clock after integration, independent of actor tick cadence.
	uint32 CaptureSimulationTick = 1;

	UPROPERTY(Transient)
	float ModeEvaluationAccumulator = 0.0f;

	UPROPERTY(Transient)
	EGuLiWingmanFlightMode Mode = EGuLiWingmanFlightMode::Orbit;

	UPROPERTY(Transient)
	bool bAlive = true;
};

/** Attack guidance has no authority to change the Transform or bypass avoidance. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanAttackFragment : public FMassFragment
{
	GENERATED_BODY()
	EGuLiWingmanAttackPhase Phase = EGuLiWingmanAttackPhase::Idle;
	FGuLiWingmanGroundRunPath Path;
	FGuLiWingmanAirTurnPlan AirTurn;
	FVector RetreatPoint = FVector::ZeroVector;
	FVector RetreatOrigin = FVector::ZeroVector;
	FGuLiWingmanAttackTarget Target;
	FName SlotId;
	FName SkillId;
	uint64 DefinitionChecksum = 0;
	uint32 EntityGeneration = 0;
	uint32 ProfileRevision = 0;
	uint32 LeaseEpoch = 0;
	uint32 RunId = 0;
	uint32 AirStateEntrySerial = 0;
	int32 NextShotIndex = 0;
	double StartTime = 0;
	double RetryAfter = 0;
	uint8 LastCancelReason = 0;
	bool bAirTurnUsingDirectGuidance = false;
	FVector PreferredVelocity = FVector::ZeroVector;
	bool bGuiding = false;
};

/** Independent basic-weapon state. A shot is not a GA activation. */
USTRUCT()
struct GULISTRIKE_API FGuLiWingmanWeaponStateFragment : public FMassFragment
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	double NextBasicFireSeconds = 0.0;

	/** Same member, independent binding-slot cadence; fixed layout keeps this a valid Mass fragment. */
	TStaticArray<FName, GULI_MAX_WINGMAN_WEAPON_CHANNELS> WeaponSlotIds{};
	TStaticArray<double, GULI_MAX_WINGMAN_WEAPON_CHANNELS> NextFireSecondsByWeaponSlot{};

	double GetNextFireSeconds(FName SlotId) const
	{
		for (int32 Index = 0; Index < GULI_MAX_WINGMAN_WEAPON_CHANNELS; ++Index)
		{
			if (WeaponSlotIds[Index] == SlotId) return NextFireSecondsByWeaponSlot[Index];
		}
		return 0.0;
	}

	double* FindNextFireSeconds(FName SlotId)
	{
		for (int32 Index = 0; Index < GULI_MAX_WINGMAN_WEAPON_CHANNELS; ++Index)
		{
			if (WeaponSlotIds[Index] == SlotId) return &NextFireSecondsByWeaponSlot[Index];
		}
		return nullptr;
	}

	bool SetNextFireSeconds(FName SlotId, double Value)
	{
		if (SlotId.IsNone() || !FMath::IsFinite(Value)) return false;
		for (int32 Index = 0; Index < GULI_MAX_WINGMAN_WEAPON_CHANNELS; ++Index)
		{
			if (WeaponSlotIds[Index] == SlotId || WeaponSlotIds[Index].IsNone())
			{
				WeaponSlotIds[Index] = SlotId;
				NextFireSecondsByWeaponSlot[Index] = Value;
				return true;
			}
		}
		return false;
	}

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
