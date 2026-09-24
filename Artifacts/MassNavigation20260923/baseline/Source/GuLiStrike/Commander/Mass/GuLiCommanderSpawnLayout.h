// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"

/** Unprojected initial deployment, shared by authority spawning and editor resource authoring. */
struct FGuLiCommanderInitialSpawnSlot
{
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	uint16 UnitTypeId = 0;
	int32 FormationIndex = 0;
	int32 SlotIndex = 0;
	FVector Location = FVector::ZeroVector;
	float FacingYawDegrees = 0.0f;
	float RadiusCentimeters = 0.0f;
};

namespace GuLiCommanderInitialSpawn
{
	inline constexpr int32 FormationsPerTeam = 10;
	inline constexpr int32 MembersPerFormation = 25;
	inline constexpr int32 Population = 2 * FormationsPerTeam * MembersPerFormation;
	inline constexpr float MaximumProjectionCorrection = 150.0f;
	inline constexpr float ProjectionVerticalExtent = 50000.0f;
	inline constexpr float MinimumSeparation = 320.0f;
}
