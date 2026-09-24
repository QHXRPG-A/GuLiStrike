#pragma once

#include "CoreMinimal.h"
#include "Commander/Mass/GuLiCommanderSpawnLayout.h"

class UGuLiResourceMapDefinition;
class UWorld;

/** Transient read-only reservation; no gameplay actors are spawned by the editor audit. */
struct FGuLiSpawnReservation
{
	FVector2D Center;
	float Radius = 0.0f;
	FString Label;
	bool bBlocksArmy = true;
};

struct FGuLiInitialArmyAuthoring
{
	TArray<FGuLiCommanderInitialSpawnSlot> Slots;
	TArray<FGuLiSpawnReservation> Reservations;
	bool Build(UWorld& World, FString& OutError);
	bool IsOreClear(const FVector2D& Center) const;
	bool Validate(UWorld& World, const UGuLiResourceMapDefinition& Definition,
		int32& OutValidSlots, FString& OutError) const;
	FString ToJson() const;
};
