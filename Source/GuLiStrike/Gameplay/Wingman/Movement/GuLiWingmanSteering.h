// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTypes.h"

class UWorld;
struct FGuLiWingmanAvoidanceState;

/** Deterministic, UObject-free flight steering shared by Pawn movement and tests. */
namespace GuLiWingmanSteering
{
	inline constexpr float FixedStepSeconds = 1.0f / 30.0f;
	inline constexpr int32 MaximumFixedStepsPerFrame = 4;

	GULISTRIKE_API FVector TurnDirectionToward(
		const FVector& CurrentDirection,
		const FVector& DesiredDirection,
		float MaximumRadians);

	struct GULISTRIKE_API FHeadingProbeResult
	{
		float ClearanceCentimeters = 0.0f;
		bool bFlightNavSegmentValid = true;
		bool bWorldStatic = false;
		bool bWorldDynamic = false;
	};

	struct GULISTRIKE_API FHeadingSelection
	{
		FVector Direction = FVector::ForwardVector;
		float ClearanceCentimeters = 0.0f;
		uint16 ProbeCount = 0u;
		bool bFullLookAheadSafe = false;
		bool bImmediateStepSafe = false;
		bool bEncounteredFlightNavBoundary = false;
		bool bEncounteredWorldStatic = false;
		bool bEncounteredWorldDynamic = false;
	};

	GULISTRIKE_API FHeadingSelection SelectSafeHeading(
		const FVector& CurrentDirection,
		const FVector& DesiredDirection,
		float CurrentSpeedCentimetersPerSecond,
		const FGuLiWingmanFormationRuntimeConfig& Tuning,
		TFunctionRef<FHeadingProbeResult(const FVector&, float)> Probe,
		float CandidateTurnHorizonSeconds = 0.0f);

	struct GULISTRIKE_API FRecoveryClockState
	{
		float ConsecutiveBlockedSeconds = 0.0f;
		float ConsecutiveClearSeconds = 0.0f;
		bool bControlledRecovery = false;
	};

	/** Half-second emergency entry and half-second verified movement exit. */
	GULISTRIKE_API void AdvanceRecoveryClock(
		FRecoveryClockState& State,
		float DeltaSeconds,
		bool bFullLookAheadSafe,
		bool bHasVerifiedReopenedPath);

	GULISTRIKE_API FVector BuildRecoveryOrbitDirection(
		const FVector& Position,
		const FVector& CurrentDirection,
		const FVector& SafePoint,
		float OrbitRadiusCentimeters);

#if WITH_DEV_AUTOMATION_TESTS
	using FFlightNavSegmentValidatorForTests =
		TFunction<bool(const FVector&, const FVector&, float)>;
	GULISTRIKE_API void SetFlightNavSegmentValidatorForTests(
		UWorld* TransientTestWorld,
		FFlightNavSegmentValidatorForTests Validator);
	GULISTRIKE_API void ResetFlightNavSegmentValidatorForTests();

	using FWorldObstacleSegmentProbeForTests =
		TFunction<FHeadingProbeResult(const FVector&, const FVector&, float)>;
	GULISTRIKE_API void SetWorldObstacleSegmentProbeForTests(
		UWorld* TransientTestWorld,
		FWorldObstacleSegmentProbeForTests Probe);
	GULISTRIKE_API void ResetWorldObstacleSegmentProbeForTests();
	GULISTRIKE_API void AccumulateHeadingThreatsForTests(
		FGuLiWingmanAvoidanceState& State,
		const FHeadingSelection& Selection);
#endif
}
