#pragma once

#include "CoreMinimal.h"
#include "GuLiWingmanAttackProfile.generated.h"

/** Movement and execution are independent: a new weapon can retain the same flight pattern. */
UENUM(BlueprintType)
enum class EGuLiWingmanAttackPattern : uint8 { Legacy = 0, AirBurstOrbit, GroundDive };

/** Per-agent attack guidance state. Group policy remains in the UE StateTree. */
enum class EGuLiWingmanAttackPhase : uint8
{
	Idle, Ingress, Lineup, Dive, PullUp, Climb,
	AirApproachFire, AirSeparate, AirReturnToOrbit, AirOrbitCooldown
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiWingmanAttackProfile
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EGuLiWingmanAttackPattern Pattern = EGuLiWingmanAttackPattern::Legacy;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName ExecutorId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float FlightSpeed = 9000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float DiveSeconds = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 MissileCount = 10;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float StripLength = 12000.0f;
	/** Minimum vertical clearance above the assigned ground point across the pull-up arc. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float PullUpHeight = 10000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ExplosionRadius = 800.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float AirFireStartDistance = 10000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float AirFireStopDistance = 5000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float AirBurstDurationSeconds = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float AirOrbitCooldownSeconds = 3.0f;
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

namespace GuLiWingmanAttack
{
	inline constexpr int32 MaximumFireRecordsPerFlight = 16;
	inline constexpr float MaximumAirFireRatePerSecond = 30.0f;
	inline constexpr float MinimumAirShotIntervalSeconds = 1.0f / MaximumAirFireRatePerSecond;
	inline constexpr float MinimumGroundHeight = 5000.0f;
	inline constexpr float MaximumPullUpHeight = 10000.0f;
	/** Eight member-relative directions followed by eight world-stable fallbacks. */
	inline constexpr int32 MaximumGroundApproachCandidates = 16;
	inline constexpr double MaximumGroundIngressSeconds = 45.0;
	GULISTRIKE_API bool BuildGroundPath(const FVector& GroundTarget, const FVector& ApproachDirection,
		const FGuLiWingmanAttackProfile& Profile, float TurnDegreesPerSecond, FGuLiWingmanGroundRunPath& OutPath);
	/** Direct approach followed by mirrored 45-degree alternatives. The stable seed splits crowded members left/right. */
	GULISTRIKE_API FVector BuildGroundApproachCandidate(const FVector& DirectApproach,
		uint32 StableAgentSeed, int32 CandidateIndex);
	/** The authored dive entry itself; ingress from the live pose is validated dynamically. */
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
	GULISTRIKE_API EGuLiWingmanAttackPhase SelectAirEntryPhase(
		float TargetDistance, const FGuLiWingmanAttackProfile& Profile);
	GULISTRIKE_API bool ShouldEndAirBurst(
		float TargetDistance, double ElapsedSeconds, const FGuLiWingmanAttackProfile& Profile);
	GULISTRIKE_API bool ShouldBeginAirOrbitCooldown(float CarrierDistance, float OuterSoftRadius);
	GULISTRIKE_API bool IsAirOrbitCooldownComplete(
		double ElapsedSeconds, const FGuLiWingmanAttackProfile& Profile);
	/** Logical shots scheduled at t=0 and then on [0, Duration). */
	GULISTRIKE_API int32 AirBurstShotsDue(float ShotIntervalSeconds, float DurationSeconds, double ElapsedSeconds);
}
