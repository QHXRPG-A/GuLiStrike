#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"

/** Immutable geometry. Ownership and supply stay in the resource authority. */
class GULISTRIKE_API FGuLiStrongholdTopology
{
public:
	void Initialize(TConstArrayView<FGuLiTerritoryDefinition> Territories);
	bool IsAdjacent(int32 A, int32 B) const;
	TArray<int32> FindAttackCandidates(int32 Start, EGuLiTeam Team, TFunctionRef<EGuLiTeam(int32)> Owner) const;
	TArray<bool> FindEncircled(EGuLiTeam Team, TFunctionRef<EGuLiTeam(int32)> Owner) const;
	/** Four-connected non-enemy regions; enemy cells have INDEX_NONE. */
	TArray<int32> FindTransportRegions(EGuLiTeam Team, TFunctionRef<EGuLiTeam(int32)> Owner) const;
private:
	struct FNode
	{
		FName Id;
		FVector Center;
		TArray<int32> Neighbors;
		TArray<int32> CardinalNeighbors;
	};
	TArray<FNode> Nodes;
	bool IdLess(int32 A, int32 B) const;
};
