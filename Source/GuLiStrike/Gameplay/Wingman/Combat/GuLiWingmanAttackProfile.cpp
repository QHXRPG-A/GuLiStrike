#include "Gameplay/Wingman/Combat/GuLiWingmanAttackProfile.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTypes.h"

bool FGuLiWingmanAttackProfile::IsWellFormed() const
{
	if (Pattern == EGuLiWingmanAttackPattern::Legacy) return true;
	if (Pattern != EGuLiWingmanAttackPattern::AirBurstOrbit && Pattern != EGuLiWingmanAttackPattern::GroundDive) return false;
	if (ExecutorId.IsNone() || !FMath::IsFinite(FlightSpeed) || FlightSpeed <= 0 || Muzzle.ContainsNaN()) return false;
	if (Pattern == EGuLiWingmanAttackPattern::AirBurstOrbit)
		return FMath::IsFinite(AirFireStartDistance) && FMath::IsFinite(AirFireStopDistance)
			&& AirFireStartDistance > AirFireStopDistance && AirFireStopDistance > 0.0f
			&& FMath::IsFinite(AirBurstDurationSeconds) && AirBurstDurationSeconds > 0.0f
			&& FMath::IsFinite(AirOrbitCooldownSeconds) && AirOrbitCooldownSeconds > 0.0f;
	return FMath::IsFinite(DiveSeconds) && DiveSeconds > 0 && MissileCount >= 1 && MissileCount <= 64
		&& FMath::IsFinite(StripLength) && StripLength >= 0 && FMath::IsFinite(PullUpHeight)
		&& PullUpHeight >= GuLiWingmanAttack::MinimumGroundHeight && PullUpHeight <= GuLiWingmanAttack::MaximumPullUpHeight
		&& FMath::IsFinite(ExplosionRadius) && ExplosionRadius > 0
		&& MaximumShotsPerFlightBatch() <= GuLiWingmanAttack::MaximumFireRecordsPerFlight;
}

int32 FGuLiWingmanAttackProfile::MaximumShotsPerFlightBatch() const
{
	if (MissileCount <= 1) return 5;
	if (!FMath::IsFinite(DiveSeconds) || DiveSeconds <= 0) return MAX_int32;
	return 5 * (1 + FMath::FloorToInt(0.2 * static_cast<double>(MissileCount - 1) / DiveSeconds + 1.e-6));
}

void FGuLiWingmanAttackProfile::AddToStableHash(uint64& Hash) const
{
	GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(Pattern));
	GuLiShipAbilityHash::AddString(Hash, ExecutorId.ToString());
	for (float Value : {FlightSpeed, DiveSeconds, StripLength, PullUpHeight, ExplosionRadius,
		AirFireStartDistance, AirFireStopDistance, AirBurstDurationSeconds, AirOrbitCooldownSeconds})
		GuLiShipAbilityHash::AddFloat(Hash, Value);
	GuLiShipAbilityHash::AddUInt32(Hash, static_cast<uint32>(MissileCount));
	GuLiShipAbilityHash::AddFloat(Hash, static_cast<float>(Muzzle.X));
	GuLiShipAbilityHash::AddFloat(Hash, static_cast<float>(Muzzle.Y));
	GuLiShipAbilityHash::AddFloat(Hash, static_cast<float>(Muzzle.Z));
}

bool GuLiWingmanAttack::BuildGroundPath(const FVector& GroundTarget, const FVector& ApproachDirection,
	const FGuLiWingmanAttackProfile& Profile, float TurnDegreesPerSecond, FGuLiWingmanGroundRunPath& OutPath)
{
	// Callers may rebuild a run in place while preserving its target/approach.
	const FVector Target = GroundTarget;
	const FVector Direction = ApproachDirection.GetSafeNormal2D();
	const bool bFiniteApproach = !ApproachDirection.ContainsNaN();
	OutPath = {};
	if (!Profile.IsWellFormed() || Profile.Pattern != EGuLiWingmanAttackPattern::GroundDive || Target.ContainsNaN()
		|| !bFiniteApproach || !FMath::IsFinite(TurnDegreesPerSecond) || TurnDegreesPerSecond <= 0) return false;
	if (Direction.IsNearlyZero()) return false;
	const float Radius = Profile.FlightSpeed / FMath::DegreesToRadians(TurnDegreesPerSecond);
	const float DropInTurn = Radius * (1.0f - UE_INV_SQRT_2);
	// PullUpHeight is the guaranteed clearance at the bottom of the finite-radius arc,
	// so lift both turn anchors by the amount the arc descends between them.
	const float TurnAnchorHeight = Profile.PullUpHeight + DropInTurn;
	const float Leg = Profile.FlightSpeed * Profile.DiveSeconds * UE_INV_SQRT_2;
	OutPath.Target = Target; OutPath.Direction = Direction;
	OutPath.Entry = Target - Direction * Leg + FVector::UpVector * (TurnAnchorHeight + Leg);
	OutPath.PullUp = Target + FVector::UpVector * TurnAnchorHeight;
	OutPath.Exit = Target + Direction * (2.0f * Radius * UE_INV_SQRT_2 + Leg)
		+ FVector::UpVector * (TurnAnchorHeight + Leg);
	OutPath.TurnRadius = Radius; OutPath.TurnSeconds = 90.0f / TurnDegreesPerSecond;
	OutPath.DiveSeconds = Profile.DiveSeconds; OutPath.Speed = Profile.FlightSpeed;
	return OutPath.IsValid();
}

FVector GuLiWingmanAttack::BuildGroundApproachCandidate(const FVector& DirectApproach,
	uint32 StableAgentSeed, int32 CandidateIndex)
{
	static constexpr int32 SignedSteps[8] = {0, 1, -1, 2, -2, 3, -3, 4};
	if (DirectApproach.ContainsNaN() || CandidateIndex < 0 || CandidateIndex >= MaximumGroundApproachCandidates)
		return FVector::ZeroVector;
	const bool bWorldStableFallback = CandidateIndex >= UE_ARRAY_COUNT(SignedSteps);
	const FVector Direct = bWorldStableFallback
		? FVector::ForwardVector.RotateAngleAxis(22.5f, FVector::UpVector)
		: DirectApproach.GetSafeNormal2D();
	if (Direct.IsNearlyZero()) return FVector::ZeroVector;
	const int32 LocalIndex = bWorldStableFallback
		? CandidateIndex - UE_ARRAY_COUNT(SignedSteps) : CandidateIndex;
	int32 Step = SignedSteps[LocalIndex];
	if ((StableAgentSeed & 1u) != 0u) Step = -Step;
	return Direct.RotateAngleAxis(45.0f * Step, FVector::UpVector).GetSafeNormal2D();
}

FVector GuLiWingmanAttack::GroundRunSetupPoint(const FGuLiWingmanGroundRunPath& Path)
{
	if (!Path.IsValid()) return FVector::ZeroVector;
	return Path.Entry;
}

bool FGuLiWingmanGroundRunPath::IsValid() const
{
	return !Target.ContainsNaN() && !Direction.ContainsNaN() && !Entry.ContainsNaN() && !Exit.ContainsNaN()
		&& FMath::IsFinite(Speed) && Speed > 0 && FMath::IsFinite(TurnSeconds) && TurnSeconds > 0
		&& FMath::IsFinite(DiveSeconds) && DiveSeconds > 0 && Direction.IsNormalized();
}

FVector FGuLiWingmanGroundRunPath::PositionAt(float Seconds) const
{
	const float Time = FMath::Clamp(Seconds, 0.0f, TotalSeconds());
	if (Time <= DiveSeconds) return Entry + (Direction - FVector::UpVector) * (Speed * Time * UE_INV_SQRT_2);
	const float TurnTime = FMath::Min(Time - DiveSeconds, TurnSeconds);
	const float Angle = -UE_PI / 4.0f + (UE_PI / 2.0f) * TurnTime / TurnSeconds;
	const FVector TurnPosition = PullUp + Direction * (TurnRadius * (FMath::Sin(Angle) + UE_INV_SQRT_2))
		+ FVector::UpVector * (TurnRadius * (UE_INV_SQRT_2 - FMath::Cos(Angle)));
	const float ClimbTime = FMath::Max(0.0f, Time - DiveSeconds - TurnSeconds);
	return TurnPosition + (Direction + FVector::UpVector) * (Speed * ClimbTime * UE_INV_SQRT_2);
}

FVector FGuLiWingmanGroundRunPath::DirectionAt(float Seconds) const
{
	const float Alpha = FMath::Clamp((Seconds - DiveSeconds) / TurnSeconds, 0.0f, 1.0f);
	const float Pitch = FMath::Lerp(-UE_PI / 4.0f, UE_PI / 4.0f, Alpha);
	return Direction * FMath::Cos(Pitch) + FVector::UpVector * FMath::Sin(Pitch);
}

float GuLiWingmanAttack::ShotTime(int32 Index, int32 Count, float Duration)
{
	return Count > 1 ? FMath::Clamp(Index, 0, Count - 1) * Duration / static_cast<float>(Count - 1) : 0.0f;
}

FVector GuLiWingmanAttack::StripPoint(const FGuLiWingmanGroundRunPath& Path, float Length, int32 Index, int32 Count)
{
	return Path.Target + Path.Direction * (Count > 1 ? Length * FMath::Clamp(Index, 0, Count - 1) / (Count - 1) : 0.0f);
}

bool GuLiWingmanAttack::IsInsideForwardArc(const FVector& Source, const FVector& Forward, const FVector& Target,
	float TargetRadius, float Range, float HalfAngleDegrees)
{
	if (Source.ContainsNaN() || Forward.ContainsNaN() || Target.ContainsNaN() || !FMath::IsFinite(TargetRadius)
		|| !FMath::IsFinite(Range) || !FMath::IsFinite(HalfAngleDegrees) || Range <= 0 || HalfAngleDegrees <= 0) return false;
	const FVector Delta = Target - Source;
	return Delta.Size() <= Range + FMath::Max(0.0f, TargetRadius) && !Forward.IsNearlyZero()
		&& FVector::DotProduct(Forward.GetSafeNormal(), Delta.GetSafeNormal()) >= FMath::Cos(FMath::DegreesToRadians(HalfAngleDegrees));
}

EGuLiWingmanAttackPhase GuLiWingmanAttack::SelectAirEntryPhase(
	const float TargetDistance, const FGuLiWingmanAttackProfile& Profile)
{
	if (!Profile.IsWellFormed() || Profile.Pattern != EGuLiWingmanAttackPattern::AirBurstOrbit
		|| !FMath::IsFinite(TargetDistance) || TargetDistance < 0.0f)
	{
		return EGuLiWingmanAttackPhase::Idle;
	}
	return TargetDistance < Profile.AirFireStartDistance
		? EGuLiWingmanAttackPhase::AirSeparate
		: EGuLiWingmanAttackPhase::AirApproachFire;
}

bool GuLiWingmanAttack::ShouldEndAirBurst(
	const float TargetDistance, const double ElapsedSeconds, const FGuLiWingmanAttackProfile& Profile)
{
	return Profile.IsWellFormed() && Profile.Pattern == EGuLiWingmanAttackPattern::AirBurstOrbit
		&& FMath::IsFinite(TargetDistance) && FMath::IsFinite(ElapsedSeconds)
		&& (TargetDistance < Profile.AirFireStopDistance
			|| ElapsedSeconds >= Profile.AirBurstDurationSeconds);
}

bool GuLiWingmanAttack::ShouldBeginAirOrbitCooldown(
	const float CarrierDistance, const float OuterSoftRadius)
{
	return FMath::IsFinite(CarrierDistance) && CarrierDistance >= 0.0f
		&& FMath::IsFinite(OuterSoftRadius) && OuterSoftRadius > 0.0f
		&& CarrierDistance <= OuterSoftRadius;
}

bool GuLiWingmanAttack::IsAirOrbitCooldownComplete(
	const double ElapsedSeconds, const FGuLiWingmanAttackProfile& Profile)
{
	return Profile.IsWellFormed() && Profile.Pattern == EGuLiWingmanAttackPattern::AirBurstOrbit
		&& FMath::IsFinite(ElapsedSeconds)
		&& ElapsedSeconds >= Profile.AirOrbitCooldownSeconds;
}

int32 GuLiWingmanAttack::AirBurstShotsDue(
	const float ShotIntervalSeconds, const float DurationSeconds, const double ElapsedSeconds)
{
	if (!FMath::IsFinite(ShotIntervalSeconds) || !FMath::IsFinite(DurationSeconds)
		|| !FMath::IsFinite(ElapsedSeconds) || ShotIntervalSeconds < MinimumAirShotIntervalSeconds
		|| DurationSeconds <= 0.0f || ElapsedSeconds < 0.0)
	{
		return 0;
	}
	const int32 Total = FMath::CeilToInt(DurationSeconds / ShotIntervalSeconds - 1.e-6f);
	return FMath::Clamp(FMath::FloorToInt((ElapsedSeconds + 1.e-6) / ShotIntervalSeconds) + 1, 0, Total);
}
