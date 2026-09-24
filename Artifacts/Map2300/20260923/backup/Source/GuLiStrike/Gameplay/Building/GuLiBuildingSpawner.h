#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"

namespace GuLiBuildings
{
	/** Checks sampled footprint support and the actual presentation's ramp templates. No actor is spawned. */
	GULISTRIKE_API bool ValidateGroundPlacement(UWorld& World, const FGuLiBuildingDefinition& Definition,
		const FTransform& GroundTransform, UClass* PresentationClass, const AActor* IgnoredActor, FString& OutReason);
	/** Deterministic nearby candidates, ordered by distance from the original gift slot. */
	GULISTRIKE_API TArray<FTransform> GetGiftPlacementCandidates(const FVector& OutpostGround, int32 SlotIndex);
	GULISTRIKE_API bool ProjectPlacementCandidate(UWorld& World, const FTransform& Candidate, const AActor* IgnoredActor,
		FTransform& OutGroundTransform, AActor*& OutSupportingActor);
	/** Terrain/presentation/clearance failure leaves the slot unclaimed and retains no incomplete actor. */
	GULISTRIKE_API AActor* Spawn(UWorld& World, int32 DefinitionId, EGuLiTeam Team,
		const FTransform& GroundTransform, const AActor& SupportingActor, int32 TerritoryIndex, EGuLiBuildingOrigin Origin,
		bool bCompleted, const FGuid& Builder = FGuid(), FString* OutFailure = nullptr);
}
