// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTypes.h"
#include "Gameplay/Wingman/Combat/GuLiWingmanAttackProfile.h"

/**
 * Client-only runtime data for one Wingman Pawn.
 *
 * These types deliberately have no framework-specific base class or UObject ownership.
 * AGuLiWingmanPawn owns one aggregate and the flight movement component is the
 * only code allowed to write its pose, velocity, bank and simulation clock.
 */
struct GULISTRIKE_API FGuLiWingmanIdentityState
{
	FGuLiWingmanHandle Handle;
	FName WingmanTypeId = TEXT("DefaultWingman");
};

struct GULISTRIKE_API FGuLiWingmanAbilityState
{
	uint32 AbilitySetRevision = 0u;
	uint32 LoadoutRevision = 0u;
	uint32 FormationCommandRevision = 0u;
	uint64 FormationDefinitionChecksum = 0u;
};

struct GULISTRIKE_API FGuLiWingmanTuningState
{
	FGuLiWingmanFormationRuntimeConfig Formation;
	FGuLiWingmanWeaponRuntimeConfig BasicWeapon;
};

struct GULISTRIKE_API FGuLiWingmanCarrierState
{
	FTransform Transform = FTransform::Identity;
	FVector Velocity = FVector::ZeroVector;
	FGuLiCarrierSourceRef Source;
};

struct GULISTRIKE_API FGuLiWingmanFormationSlotState
{
	float RadiusCentimeters = 6000.0f;
	float HeightCentimeters = 1500.0f;
	float PhaseRadians = 0.0f;
	float AngularSpeedRadiansPerSecond = 0.16f;
	bool bClockwise = true;
};

struct GULISTRIKE_API FGuLiWingmanSwarmAgentState
{
	uint32 FlightSeed = 1u;
	uint32 AgentSeed = 1u;
	uint32 FlowSimulationTick = 1u;
	float FlowStepAccumulator = 0.0f;
	float PreferredRadiusBaseCentimeters = 7200.0f;
	float PreferredVerticalBiasCentimeters = 0.0f;
	FVector BaseOrbitAxis = FVector::UpVector;
	FVector NoiseDomainOffset = FVector::ZeroVector;
	float SwirlSign = 1.0f;
};

struct GULISTRIKE_API FGuLiWingmanGuidanceState
{
	FVector DesiredPosition = FVector::ZeroVector;
	FVector DesiredForward = FVector::ForwardVector;
	FVector PreferredVelocity = FVector::ZeroVector;
	float DesiredSpeedCentimetersPerSecond = 1800.0f;
	bool bUsesVelocityField = false;
	bool bAttackGuidance = false;
};

struct GULISTRIKE_API FGuLiWingmanNavigationGuidanceState
{
	FVector Waypoint = FVector::ZeroVector;
	FVector PathGoal = FVector::ZeroVector;
	uint32 AbilitySetRevision = 0u;
	uint32 FormationCommandRevision = 0u;
	uint32 RequestSerial = 0u;
	uint16 PathPointIndex = 0u;
	bool bHasPath = false;
	bool bUsingSafeFallback = false;
};

struct GULISTRIKE_API FGuLiWingmanAvoidanceState
{
	FVector SafeDirection = FVector::ForwardVector;
	FVector NextStepSafeDirection = FVector::ForwardVector;
	FVector LastVerifiedSafePoint = FVector::ZeroVector;
	FVector RecoveryEscapeDirection = FVector::ForwardVector;
	float ConsecutiveBlockedSeconds = 0.0f;
	float ConsecutiveClearSeconds = 0.0f;
	float RecoveryPointValidationAccumulator = 0.0f;
	float NavigationSpeedLimitCentimetersPerSecond = 0.0f;
	float NoProgressSeconds = 0.0f;
	float NormalProgressSeconds = 0.0f;
	uint32 VerifiedWaypointRequestSerial = 0u;
	uint16 VerifiedWaypointPathPointIndex = 0u;
	uint16 HeadingProbeCount = 0u;
	bool bHasSafeDirection = false;
	bool bHasNextStepSafeDirection = false;
	bool bHasNavigationSpeedLimit = false;
	bool bHasVerifiedSafePoint = false;
	bool bHasRecoveryEscapeDirection = false;
	bool bRecoveryPointCurrentlyValid = false;
	bool bControlledRecovery = false;
	bool bAwaitingRebase = false;
	bool bRebaseRequested = false;
	bool bDetectedWorldStatic = false;
	bool bDetectedWorldDynamic = false;
	bool bDetectedFlightNavBoundary = false;
};

struct GULISTRIKE_API FGuLiWingmanFlightDynamicsState
{
	FVector Velocity = FVector::ZeroVector;
	float BankDegrees = 0.0f;
	float FixedStepAccumulator = 0.0f;
	uint32 CaptureSimulationTick = 1u;
	float ModeEvaluationAccumulator = 0.0f;
	EGuLiWingmanFlightMode Mode = EGuLiWingmanFlightMode::Orbit;
	bool bAlive = true;
	bool bStale = false;
};

struct GULISTRIKE_API FGuLiWingmanAttackRunState
{
	EGuLiWingmanAttackPhase Phase = EGuLiWingmanAttackPhase::Idle;
	FGuLiWingmanGroundRunPath Path;
	FGuLiWingmanAttackTarget Target;
	FName SlotId;
	FName SkillId;
	uint64 DefinitionChecksum = 0u;
	uint32 EntityGeneration = 0u;
	uint32 ProfileRevision = 0u;
	uint32 LeaseEpoch = 0u;
	uint32 RunId = 0u;
	uint32 CompletedGroundRuns = 0u;
	int32 NextShotIndex = 0;
	double StartTime = 0.0;
	double PhaseStartTime = 0.0;
	double RetryAfter = 0.0;
	uint8 LastCancelReason = 0u;
	uint8 GroundPathFailureMask = 0u;
	uint8 GroundNavigationFailureMask = 0u;
	FVector PreferredVelocity = FVector::ZeroVector;
	bool bGuiding = false;
};

struct GULISTRIKE_API FGuLiWingmanWeaponState
{
	double NextBasicFireSeconds = 0.0;
	TStaticArray<FName, GULI_MAX_WINGMAN_WEAPON_CHANNELS> WeaponSlotIds{};
	TStaticArray<double, GULI_MAX_WINGMAN_WEAPON_CHANNELS> NextFireSecondsByWeaponSlot{};
	uint32 DomainFireSequence = 0u;
	FGuLiTargetHandle Target;
	FGuLiAcceptedStateRef SourceAcceptedState;
	uint32 SourceAcceptedAbilitySetRevision = 0u;
	uint32 SourceAcceptedFormationCommandRevision = 0u;
	uint64 SourceAcceptedFormationDefinitionChecksum = 0u;

	double GetNextFireSeconds(const FName SlotId) const
	{
		for (int32 Index = 0; Index < GULI_MAX_WINGMAN_WEAPON_CHANNELS; ++Index)
		{
			if (WeaponSlotIds[Index] == SlotId)
			{
				return NextFireSecondsByWeaponSlot[Index];
			}
		}
		return 0.0;
	}

	double* FindNextFireSeconds(const FName SlotId)
	{
		for (int32 Index = 0; Index < GULI_MAX_WINGMAN_WEAPON_CHANNELS; ++Index)
		{
			if (WeaponSlotIds[Index] == SlotId)
			{
				return &NextFireSecondsByWeaponSlot[Index];
			}
		}
		return nullptr;
	}

	bool SetNextFireSeconds(const FName SlotId, const double Value)
	{
		if (SlotId.IsNone() || !FMath::IsFinite(Value))
		{
			return false;
		}
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
};

struct GULISTRIKE_API FGuLiWingmanRuntimeState
{
	FGuLiWingmanIdentityState Identity;
	FGuLiWingmanAbilityState Ability;
	FGuLiWingmanTuningState Tuning;
	FGuLiWingmanCarrierState Carrier;
	FGuLiWingmanFormationSlotState FormationSlot;
	FGuLiWingmanSwarmAgentState SwarmAgent;
	FGuLiWingmanGuidanceState Guidance;
	FGuLiWingmanNavigationGuidanceState Navigation;
	FGuLiWingmanAvoidanceState Avoidance;
	FGuLiWingmanFlightDynamicsState Dynamics;
	FGuLiWingmanAttackRunState Attack;
	FGuLiWingmanWeaponState Weapon;
};
