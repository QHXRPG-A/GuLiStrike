// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Resources/GuLiResourceTypes.h"

namespace
{
	constexpr uint8 BlueBudgets[GULI_RESOURCE_BOARD_DIMENSION][GULI_RESOURCE_BOARD_DIMENSION] =
	{
		{ 6, 7, 10, 7, 6 },
		{ 7, 9, 10, 9, 7 },
		{ 8, 9, 10, 9, 8 },
		{ 7, 9, 10, 9, 7 },
		{ 6, 7, 10, 7, 6 }
	};
	constexpr uint8 RedBudgets[GULI_RESOURCE_BOARD_DIMENSION][GULI_RESOURCE_BOARD_DIMENSION] =
	{
		{ 0, 1, 2, 1, 0 },
		{ 1, 2, 3, 2, 1 },
		{ 2, 3, 4, 3, 2 },
		{ 1, 2, 3, 2, 1 },
		{ 0, 1, 2, 1, 0 }
	};

	bool SetError(FString* OutError, const FString& Error)
	{
		if (OutError)
		{
			*OutError = Error;
		}
		return false;
	}
}

bool FGuLiControllableActorId::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	(void)Map;
	Ar.SerializeIntPacked(Value);
	bOutSuccess = !Ar.IsError();
	return true;
}

bool FGuLiTerritoryDefinition::IsWellFormed(FString* OutError) const
{
	if (TerritoryId.IsNone() || BoardRow < 1 || BoardRow > GULI_RESOURCE_BOARD_DIMENSION
		|| BoardColumn < 1 || BoardColumn > GULI_RESOURCE_BOARD_DIMENSION
		|| Center.ContainsNaN() || LocalPolygon.Num() != 4)
	{
		return SetError(OutError, TEXT("Territory identity, board coordinate, center, or polygon is invalid."));
	}
	if (TerritoryId != GuLiResources::MakeTerritoryId(BoardRow, BoardColumn))
	{
		return SetError(OutError, TEXT("TerritoryId does not match BoardRow/BoardColumn."));
	}
	if (BlueClusterBudget != GuLiResources::GetBlueClusterBudget(BoardRow, BoardColumn)
		|| RedClusterBudget != GuLiResources::GetRedClusterBudget(BoardRow, BoardColumn))
	{
		return SetError(OutError, TEXT("Territory cluster budget differs from the canonical 5x5 budget."));
	}
	const FVector2D CanonicalCorners[] = {
		FVector2D(-GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM, -GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM),
		FVector2D( GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM, -GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM),
		FVector2D( GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM,  GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM),
		FVector2D(-GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM,  GULI_RESOURCE_TERRITORY_HALF_EXTENT_CM)
	};
	for (const FVector2D Corner : CanonicalCorners)
	{
		if (!LocalPolygon.ContainsByPredicate(
			[Corner](const FVector2D Point) { return Point.Equals(Corner, 0.1); }))
		{
			return SetError(OutError,
				TEXT("Territory polygon must contain the four canonical +/-56000 cm corners."));
		}
	}
	return true;
}

bool FGuLiResourceClusterDefinition::IsWellFormed(const int32 TotalNodeCount, FString* OutError) const
{
	if (ClusterId == 0u || TerritoryIndex >= GULI_RESOURCE_TERRITORY_COUNT
		|| Center.ContainsNaN() || !FMath::IsFinite(ObstacleRadiusCentimeters)
		|| ObstacleRadiusCentimeters <= 0.0f || NodeCount != GULI_RESOURCE_NODES_PER_CLUSTER
		|| FirstNodeIndex + NodeCount > static_cast<uint32>(TotalNodeCount))
	{
		return SetError(OutError, TEXT("Cluster identity, territory, transform, obstacle, or node span is invalid."));
	}
	return true;
}

EGuLiOreVisualStage FGuLiResourceNodeDefinition::GetInitialVisualStage() const
{
	return GuLiResources::AmountToVisualStage(InitialAmount);
}

bool FGuLiResourceNodeDefinition::IsWellFormed(const int32 TotalClusterCount, FString* OutError) const
{
	if (NodeId == 0u || ClusterId == 0u || ClusterId > TotalClusterCount || FamilyIndex > 3u
		|| InitialAmount < 1u || InitialAmount > 3u || WorldTransform.ContainsNaN()
		|| WorldTransform.GetScale3D().ContainsNaN() || WorldTransform.GetScale3D().GetMin() <= 0.0)
	{
		return SetError(OutError, TEXT("Node identity, cluster, family, amount, or transform is invalid."));
	}
	return true;
}

bool FGuLiResourceSpawnAnchors::IsWellFormed() const
{
	return !RedFactory.ContainsNaN() && !RedAssembly.ContainsNaN()
		&& !BlueFactory.ContainsNaN() && !BlueAssembly.ContainsNaN()
		&& FVector2D(RedFactory).Equals(FVector2D(0.0, 252000.0), 1.0)
		&& FVector2D(RedAssembly).Equals(FVector2D(0.0, 196000.0), 1.0)
		&& FVector2D(BlueFactory).Equals(FVector2D(0.0, -252000.0), 1.0)
		&& FVector2D(BlueAssembly).Equals(FVector2D(0.0, -196000.0), 1.0);
}

bool FGuLiMiningCommand::IsWellFormed() const
{
	if (RequestId == 0u || SelectionRevision == 0u || FVector(Target).ContainsNaN())
	{
		return false;
	}
	switch (Type)
	{
	case EGuLiMiningOrderType::Move:
	case EGuLiMiningOrderType::ReturnToFactory:
	case EGuLiMiningOrderType::Cancel:
		return ClusterId == 0u;
	case EGuLiMiningOrderType::MineCluster:
		return ClusterId != 0u;
	default:
		return false;
	}
}

void FGuLiTeamResourcePrivateState::SortByStableId()
{
	Factories.Sort([](
		const FGuLiResourceFactoryPrivateState& A,
		const FGuLiResourceFactoryPrivateState& B)
	{
		return A.StableActorId < B.StableActorId;
	});
	MiningVehicles.Sort([](
		const FGuLiMiningVehiclePrivateState& A,
		const FGuLiMiningVehiclePrivateState& B)
	{
		return A.StableActorId < B.StableActorId;
	});
}

bool FGuLiTeamResourcePrivateState::HasSamePayload(
	const FGuLiTeamResourcePrivateState& Other) const
{
	return Inventory.Blue == Other.Inventory.Blue
		&& Inventory.Red == Other.Inventory.Red
		&& Inventory.Revision == Other.Inventory.Revision
		&& Factories == Other.Factories
		&& MiningVehicles == Other.MiningVehicles;
}

const FGuLiMiningVehiclePrivateState* FGuLiTeamResourcePrivateState::FindMiningVehicle(
	const FGuLiControllableActorId Id) const
{
	return MiningVehicles.FindByPredicate([Id](const FGuLiMiningVehiclePrivateState& State)
	{
		return State.StableActorId == Id;
	});
}

FName GuLiResources::MakeTerritoryId(const int32 Row, const int32 Column)
{
	return FName(*FString::Printf(TEXT("Outpost_R%dC%d"), Row, Column));
}

int32 GuLiResources::ToTerritoryIndex(const int32 Row, const int32 Column)
{
	return Row >= 1 && Row <= GULI_RESOURCE_BOARD_DIMENSION
		&& Column >= 1 && Column <= GULI_RESOURCE_BOARD_DIMENSION
		? (Row - 1) * GULI_RESOURCE_BOARD_DIMENSION + (Column - 1)
		: INDEX_NONE;
}

FVector GuLiResources::GetTerritoryCenter(const int32 Row, const int32 Column)
{
	const float X = (static_cast<float>(Column) - 3.0f) * GULI_RESOURCE_TERRITORY_SIZE_CM;
	const float Y = (3.0f - static_cast<float>(Row)) * GULI_RESOURCE_TERRITORY_SIZE_CM;
	return FVector(X, Y, 0.0f);
}

EGuLiOreVisualStage GuLiResources::AmountToVisualStage(const uint8 RemainingAmount)
{
	if (RemainingAmount >= 3u)
	{
		return EGuLiOreVisualStage::Full;
	}
	if (RemainingAmount == 2u)
	{
		return EGuLiOreVisualStage::Partial;
	}
	if (RemainingAmount == 1u)
	{
		return EGuLiOreVisualStage::Remnant;
	}
	return EGuLiOreVisualStage::Hidden;
}

uint8 GuLiResources::GetBlueClusterBudget(const int32 Row, const int32 Column)
{
	return Row >= 1 && Row <= GULI_RESOURCE_BOARD_DIMENSION
		&& Column >= 1 && Column <= GULI_RESOURCE_BOARD_DIMENSION
		? BlueBudgets[Row - 1][Column - 1]
		: 0u;
}

uint8 GuLiResources::GetRedClusterBudget(const int32 Row, const int32 Column)
{
	return Row >= 1 && Row <= GULI_RESOURCE_BOARD_DIMENSION
		&& Column >= 1 && Column <= GULI_RESOURCE_BOARD_DIMENSION
		? RedBudgets[Row - 1][Column - 1]
		: 0u;
}

bool GuLiResources::IsPlayableTeam(const EGuLiTeam Team)
{
	return Team == EGuLiTeam::Red || Team == EGuLiTeam::Blue;
}
