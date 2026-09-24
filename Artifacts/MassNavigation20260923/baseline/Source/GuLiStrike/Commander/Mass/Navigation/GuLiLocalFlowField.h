// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Opaque revision key captured before a local flow-field build starts.
 *
 * The authority layer owns the meaning of these revisions. A worker result must only
 * be installed when this key still matches the live order/navigation state.
 */
struct GULISTRIKE_API FGuLiFlowFieldBuildKey
{
	uint32 OrderId = 0u;
	uint32 NavigationGeneration = 0u;
	uint32 PathRevision = 0u;
	int32 PathPointIndex = INDEX_NONE;
	FIntPoint TileCoordinate = FIntPoint::ZeroValue;

	bool operator==(const FGuLiFlowFieldBuildKey& Other) const
	{
		return OrderId == Other.OrderId
			&& NavigationGeneration == Other.NavigationGeneration
			&& PathRevision == Other.PathRevision
			&& PathPointIndex == Other.PathPointIndex
			&& TileCoordinate == Other.TileCoordinate;
	}

	bool operator!=(const FGuLiFlowFieldBuildKey& Other) const
	{
		return !(*this == Other);
	}

	friend uint32 GetTypeHash(const FGuLiFlowFieldBuildKey& Key)
	{
		uint32 Hash = HashCombine(GetTypeHash(Key.OrderId), GetTypeHash(Key.NavigationGeneration));
		Hash = HashCombine(Hash, GetTypeHash(Key.PathRevision));
		Hash = HashCombine(Hash, GetTypeHash(Key.PathPointIndex));
		return HashCombine(Hash, GetTypeHash(Key.TileCoordinate));
	}
};

/**
 * Fully detached input for one 64x64 local flow-field tile.
 *
 * Navigation and corridor sampling must happen before this value is handed to a
 * worker thread. Walkable has exactly CellCount bits. TraversalCosts is either empty
 * (uniform cost) or contains CellCount positive finite weights.
 */
struct GULISTRIKE_API FGuLiLocalFlowFieldBuildData
{
	FGuLiFlowFieldBuildKey Key;
	FVector2D WorldMin = FVector2D::ZeroVector;
	float CellSizeCentimeters = 500.0f;
	FIntPoint RequestedGoalCell = FIntPoint::ZeroValue;
	TBitArray<> Walkable;
	TArray<float> TraversalCosts;

	bool IsWellFormed(FString* OutError = nullptr) const;
};

/**
 * Project-owned, server-side local flow-field tile.
 *
 * Build() performs deterministic eight-neighbour Dijkstra integration without
 * touching UObject state, so callers may run it on a worker thread. Sampling is
 * read-only after Build() completes. A location outside the tile, or in an
 * unreachable cell, returns false so the caller can fall back to its shared
 * NavMesh path direction.
 */
class GULISTRIKE_API FGuLiLocalFlowField
{
public:
	static constexpr int32 GridSize = 64;
	static constexpr int32 CellCount = GridSize * GridSize;
	static constexpr float DefaultCellSizeCentimeters = 500.0f;

	bool Build(const FGuLiLocalFlowFieldBuildData& BuildData, FString* OutError = nullptr);
	void Reset();

	bool IsReady() const { return bReady; }
	const FGuLiFlowFieldBuildKey& GetBuildKey() const { return BuildKey; }
	const FIntPoint& GetResolvedGoalCell() const { return ResolvedGoalCell; }
	float GetCellSizeCentimeters() const { return CellSizeCentimeters; }
	const FVector2D& GetWorldMin() const { return WorldMin; }

	bool TryWorldToCell(const FVector& WorldLocation, FIntPoint& OutCell) const;
	FVector GetCellCenter(const FIntPoint& Cell, float WorldZ = 0.0f) const;
	bool IsReachable(const FIntPoint& Cell) const;
	float GetIntegrationCost(const FIntPoint& Cell) const;

	/** Returns a normalized XY direction. Zero is a valid result at the goal cell. */
	bool SampleDirection(const FVector& WorldLocation, FVector& OutDirection) const;

	static bool IsCellInBounds(const FIntPoint& Cell);
	static int32 CellToIndex(const FIntPoint& Cell);
	static FIntPoint IndexToCell(int32 CellIndex);

private:
	bool ResolveGoalCell(const FIntPoint& RequestedGoal, FIntPoint& OutGoal) const;
	bool CanTraverseDiagonal(const FIntPoint& From, const FIntPoint& To) const;
	float GetTraversalCost(int32 CellIndex) const;

	FGuLiFlowFieldBuildKey BuildKey;
	FVector2D WorldMin = FVector2D::ZeroVector;
	float CellSizeCentimeters = DefaultCellSizeCentimeters;
	FIntPoint ResolvedGoalCell = FIntPoint::ZeroValue;
	TBitArray<> Walkable;
	TArray<float> TraversalCosts;
	TArray<float> IntegrationCosts;
	TArray<FVector2D> Directions;
	bool bReady = false;
};
