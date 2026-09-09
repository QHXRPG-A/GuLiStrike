// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Wingman/GuLiWingmanRuntimeTypes.h"

/** UObject-free deterministic flow helpers shared by spawn, Guidance and Flight recovery. */
namespace GuLiWingmanSwarmFlow
{
	inline constexpr float FixedStepSeconds = 1.0f / 30.0f;

	GULISTRIKE_API void InitializeAgent(
		const FGuLiWingmanFormationRuntimeConfig& Formation,
		const FGuLiWingmanHandle& Handle,
		uint32 InitialSimulationTick,
		FGuLiWingmanSwarmAgentState& OutAgent);

	GULISTRIKE_API void AdvanceSimulationClock(
		float FrameDeltaSeconds,
		FGuLiWingmanSwarmAgentState& InOutAgent);

	GULISTRIKE_API FVector BuildInitialOffset(
		const FGuLiWingmanFormationRuntimeConfig& Formation,
		const FGuLiWingmanHandle& Handle,
		const FGuLiWingmanSwarmAgentState& Agent,
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
		const FGuLiWingmanSwarmAgentState& Agent,
		const FGuLiWingmanFormationRuntimeConfig& Formation,
		EGuLiWingmanFlightMode Mode);
}
