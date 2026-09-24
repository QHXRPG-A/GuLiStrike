#pragma once

#include "CoreMinimal.h"
#include "NavigationData.h"
#include "Commander/Mass/Navigation/GuLiNavigationDependency.h"

class UNavigationSystemV1;
struct FGuLiSharedRouteGoal;

/** One native NavMesh corridor, shared across control groups. No soldier connectors or slots. */
struct FGuLiSharedMoveRoute
{
	enum class EState : uint8 { Queued, Indexing, Ready, Unavailable, Retiring };
	uint64 Handle = 0;
	TSharedPtr<FGuLiSharedRouteGoal> Goal;
	FNavLocation Start;
	FGuLiNavigationDependency Navigation;
	TMap<NavNodeRef, int32> PolygonIndex;
	TArray<FVector> Portals;
	EState State = EState::Queued;
	int32 IndexCursor = 0, RetireCursor = 0;
	uint32 AttemptGeneration = 0;
	double LastUsed = 0, QueuedAt = 0, ReadyAt = 0;
	bool bAutomatic = false, bPartial = false;
	FVector End = FVector::ZeroVector;
	bool Sample(const FNavLocation& Position, float Tolerance, int32& Cursor, FVector& Out) const;
	SIZE_T Bytes() const;
};

struct FGuLiSharedRouteGoal
{
	FVector Target = FVector::ZeroVector;
	TMap<NavNodeRef, TWeakPtr<FGuLiSharedMoveRoute>> Starts, Covered;
};

/** Random one-to-one slots within a command, with no cross-command occupancy ledger. */
struct FGuLiSharedMoveIntent
{
	FVector Click = FVector::ZeroVector;
	TSharedPtr<FGuLiSharedRouteGoal> Goal;
	TArray<FNavLocation> Points;
	TMap<uint32, FNavLocation> AssignedPoints;
	// Frozen by task submission before any admission slice; consumed by random swap/pop.
	TArray<uint32> UnassignedMembers;
	TMap<uint16, float> WidthByUnitType;
	FRandomStream Random;
	float Pitch = 0.f, GeneratedRadius = 0.f;
	int32 WantedPoints = 0, MetadataCursor = 0, Ring = 0, Step = 0;
	uint32 CoverageGeneration = 0;
	bool bMetadataReady = false;
	bool bExhausted = false;
	void Generate(const ANavigationData& Data, uint32 Generation, FGuLiNavigationWorkBudget& Budget);
	bool InDockingArea(const FVector& Position) const;
};

/** World-owned, game-thread-only request pool. Engine auto-repath is always disabled. */
struct FGuLiSharedMoveRoutePool
{
	static constexpr SIZE_T MaximumBytes = 128ull * 1024 * 1024;
	TWeakObjectPtr<const ANavigationData> Data;
	TMap<FVector, TWeakPtr<FGuLiSharedRouteGoal>> Goals;
	TArray<FVector> GoalKeys;
	TArray<TSharedPtr<FGuLiSharedMoveRoute>> Routes;
	uint64 NextHandle = 1, Queries = 0, FailedQueries = 0, CacheHits = 0, Invalidations = 0;
	int32 WorkCursor[2] = {}, MaintenanceCursor = 0, GoalCursor = 0;
	SIZE_T CachedBytes = 0;
	double QueryCpuSeconds = 0;
	TSharedPtr<FGuLiSharedRouteGoal> Goal(const FVector& Target);
	TSharedPtr<FGuLiSharedMoveRoute> Request(const TSharedPtr<FGuLiSharedRouteGoal>& Goal,
		const FNavLocation& Start, bool bAutomatic, const ANavigationData& NavData, uint32 Generation);
	void Tick(bool bAutomatic, UNavigationSystemV1& System, const ANavigationData& NavData,
		uint32 Generation, FGuLiNavigationWorkBudget& Budget);
	void Maintain(const ANavigationData& NavData, uint32 Generation, FGuLiNavigationWorkBudget& Budget);
};
