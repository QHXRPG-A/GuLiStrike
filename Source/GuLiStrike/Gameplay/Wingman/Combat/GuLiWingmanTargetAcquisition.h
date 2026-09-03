// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Contracts/GuLiWingmanProtocolTypes.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTypes.h"

class AActor;

enum class EGuLiWingmanTargetObservationSource : uint8
{
	ReplicatedShip = 0,
	CommanderAuthoritativePose,
	WingmanAcceptedPose
};

/**
 * Client-side observation used only to choose a target for a FireIntent. It is
 * never accepted as combat truth; authority resolves the stable Target handle
 * and validates the source AcceptedStateRef again.
 */
struct GULISTRIKE_API FGuLiWingmanTargetObservation
{
	FGuLiTargetHandle Target;
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	FVector Location = FVector::ZeroVector;
	TWeakObjectPtr<AActor> CollisionActor;
	EGuLiWingmanTargetObservationSource Source =
		EGuLiWingmanTargetObservationSource::ReplicatedShip;
	bool bAlive = false;
	bool bFromAcceptedOrReliableState = false;
};

/**
 * Per-acquisition-cut client-only 3D index. Observations are copied and sorted
 * by stable target identity before insertion so neither TMap bucket order nor
 * replicated container order can affect the selected target.
 */
class GULISTRIKE_API FGuLiWingmanTargetSpatialHash
{
public:
	/** Builds a fresh immutable cut. CellSizeCentimeters is clamped to a safe finite range. */
	bool Build(
		TConstArrayView<FGuLiWingmanTargetObservation> Observations,
		double CellSizeCentimeters);

	/** Returns only cells intersecting the spherical range, in stable handle order. */
	bool QuerySphere(
		const FVector& Center,
		double RadiusCentimeters,
		TArray<FGuLiWingmanTargetObservation>& OutCandidates) const;

	void Reset();
	int32 Num() const { return StableObservations.Num(); }
	double GetCellSizeCentimeters() const { return CellSizeCentimeters; }

private:
	static FIntVector CellForLocation(const FVector& Location, double CellSize);

	double CellSizeCentimeters = 0.0;
	TArray<FGuLiWingmanTargetObservation> StableObservations;
	TMap<FIntVector, TArray<int32>> Buckets;
};

/** Pure, deterministic policy shared by the runtime collector and automation tests. */
class GULISTRIKE_API FGuLiWingmanTargetAcquisition
{
public:
	/** Five candidate cuts per second; this is not permission to send at a higher rate. */
	static constexpr double CandidatePublishIntervalSeconds = 0.2;
	static constexpr double TargetScanIntervalSeconds = 0.2;
	static constexpr double MaximumAcceptedPoseAgeSeconds = 0.35;

	using FLineOfSightPredicate = TFunctionRef<bool(const FGuLiWingmanTargetObservation&)>;
	using FCommanderAuthoritativeTransformReader =
		TFunctionRef<bool(FGuLiSoldierId, FTransform&)>;

	/**
	 * Nearest eligible target wins. Exact/effectively equal distances are ordered
	 * by the stable protocol handle so input container order never changes truth.
	 */
	static bool SelectBestTarget(
		const FVector& EmitterLocation,
		const FVector& EmitterForward,
		EGuLiTeam EmitterTeam,
		const FGuLiWingmanWeaponRuntimeConfig& Weapon,
		TConstArrayView<FGuLiWingmanTargetObservation> Observations,
		FLineOfSightPredicate HasLineOfSight,
		FGuLiWingmanTargetObservation& OutTarget);

	/** Initializes 25 independent 5 Hz scans across one 200 ms phase window. */
	static bool InitializeStaggeredSchedule(
		double NowSeconds,
		int32 EmitterCount,
		TArray<double>& OutNextScanSeconds);

	/** Consumes at most one due scan per emitter, preserving phase after hitches. */
	static bool ConsumeDueScans(
		double NowSeconds,
		TArray<double>& InOutNextScanSeconds,
		TArray<int32>& OutDueEmitterIndices);

	/**
	 * Converts reliable Commander roster entries with a caller-supplied
	 * authoritative-pose reader. Production passes
	 * AGuLiCommanderPresentationActor::TryGetAuthoritativeSoldierTransform;
	 * the predicted PresentedTransform API is intentionally not part of this contract.
	 */
	static int32 AppendCommanderObservations(
		TConstArrayView<FGuLiSoldierStateItem> ReliableStates,
		uint32 SnapshotMatchEpoch,
		uint32 ExpectedMatchEpoch,
		FCommanderAuthoritativeTransformReader ReadAuthoritativeTransform,
		TArray<FGuLiWingmanTargetObservation>& InOutObservations);

	/** Stable protocol ordering used by acquisition cuts and the spatial index. */
	static bool IsStableHandleLess(const FGuLiTargetHandle& Lhs, const FGuLiTargetHandle& Rhs);
};
