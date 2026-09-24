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
	FQuat Rotation = FQuat::Identity;
	double SampleSimulationSeconds = 0.0;
	uint32 DisplacementRevision = 0u;
	// Cumulative extrapolation from the original sample, including snapshot age.
	float ExtrapolatedSeconds = 0.0f;
	float RadiusCentimeters = 0.0f;
	float BottomZ = 0.0f;
	float TopZ = 0.0f;

	bool IsValid() const;
	FGuLiGroundMassBody Extrapolated(float Seconds) const;
	FVector TranslationDuring(float Seconds) const;
	float RemainingExtrapolationSeconds() const;
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

enum class EGuLiGroundMassContactKind : uint8
{
	None,
	Side,
	Top
};

struct GULISTRIKE_API FGuLiGroundMassContact
{
	EGuLiGroundMassContactKind Kind = EGuLiGroundMassContactKind::None;
	float Time = 1.0f;
	FVector Normal = FVector::ZeroVector;
	float Penetration = 0.0f;
	FGuLiGroundMassBody Body;
	bool IsValid() const { return Kind != EGuLiGroundMassContactKind::None; }
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
	void Rebuild(TConstArrayView<FGuLiGroundMassBody> InBodies, double ReferenceSeconds = 0.0);
	void Query(const FBox2D &Bounds, float ExtrapolationSeconds, float MovementSeconds,
			   TArray<FGuLiGroundMassBody> &OutBodies, int32 *OutRawCandidateCount = nullptr) const;
	bool Find(FGuLiSoldierId SoldierId, float ExtrapolationSeconds, FGuLiGroundMassBody &OutBody) const;
	int32 Num() const { return Bodies.Num(); }
	int32 GetBucketCount() const { return Grid.Num(); }

  private:
	static constexpr float CellSizeCentimeters = 1000.0f;
	TArray<FGuLiGroundMassBody> Bodies;
	TMap<uint32, int32> IndexBySoldierId;
	TMap<FIntPoint, TArray<int32, TInlineAllocator<8>>> Grid;
	double ReferenceSimulationSeconds = 0.0;
	float MaximumRadiusCentimeters = 0.0f;
	float MaximumPlanarSpeedCentimetersPerSecond = 0.0f;
};

/** Immutable once published. Lifecycle overlays share the large, 10 Hz spatial index. */
struct GULISTRIKE_API FGuLiGroundMassSnapshot
{
	uint32 Epoch = 0u;
	uint32 FrameSequence = 0u;
	uint32 CacheGeneration = 0u;
	double SimulationSeconds = 0.0;
	TSharedPtr<const FGuLiGroundMassSpatialIndex> Index;
	// An invalid body is a tombstone. Valid entries replace reliable displacements immediately.
	TMap<uint32, FGuLiGroundMassBody> Overrides;
	TSharedPtr<const FGuLiGroundMassSpatialIndex> OverrideIndex;
	void Query(const FBox2D &Bounds, double StartSeconds, float Duration, TArray<FGuLiGroundMassBody> &Out,
			   int32 *OutRawCount = nullptr) const;
	bool Find(FGuLiSoldierId Id, double AtSeconds, FGuLiGroundMassBody &Out) const;
};

struct GULISTRIKE_API FGuLiGroundMassMoveContext
{
	TSharedPtr<const FGuLiGroundMassSnapshot> Snapshot;
	double StartSimulationSeconds = 0.0;
	float Duration = 0.0f;
};

namespace GuLiGroundMassCollision
{
inline constexpr int32 MaximumCollisionIterations = 3;
inline constexpr float MaximumDepenetrationCentimeters = 50.0f;
inline constexpr float ContactToleranceCentimeters = 2.0f;
inline constexpr float MaximumExtrapolationSeconds = 0.1f;

GULISTRIKE_API void ResolveBodyBounds(const FBox &ModelBounds, FGuLiGroundMassBody &Body);
GULISTRIKE_API FGuLiGroundMassContact Sweep(const FVector &Start, const FVector &Delta, float Radius, float HalfHeight,
											float Duration, TConstArrayView<FGuLiGroundMassBody> Bodies,
											FGuLiSoldierId IgnoredSoldier = {}, bool bAllowLanding = false);

GULISTRIKE_API bool HasVerticalOverlap(float CapsuleCenterZ, float CapsuleHalfHeight, float BodyBottomZ, float BodyTopZ,
									   float Tolerance = ContactToleranceCentimeters);

/** Bounded side-contact solver, also used by deterministic regression tests. */
GULISTRIKE_API FGuLiGroundMassMoveResult ResolvePlanarMove(const FVector &Start, const FVector &IntendedDelta,
														   float CapsuleRadius, float CapsuleHalfHeight,
														   float DeltaSeconds,
														   TConstArrayView<FGuLiGroundMassBody> Candidates,
														   FGuLiSoldierId IgnoredSoldier = FGuLiSoldierId());

/** Finds the highest crossed cylinder top; equal heights use the lowest stable SoldierId. */
GULISTRIKE_API FGuLiGroundMassLandingResult FindLandingSupport(const FVector &Start, const FVector &IntendedDelta,
															   float CapsuleHalfHeight, float DeltaSeconds,
															   TConstArrayView<FGuLiGroundMassBody> Candidates);

/** Returns at most MaximumCount same-team idle candidates ordered by distance, then stable ID. */
GULISTRIKE_API void SelectFriendlyYieldCandidates(TConstArrayView<FGuLiGroundMassYieldCandidate> Candidates,
												  EGuLiTeam MechTeam, const FVector &MechLocation,
												  float MechRadiusCentimeters, float ActivationPaddingCentimeters,
												  float MaximumHeightDifferenceCentimeters, int32 MaximumCount,
												  TArray<int32> &OutCandidateIndices);

/** Computes a clearance target and clamps it to the soldier's captured anchor. */
GULISTRIKE_API FVector ComputeYieldTarget(const FVector &Anchor, const FVector &CurrentLocation,
										  const FVector &MechLocation, uint32 MechStableId, uint32 SoldierStableId,
										  float RequiredCenterDistanceCentimeters,
										  float MaximumAnchorOffsetCentimeters);

/** Holds the existing target until pressure has been absent long enough, then returns the anchor. */
GULISTRIKE_API FVector ResolveYieldTargetWithoutPressure(const FVector &Anchor, const FVector &CurrentTarget,
														 double CurrentSimulationSeconds,
														 double LastPressureSimulationSeconds,
														 double ReturnDelaySeconds, bool &bOutReturning);
} // namespace GuLiGroundMassCollision
