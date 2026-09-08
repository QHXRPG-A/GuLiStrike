// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Building/GuLiBuildingCatalog.h"

#include "Materials/MaterialInterface.h"

namespace GuLiBuildingCatalogPrivate
{
	constexpr TCHAR DefaultCatalogObjectPath[] =
		TEXT("/Game/GuLiStrike/Buildings/DA_GuLiBuildingCatalog.DA_GuLiBuildingCatalog");
}

const FGuLiBuildingDefinition* UGuLiBuildingCatalog::FindDefinition(
	const EGuLiBuildingType Type) const
{
	return Definitions.FindByPredicate(
		[Type](const FGuLiBuildingDefinition& Definition)
		{
			return Definition.Type == Type;
		});
}

bool UGuLiBuildingCatalog::IsUsable() const
{
	if (!PreviewMaterial || Definitions.Num() != 3)
	{
		return false;
	}

	for (const EGuLiBuildingType Type : {
		EGuLiBuildingType::MissileTurret,
		EGuLiBuildingType::SentryTurret,
		EGuLiBuildingType::Outpost})
	{
		const FGuLiBuildingDefinition* Definition = FindDefinition(Type);
		if (!Definition || !Definition->IsUsable())
		{
			return false;
		}
	}
	return true;
}

const TCHAR* UGuLiBuildingCatalog::GetDefaultCatalogObjectPath()
{
	return GuLiBuildingCatalogPrivate::DefaultCatalogObjectPath;
}

UGuLiBuildingCatalog* UGuLiBuildingCatalog::LoadDefaultCatalog()
{
	return LoadObject<UGuLiBuildingCatalog>(nullptr, GetDefaultCatalogObjectPath());
}

