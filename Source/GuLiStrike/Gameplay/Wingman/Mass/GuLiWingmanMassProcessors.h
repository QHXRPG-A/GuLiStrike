// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTypes.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "GuLiWingmanMassProcessors.generated.h"

class UWorld;
struct FGuLiWingmanAvoidanceFragment;

namespace GuLiWingmanAvoidance
{
	/** UObject-free input shared by production and deterministic automation. */
	struct GULISTRIKE_API FSpatialSample
	{
		FGuLiWingmanHandle Handle;
		FVector Position = FVector::ZeroVector;
		float SeparationRadiusCentimeters = 0.0f;
		float MaximumAccelerationCentimetersPerSecondSquared = 0.0f;
		bool bAlive = false;
	};

	struct GULISTRIKE_API FSpatialResult
	{
		FGuLiWingmanHandle Handle;
		FVector Acceleration = FVector::ZeroVector;
		FIntVector Cell = FIntVector::ZeroValue;
		float CellSizeCentimeters = 0.0f;
		uint16 NeighborTests = 0u;
		uint16 ContributingNeighbors = 0u;
	};

	/** Deterministic group-scoped 3D uniform hash. Each query visits exactly 27 neighboring cells. */
	GULISTRIKE_API void ComputeSpatialHashSeparation(
		TConstArrayView<FSpatialSample> Samples,
		TArray<FSpatialResult>& OutResults);

	/** One collision probe result. Clearance equals the requested distance when unobstructed. */
	struct GULISTRIKE_API FHeadingProbeResult
	{
		float ClearanceCentimeters = 0.0f;
		/** False when the complete probed segment leaves or violates baked FlightNav. */
		bool bFlightNavSegmentValid = true;
		bool bWorldStatic = false;
		bool bWorldDynamic = false;
	};

	struct GULISTRIKE_API FHeadingSelection
	{
		FVector Direction = FVector::ForwardVector;
		float ClearanceCentimeters = 0.0f;
		uint16 ProbeCount = 0u;
		bool bFullLookAheadSafe = false;
		bool bImmediateStepSafe = false;
		bool bEncounteredFlightNavBoundary = false;
		bool bEncounteredWorldStatic = false;
		bool bEncounteredWorldDynamic = false;
	};

	/**
	 * Tests feasible forward/yaw/pitch headings in a fixed order, then scores
	 * deviation and clearance deterministically. Only a sweep-safe direction is returned.
	 */
	GULISTRIKE_API FHeadingSelection SelectSafeHeading(
		const FVector& CurrentDirection,
		const FVector& DesiredDirection,
		float CurrentSpeedCentimetersPerSecond,
		const FGuLiWingmanFormationRuntimeConfig& Tuning,
		TFunctionRef<FHeadingProbeResult(const FVector&, float)> Probe);

	struct GULISTRIKE_API FRecoveryClockState
	{
		float ConsecutiveBlockedSeconds = 0.0f;
		float ConsecutiveClearSeconds = 0.0f;
		bool bControlledRecovery = false;
	};

	/** Three-second entry and half-second verified-clear exit, shared by runtime and tests. */
	GULISTRIKE_API void AdvanceRecoveryClock(
		FRecoveryClockState& State,
		float DeltaSeconds,
		bool bFullLookAheadSafe,
		bool bHasVerifiedReopenedPath);

	/** Stable minimum-speed orbit/wait heading around a previously verified navigation point. */
	GULISTRIKE_API FVector BuildRecoveryOrbitDirection(
		const FVector& Position,
		const FVector& CurrentDirection,
		const FVector& SafePoint,
		float OrbitRadiusCentimeters);

#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * Scoped game-thread seam for synthetic transient Worlds that have no baked
	 * navigation volume. Production code never consults this in non-test builds.
	 */
	using FFlightNavSegmentValidatorForTests =
		TFunction<bool(const FVector&, const FVector&, float)>;
	GULISTRIKE_API void SetFlightNavSegmentValidatorForTests(
		UWorld* TransientTestWorld,
		FFlightNavSegmentValidatorForTests Validator);
	GULISTRIKE_API void ResetFlightNavSegmentValidatorForTests();

	/**
	 * Scoped game-thread seam for exercising the physical final-step gate in a
	 * transient World without a physics scene. Production builds always execute
	 * the shared WorldStatic/WorldDynamic sphere-sweep implementation.
	 */
	using FWorldObstacleSegmentProbeForTests =
		TFunction<FHeadingProbeResult(const FVector&, const FVector&, float)>;
	GULISTRIKE_API void SetWorldObstacleSegmentProbeForTests(
		UWorld* TransientTestWorld,
		FWorldObstacleSegmentProbeForTests Probe);
	GULISTRIKE_API void ResetWorldObstacleSegmentProbeForTests();

	/** Exercises the exact production fragment-persistence helper without running Mass. */
	GULISTRIKE_API void AccumulateHeadingThreatsForTests(
		FGuLiWingmanAvoidanceFragment& State,
		const FHeadingSelection& Selection);
#endif
}

/** Unit-testable mode policy processor. Runtime groups use the explicit StateTree/fallback runner instead. */
UCLASS()
class GULISTRIKE_API UGuLiWingmanModeProcessor final : public UMassProcessor
{
	GENERATED_BODY()

public:
	UGuLiWingmanModeProcessor();
	static bool IsOwnerTransformWriter() { return false; }

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};

/** Turns the committed formation definition into per-entity world-space guidance. */
UCLASS()
class GULISTRIKE_API UGuLiWingmanFormationGuidanceProcessor final : public UMassProcessor
{
	GENERATED_BODY()

public:
	UGuLiWingmanFormationGuidanceProcessor();
	static bool IsOwnerTransformWriter() { return false; }
	static bool ConsumesNavigationGuidanceReadOnly() { return true; }

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};

/** Computes owner-only pair separation without writing Transform. */
UCLASS()
class GULISTRIKE_API UGuLiWingmanAvoidanceProcessor final : public UMassProcessor
{
	GENERATED_BODY()

public:
	UGuLiWingmanAvoidanceProcessor();
	static bool IsOwnerTransformWriter() { return false; }

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};

/** The sole fixed-wing Transform writer for owner entities. */
UCLASS()
class GULISTRIKE_API UGuLiWingmanFlightIntegrationProcessor final : public UMassProcessor
{
	GENERATED_BODY()

public:
	UGuLiWingmanFlightIntegrationProcessor();
	static bool IsOwnerTransformWriter() { return true; }

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
