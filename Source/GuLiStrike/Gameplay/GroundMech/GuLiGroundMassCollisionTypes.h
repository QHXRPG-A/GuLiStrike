#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Commander/Network/GuLiCommanderTypes.h"

/** One data-only cylindrical collision body backed by an authoritative Mass soldier. */
struct GULISTRIKE_API FGuLiGroundMassBody
{
	FGuLiSoldierId SoldierId;
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	uint16 UnitTypeId = 0u;
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	float RadiusCentimeters = 0.0f;
	float BottomZ = 0.0f;
	float TopZ = 0.0f;

	bool IsValid() const;
	FGuLiGroundMassBody Extrapolated(float Seconds) const;
};

struct GULISTRIKE_API FGuLiGroundMassMoveResult
{
	FVector Delta = FVector::ZeroVector;
	FGuLiSoldierId FirstHit;
	int32 CandidateCount = 0;
	int32 SideHitCount = 0;
	float DepenetrationCentimeters = 0.0f;
};

struct GULISTRIKE_API FGuLiGroundMassLandingResult
{
	FGuLiSoldierId SoldierId;
	float Time = 1.0f;
	float TopZ = 0.0f;
	FVector BodyLocation = FVector::ZeroVector;

	bool IsValid() const { return SoldierId.IsValid(); }
};

/** Minimal input for the deterministic friendly-yield selector. */
struct GULISTRIKE_API FGuLiGroundMassYieldCandidate
{
	uint32 StableSoldierId = 0u;
	EGuLiTeam Team = EGuLiTeam::Unassigned;
	FVector Location = FVector::ZeroVector;
	float RadiusCentimeters = 0.0f;
	bool bCanYield = false;
};

/** Shared 10 Hz spatial index. Each body lives in one cell; queries expand by the largest body motion. */
class GULISTRIKE_API FGuLiGroundMassSpatialIndex
{
public:
	void Reset();
	void Rebuild(TConstArrayView<FGuLiGroundMassBody> InBodies);
	void Query(
		const FBox2D& Bounds,
		float ExtrapolationSeconds,
		float MovementSeconds,
		TArray<FGuLiGroundMassBody>& OutBodies,
		int32* OutRawCandidateCount = nullptr) const;
	bool Find(FGuLiSoldierId SoldierId, float ExtrapolationSeconds, FGuLiGroundMassBody& OutBody) const;
	int32 Num() const { return Bodies.Num(); }
	int32 GetBucketCount() const { return Grid.Num(); }

private:
	static constexpr float CellSizeCentimeters = 1000.0f;
	TArray<FGuLiGroundMassBody> Bodies;
	TMap<uint32, int32> IndexBySoldierId;
	TMap<FIntPoint, TArray<int32, TInlineAllocator<8>>> Grid;
	float MaximumRadiusCentimeters = 0.0f;
	float MaximumPlanarSpeedCentimetersPerSecond = 0.0f;
};

namespace GuLiGroundMassCollision
{
	inline constexpr int32 MaximumCollisionIterations = 3;
	inline constexpr float MaximumDepenetrationCentimeters = 50.0f;
	inline constexpr float ContactToleranceCentimeters = 2.0f;

	GULISTRIKE_API bool HasVerticalOverlap(
		float CapsuleCenterZ,
		float CapsuleHalfHeight,
		float BodyBottomZ,
		float BodyTopZ,
		float Tolerance = ContactToleranceCentimeters);

	/** Resolves cylinder side contacts without mutating Mass bodies. Z remains the caller's requested Z delta. */
	GULISTRIKE_API FGuLiGroundMassMoveResult ResolvePlanarMove(
		const FVector& Start,
		const FVector& IntendedDelta,
		float CapsuleRadius,
		float CapsuleHalfHeight,
		float DeltaSeconds,
		TConstArrayView<FGuLiGroundMassBody> Candidates,
		FGuLiSoldierId IgnoredSoldier = FGuLiSoldierId());

	/** Finds the highest crossed cylinder top; equal heights use the lowest stable SoldierId. */
	GULISTRIKE_API FGuLiGroundMassLandingResult FindLandingSupport(
		const FVector& Start,
		const FVector& IntendedDelta,
		float CapsuleHalfHeight,
		float DeltaSeconds,
		TConstArrayView<FGuLiGroundMassBody> Candidates);

	/** Returns at most MaximumCount same-team idle candidates ordered by distance, then stable ID. */
	GULISTRIKE_API void SelectFriendlyYieldCandidates(
		TConstArrayView<FGuLiGroundMassYieldCandidate> Candidates,
		EGuLiTeam MechTeam,
		const FVector& MechLocation,
		float MechRadiusCentimeters,
		float ActivationPaddingCentimeters,
		float MaximumHeightDifferenceCentimeters,
		int32 MaximumCount,
		TArray<int32>& OutCandidateIndices);

	/** Computes a clearance target and clamps it to the soldier's captured anchor. */
	GULISTRIKE_API FVector ComputeYieldTarget(
		const FVector& Anchor,
		const FVector& CurrentLocation,
		const FVector& MechLocation,
		uint32 MechStableId,
		uint32 SoldierStableId,
		float RequiredCenterDistanceCentimeters,
		float MaximumAnchorOffsetCentimeters);

	/** Holds the existing target until pressure has been absent long enough, then returns the anchor. */
	GULISTRIKE_API FVector ResolveYieldTargetWithoutPressure(
		const FVector& Anchor,
		const FVector& CurrentTarget,
		double CurrentSimulationSeconds,
		double LastPressureSimulationSeconds,
		double ReturnDelaySeconds,
		bool& bOutReturning);
}
