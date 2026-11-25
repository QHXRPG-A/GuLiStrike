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

}
