#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"

struct FGuLiStrongholdEdge
{
	int32 A = INDEX_NONE;
	int32 B = INDEX_NONE;
};

/** Immutable geometry. Ownership and supply stay in the resource authority. */
class GULISTRIKE_API FGuLiStrongholdTopology
{
public:
	void Initialize(TConstArrayView<FGuLiTerritoryDefinition> Territories);
	bool IsAdjacent(int32 A, int32 B) const;
	const TArray<FGuLiStrongholdEdge>& GetTransportEdges() const { return Edges; }
	TArray<int32> FindRoute(int32 Start, int32 Goal, TFunctionRef<bool(int32)> CanUse) const;
	TArray<int32> FindAttackCandidates(int32 Start, EGuLiTeam Team, TFunctionRef<EGuLiTeam(int32)> Owner) const;
	TArray<bool> FindEncircled(EGuLiTeam Team, TFunctionRef<EGuLiTeam(int32)> Owner) const;
private:
	struct FNode
	{
		FName Id;
		FVector Center;
		TArray<int32> Neighbors;
		TArray<int32> CardinalNeighbors;
		TArray<int32> TransportNeighbors;
	};
	TArray<FNode> Nodes;
	TArray<FGuLiStrongholdEdge> Edges;
	bool IdLess(int32 A, int32 B) const;
};
