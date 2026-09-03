#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiFlightNavigationQuery.h"
#include "GuLiFlightNavigationSubsystem.generated.h"

class AGuLiFlightNavigationVolume;
class UGuLiFlightNavigationData;

/** Read-only world registry and query entry point. It owns no movement or gameplay state. */
UCLASS()
class GULIFLIGHTNAVIGATIONRUNTIME_API UGuLiFlightNavigationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	void RegisterVolume(AGuLiFlightNavigationVolume* Volume);
	void UnregisterVolume(AGuLiFlightNavigationVolume* Volume);

	/** Runtime spawn gate: validates a baked asset without scanning or rebuilding world geometry. */
	UFUNCTION(BlueprintPure, Category = "Flight Navigation")
	bool HasUsableNavigationAt(const FVector& Point, FString& OutError) const;

	UFUNCTION(BlueprintCallable, Category = "Flight Navigation")
	FGuLiFlightNavPathResult FindPath(
		const FVector& Start,
		const FVector& Goal,
		const FGuLiFlightNavPathQueryOptions& Options);

	FGuLiFlightNavigationQuery CreateQuery(const FVector& Start, const FVector& Goal) const;

	/**
	 * Strict authority gate for one already-produced movement segment.
	 *
	 * The immutable graph is selected exclusively from Start. End is deliberately not part of
	 * volume resolution so a segment leaving navigation reports EndpointOutsideNavigation instead
	 * of degenerating into an empty-query InvalidData result. Both endpoints must also fit within
	 * the selected Volume's current bounds after the agent-radius inset. All query-level radius,
	 * cell, component, link and portal checks remain authoritative.
	 */
	EGuLiFlightNavSegmentStatus ValidateAuthoritativeSegment(
		const FVector& Start,
		const FVector& End,
		float AgentRadius = 0.0f,
		int32* OutStartCell = nullptr,
		int32* OutEndCell = nullptr) const;

	/** Selects the graph from Start and requires both endpoints to share its component. */
	EGuLiFlightNavSegmentStatus ValidateEndpointsInSameComponent(
		const FVector& Start,
		const FVector& End,
		float AgentRadius = 0.0f,
		int32* OutStartCell = nullptr,
		int32* OutEndCell = nullptr) const;

	/** Read-only, bounded diagnostic for explaining why a location cannot select a runtime graph. */
	FString DescribeNavigationAt(const FVector& Point) const;

	TFuture<FGuLiFlightNavPathResult> FindPathAsync(
		const FVector& Start,
		const FVector& Goal,
		const FGuLiFlightNavPathQueryOptions& Options,
		const TSharedPtr<FGuLiFlightNavCancellationToken, ESPMode::ThreadSafe>& CancellationToken = nullptr) const;

private:
	struct FRuntimeGraphCacheEntry
	{
		TWeakObjectPtr<UGuLiFlightNavigationData> Data;
		uint64 ContentChecksum = 0;
		uint32 DefinitionRevision = 0;
		TSharedPtr<const FGuLiFlightNavRuntimeGraph, ESPMode::ThreadSafe> Graph;
	};

	const AGuLiFlightNavigationVolume* ResolveVolume(const FVector& Start, const FVector& Goal) const;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AGuLiFlightNavigationVolume>> RegisteredVolumes;

	/** Game-thread cache; each entry points at an immutable graph safe for worker-thread queries. */
	mutable TArray<FRuntimeGraphCacheEntry> RuntimeGraphCache;
};
