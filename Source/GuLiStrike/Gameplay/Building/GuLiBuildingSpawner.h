#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"

namespace GuLiBuildings
{
	/** Ground is already projected by the caller; collision failure leaves the slot unclaimed. */
	GULISTRIKE_API AActor* Spawn(UWorld& World, int32 DefinitionId, EGuLiTeam Team,
		const FTransform& GroundTransform, const AActor& SupportingActor, int32 TerritoryIndex, EGuLiBuildingOrigin Origin,
		bool bCompleted, const FGuid& Builder = FGuid());
}
