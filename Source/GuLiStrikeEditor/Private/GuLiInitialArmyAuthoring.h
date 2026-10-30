#pragma once

#include "CoreMinimal.h"
#include "Commander/Mass/GuLiCommanderSpawnLayout.h"
#include "Gameplay/Units/GuLiUnitMovementLayer.h"

class UGuLiResourceMapDefinition;
class UWorld;
class AGuLiCommanderDeploymentPoint;

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
    TMap<uint16,EGuLiUnitMovementLayer> MovementLayers;
    TMap<uint16,float> Altitudes;
	TArray<FGuLiSpawnReservation> Reservations;
	bool bUsesAuthoredDeployments = false;
	bool Build(UWorld& World, FString& OutError, int32 Population = 0,
		const TSet<const AGuLiCommanderDeploymentPoint*>* ExcludedDeployments = nullptr);
	bool IsOreClear(const FVector2D& Center) const;
	/** Deterministic formation placement for an explicit load, preserving the authored map. */
	bool PlaceLoadOnNavigation(UWorld& World, const UGuLiResourceMapDefinition& Definition, FString& OutError);
	bool Validate(UWorld& World, const UGuLiResourceMapDefinition& Definition,
		int32& OutValidSlots, FString& OutError) const;
	/** Resource exclusion hashes use only the ground layout; previews also expose movement metadata. */
	FString ToJson(bool bIncludeMovementLayer = true) const;
};
