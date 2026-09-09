#pragma once

#include "CoreMinimal.h"
#include "GuLiWingmanAttackProfile.generated.h"

/** Movement and execution are independent: a new weapon can retain the same flight pattern. */
UENUM(BlueprintType)
enum class EGuLiWingmanAttackPattern : uint8 { Legacy = 0, AirDogfight, GroundDive };

/** Per-agent attack guidance state. Group policy remains in the UE StateTree. */
enum class EGuLiWingmanAttackPhase : uint8
{
	Idle, Ingress, Lineup, Dive, PullUp, Climb,
	AirApproachFire, AirBreakawayTurn, AirRetreat, AirReturnTurn
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanAttackProfile
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EGuLiWingmanAttackPattern Pattern = EGuLiWingmanAttackPattern::Legacy;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName ExecutorId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float FlightSpeed = 4500.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float DiveSeconds = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 MissileCount = 10;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float StripLength = 12000.0f;
	/** Minimum vertical clearance above the assigned ground point across the pull-up arc. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float PullUpHeight = 10000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ExplosionRadius = 800.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float BreakawayDistance = 30000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RetreatMinimumDistance = 45000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RetreatLongitudinalMinFraction = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RetreatLongitudinalMaxFraction = 0.60f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RetreatLateralRadius = 15000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RetreatVerticalRadius = 10000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ManeuverArrivalRadius = 7500.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float TurnYawMinDegrees = 40.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float TurnYawMaxDegrees = 90.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float TurnPitchMaxDegrees = 30.0f;
	/** Local to the logical +X-forward aircraft, independently calibrated from the rendered -X mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector Muzzle = FVector(1200.0, 0.0, 0.0);
	bool IsWellFormed() const;
	void AddToStableHash(uint64& Hash) const;
	/** Includes the first/last endpoint shots and the worst 200ms upload window. */
	int32 MaximumShotsPerFlightBatch() const;
};

/** Three straight-leg anchors plus the finite-radius pull-up between the last two legs. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanGroundRunPath
{
	GENERATED_BODY()
	UPROPERTY() FVector Target = FVector::ZeroVector;
	UPROPERTY() FVector Direction = FVector::ForwardVector;
	UPROPERTY() FVector Entry = FVector::ZeroVector;
	UPROPERTY() FVector PullUp = FVector::ZeroVector;
	UPROPERTY() FVector Exit = FVector::ZeroVector;
	UPROPERTY() float TurnRadius = 0.0f;
	UPROPERTY() float TurnSeconds = 0.0f;
	UPROPERTY() float DiveSeconds = 0.0f;
	UPROPERTY() float Speed = 0.0f;
	bool IsValid() const;
	FVector PositionAt(float Seconds) const;
	FVector DirectionAt(float Seconds) const;
	float TotalSeconds() const { return 2.0f * DiveSeconds + TurnSeconds; }
};

/** Deterministic, per-entry geometry used by one fixed-wing dogfight turn. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanAirTurnPlan
{
	GENERATED_BODY()
	UPROPERTY() FVector Origin = FVector::ZeroVector;
	UPROPERTY() FVector Destination = FVector::ZeroVector;
	UPROPERTY() FVector ControlPoint = FVector::ZeroVector;
	UPROPERTY() float SignedYawDegrees = 0.0f;
	UPROPERTY() float PitchDegrees = 0.0f;
	bool IsValid() const;
};

namespace GuLiWingmanAttack
{
	inline constexpr int32 MaximumFireRecordsPerFlight = 16;
	inline constexpr float MinimumGroundHeight = 5000.0f;
	inline constexpr float MaximumPullUpHeight = 10000.0f;
	/** Six seconds of straight, fully validated lead-in precede the authored dive entry. */
	inline constexpr float GroundIngressLeadSeconds = 6.0f;
	/** Eight member-relative directions followed by eight world-stable fallbacks. */
	inline constexpr int32 MaximumGroundApproachCandidates = 16;
	inline constexpr double MaximumGroundIngressSeconds = 45.0;
	inline constexpr double MaximumGroundLineupSeconds = 15.0;
	inline constexpr int32 MaximumAirManeuverCandidates = 8;
	inline constexpr float AirTurnTimeoutSeconds = 10.0f;
	inline constexpr uint32 BreakawayTurnSalt = 0x42524b41u;
	inline constexpr uint32 ReturnTurnSalt = 0x5254524eu;
	GULISTRIKE_API bool BuildGroundPath(const FVector& GroundTarget, const FVector& ApproachDirection,
		const FGuLiWingmanAttackProfile& Profile, float TurnDegreesPerSecond, FGuLiWingmanGroundRunPath& OutPath);
	/** Direct approach followed by mirrored 45-degree alternatives. The stable seed splits crowded members left/right. */
	GULISTRIKE_API FVector BuildGroundApproachCandidate(const FVector& DirectApproach,
		uint32 StableAgentSeed, int32 CandidateIndex);
	GULISTRIKE_API FVector GroundRunSetupPoint(const FGuLiWingmanGroundRunPath& Path);
	inline bool IsGroundPreparationPhase(const EGuLiWingmanAttackPhase Phase)
	{
		return Phase == EGuLiWingmanAttackPhase::Ingress
			|| Phase == EGuLiWingmanAttackPhase::Lineup;
	}
	inline bool IsFrozenGroundExecutionPhase(const EGuLiWingmanAttackPhase Phase)
	{
		return Phase == EGuLiWingmanAttackPhase::Dive
			|| Phase == EGuLiWingmanAttackPhase::PullUp
			|| Phase == EGuLiWingmanAttackPhase::Climb;
	}
	GULISTRIKE_API float ShotTime(int32 Index, int32 Count, float Duration);
	GULISTRIKE_API FVector StripPoint(const FGuLiWingmanGroundRunPath& Path, float Length, int32 Index, int32 Count);
	GULISTRIKE_API bool IsInsideForwardArc(const FVector& Source, const FVector& Forward,
		const FVector& Target, float TargetRadius, float Range, float HalfAngleDegrees);
	GULISTRIKE_API uint32 MakeAirManeuverSeed(uint32 AgentSeed, uint32 TargetRevision,
		uint32 EntrySerial, uint32 ClientTick, uint32 PhaseSalt, uint32 CandidateIndex = 0u);
	GULISTRIKE_API FVector BuildRetreatCandidate(const FVector& Position, const FVector& Target,
		const FVector& Ship, float BreakawayBoundary, const FGuLiWingmanAttackProfile& Profile,
		uint32 Seed);
	GULISTRIKE_API bool BuildAirTurnPlan(const FVector& Position, const FVector& Destination,
		float FlightSpeed, float TurnDegreesPerSecond, const FGuLiWingmanAttackProfile& Profile,
		uint32 Seed, FGuLiWingmanAirTurnPlan& OutPlan);
	GULISTRIKE_API bool HasReachedOrPassed(const FVector& Position, const FVector& Origin,
		const FVector& Destination, float ArrivalRadius);
	GULISTRIKE_API EGuLiWingmanAttackPhase NextAirDogfightPhase(EGuLiWingmanAttackPhase Phase);
	GULISTRIKE_API bool CanQueueAirGun(EGuLiWingmanAttackPhase Phase);
	GULISTRIKE_API bool ShouldFinishAirTurn(const FVector& Position, const FGuLiWingmanAirTurnPlan& Plan,
		float ArrivalRadius, double ElapsedSeconds);
}
