#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "GuLiStrongholdTransportNetwork.generated.h"

/** Derived transport eligibility, never an ownership authority. */
USTRUCT()
struct FGuLiTransportNode
{
	GENERATED_BODY()
	UPROPERTY() int32 TerritoryIndex = INDEX_NONE;
	UPROPERTY() FName TerritoryId;
	UPROPERTY() EGuLiTeam Team = EGuLiTeam::Unassigned;
	UPROPERTY() FVector GroundLocation = FVector::ZeroVector;
	UPROPERTY() int32 TransitFieldId = 0;
	bool operator==(const FGuLiTransportNode& Other) const;
};

USTRUCT()
struct FGuLiTransportEdge
{
	GENERATED_BODY()
	UPROPERTY() int32 A = INDEX_NONE;
	UPROPERTY() int32 B = INDEX_NONE;
	UPROPERTY() EGuLiTeam Team = EGuLiTeam::Unassigned;
};

USTRUCT(BlueprintType)
struct FGuLiTransportNetworkSnapshot
{
	GENERATED_BODY()
	UPROPERTY() uint32 Revision = 0;
	UPROPERTY() TArray<FGuLiTransportNode> Nodes;
	UPROPERTY() TArray<FGuLiTransportEdge> Edges;
	const FGuLiTransportNode* FindNode(int32 TerritoryIndex) const;
};

/** Two deterministic faction MSTs. ResourceWorld supplies the eligible nodes. */
class GULISTRIKE_API FGuLiStrongholdTransportNetwork
{
public:
	bool Rebuild(TArray<FGuLiTransportNode> EligibleNodes);
	const FGuLiTransportNetworkSnapshot& GetSnapshot() const { return Snapshot; }
	TArray<int32> FindRoute(int32 Start, int32 Goal, EGuLiTeam Team) const;
	bool HasEdge(int32 A, int32 B, EGuLiTeam Team) const;
private:
	FGuLiTransportNetworkSnapshot Snapshot;
};
