// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Wingman/Mass/GuLiWingmanMassFragments.h"

/** UObject-free deterministic flow helpers shared by spawn, Guidance and Flight recovery. */
namespace GuLiWingmanSwarmFlow
{
	inline constexpr float FixedStepSeconds = 1.0f / 30.0f;

	GULISTRIKE_API void InitializeAgent(
		const FGuLiWingmanFormationRuntimeConfig& Formation,
		const FGuLiWingmanHandle& Handle,
		uint32 InitialSimulationTick,
		FGuLiWingmanSwarmAgentFragment& OutAgent);

	GULISTRIKE_API void AdvanceSimulationClock(
		float FrameDeltaSeconds,
		FGuLiWingmanSwarmAgentFragment& InOutAgent);

	GULISTRIKE_API FVector BuildInitialOffset(
		const FGuLiWingmanFormationRuntimeConfig& Formation,
		const FGuLiWingmanHandle& Handle,
		const FGuLiWingmanSwarmAgentFragment& Agent,
		uint32 CandidateIndex = 0u);

	/** Stable Flight-level A* goal offset; this never follows a rotating member slot. */
	GULISTRIKE_API FVector BuildFlightRecoveryOffset(
		const FGuLiWingmanFormationRuntimeConfig& Formation,
		uint8 FlightIndex);

	GULISTRIKE_API FVector BuildPreferredVelocity(
		const FVector& Position,
		const FVector& Velocity,
		const FVector& CarrierPosition,
		const FVector& CarrierVelocity,
		const FGuLiWingmanSwarmAgentFragment& Agent,
		const FGuLiWingmanFormationRuntimeConfig& Formation,
		EGuLiWingmanFlightMode Mode);
}
