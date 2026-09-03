#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "GuLiFlightNavigationTypes.h"

class UGuLiFlightNavigationData;

/**
 * Result of the narrow, synchronous authority validation path. This path only inspects the
 * immutable baked graph; it never searches for a route or advances gameplay state.
 */
enum class EGuLiFlightNavSegmentStatus : uint8
{
	Valid = 0,
	InvalidData,
	EndpointOutsideNavigation,
	InsufficientClearance,
	Disconnected,
	MissingLink,
	InvalidPortal
};

/** Immutable, UObject-free graph copied on the game thread before an async query starts. */
struct GULIFLIGHTNAVIGATIONRUNTIME_API FGuLiFlightNavRuntimeGraph
{
	FGuLiFlightNavBakeMetadata Metadata;
	TArray<FGuLiFlightNavOctreeNode> Nodes;
	TArray<FGuLiFlightNavCell> Cells;
	TArray<FGuLiFlightNavPortal> Portals;
	TArray<FGuLiFlightNavLink> Links;

	int32 FindContainingCell(const FVector& Point) const;
	bool IsStructurallyValid() const;
};

class GULIFLIGHTNAVIGATIONRUNTIME_API FGuLiFlightNavCancellationToken final
{
public:
	void Cancel();
	bool IsCancelled() const;

private:
	TAtomic<bool> bCancelled{ false };
};

/** Thread-safe query facade. It never reads UObjects after construction. */
class GULIFLIGHTNAVIGATIONRUNTIME_API FGuLiFlightNavigationQuery final
{
public:
	FGuLiFlightNavigationQuery() = default;
	explicit FGuLiFlightNavigationQuery(TSharedPtr<const FGuLiFlightNavRuntimeGraph, ESPMode::ThreadSafe> InGraph);

	bool IsValid() const;
	int32 FindContainingCell(const FVector& Point) const;
	bool IsSegmentNavigable(const FVector& Start, const FVector& End, float AgentRadius = 0.0f) const;

	/**
	 * Validates two endpoints against one immutable graph without requiring the
	 * straight line between them to cross portals. This is intended for spawn
	 * and formation gates that need a shared connected component before any
	 * movement exists.
	 */
	EGuLiFlightNavSegmentStatus ValidateEndpointsInSameComponent(
		const FVector& Start,
		const FVector& End,
		float AgentRadius = 0.0f,
		int32* OutStartCell = nullptr,
		int32* OutEndCell = nullptr) const;

	/**
	 * Validates one already-produced movement segment against baked cells, component identity,
	 * links and portals. Unlike FindPath this only follows portals crossed in monotonic segment
	 * order, expands each cell at most once, and never searches for a spatial detour.
	 */
	EGuLiFlightNavSegmentStatus ValidateAuthoritativeSegment(
		const FVector& Start,
		const FVector& End,
		float AgentRadius = 0.0f,
		int32* OutStartCell = nullptr,
		int32* OutEndCell = nullptr) const;

	FGuLiFlightNavPathResult FindPath(
		const FVector& Start,
		const FVector& Goal,
		const FGuLiFlightNavPathQueryOptions& Options = FGuLiFlightNavPathQueryOptions(),
		const TSharedPtr<FGuLiFlightNavCancellationToken, ESPMode::ThreadSafe>& CancellationToken = nullptr) const;

	TFuture<FGuLiFlightNavPathResult> FindPathAsync(
		const FVector& Start,
		const FVector& Goal,
		const FGuLiFlightNavPathQueryOptions& Options = FGuLiFlightNavPathQueryOptions(),
		const TSharedPtr<FGuLiFlightNavCancellationToken, ESPMode::ThreadSafe>& CancellationToken = nullptr) const;

private:
	float ResolveAgentRadius(float RequestedRadius) const;
	TArray<FVector> SmoothPath(const TArray<FVector>& RawPoints, float AgentRadius) const;

	TSharedPtr<const FGuLiFlightNavRuntimeGraph, ESPMode::ThreadSafe> Graph;
};
