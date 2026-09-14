// Copyright Epic Games, Inc. All Rights Reserved.

// Compatibility surface retained for the established resource contract tests and authored assets.
// Runtime building transactions use IGuLiTeamEconomy and do not include the resource system.
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"

#include "Gameplay/Building/GuLiBuildingTypes.h"

int32 UGuLiResourceEconomyConfig::GetBlueBuildingCost(const EGuLiBuildingType Type) const
{
	switch (Type)
	{
	case EGuLiBuildingType::SentryTurret: return SentryTurretBlueCost;
	case EGuLiBuildingType::MissileTurret: return MissileTurretBlueCost;
	case EGuLiBuildingType::Outpost: return OutpostBlueCost;
	default: return INDEX_NONE;
	}
}
