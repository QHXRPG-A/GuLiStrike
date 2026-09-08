#include "Gameplay/Wingman/Combat/GuLiWingmanAttackProfile.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTypes.h"

bool FGuLiWingmanAttackProfile::IsWellFormed() const
{
	if (Pattern == EGuLiWingmanAttackPattern::Legacy) return true;
	if (Pattern != EGuLiWingmanAttackPattern::AirDogfight && Pattern != EGuLiWingmanAttackPattern::GroundDive) return false;
	if (ExecutorId.IsNone() || !FMath::IsFinite(FlightSpeed) || FlightSpeed <= 0 || Muzzle.ContainsNaN()) return false;
	if (Pattern == EGuLiWingmanAttackPattern::AirDogfight)
		return FMath::IsFinite(BreakawayDistance) && BreakawayDistance > 0
			&& FMath::IsFinite(RetreatMinimumDistance) && RetreatMinimumDistance > 0
			&& FMath::IsFinite(RetreatLongitudinalMinFraction) && FMath::IsFinite(RetreatLongitudinalMaxFraction)
			&& RetreatLongitudinalMinFraction >= 0 && RetreatLongitudinalMaxFraction >= RetreatLongitudinalMinFraction
			&& RetreatLongitudinalMaxFraction <= 1.0f
			&& FMath::IsFinite(RetreatLateralRadius) && RetreatLateralRadius > 0
			&& FMath::IsFinite(RetreatVerticalRadius) && RetreatVerticalRadius > 0
			&& FMath::IsFinite(ManeuverArrivalRadius) && ManeuverArrivalRadius > 0
			&& FMath::IsFinite(TurnYawMinDegrees) && FMath::IsFinite(TurnYawMaxDegrees)
			&& TurnYawMinDegrees > 0 && TurnYawMaxDegrees >= TurnYawMinDegrees && TurnYawMaxDegrees < 180.0f
			&& FMath::IsFinite(TurnPitchMaxDegrees) && TurnPitchMaxDegrees >= 0 && TurnPitchMaxDegrees < 90.0f;
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
		BreakawayDistance, RetreatMinimumDistance, RetreatLongitudinalMinFraction,
		RetreatLongitudinalMaxFraction, RetreatLateralRadius, RetreatVerticalRadius,
		ManeuverArrivalRadius, TurnYawMinDegrees, TurnYawMaxDegrees, TurnPitchMaxDegrees})
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
	if (Profile.PullUpHeight - DropInTurn < MinimumGroundHeight) return false;
	const float Leg = Profile.FlightSpeed * Profile.DiveSeconds * UE_INV_SQRT_2;
	OutPath.Target = Target; OutPath.Direction = Direction;
	OutPath.Entry = Target - Direction * Leg + FVector::UpVector * (Profile.PullUpHeight + Leg);
	OutPath.PullUp = Target + FVector::UpVector * Profile.PullUpHeight;
	OutPath.Exit = Target + Direction * (2.0f * Radius * UE_INV_SQRT_2 + Leg)
		+ FVector::UpVector * (Profile.PullUpHeight + Leg);
	OutPath.TurnRadius = Radius; OutPath.TurnSeconds = 90.0f / TurnDegreesPerSecond;
	OutPath.DiveSeconds = Profile.DiveSeconds; OutPath.Speed = Profile.FlightSpeed;
	return OutPath.IsValid();
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

bool FGuLiWingmanAirTurnPlan::IsValid() const
{
	return !Origin.ContainsNaN() && !Destination.ContainsNaN() && !ControlPoint.ContainsNaN()
		&& !Origin.Equals(Destination) && !Origin.Equals(ControlPoint)
		&& FMath::IsFinite(SignedYawDegrees) && FMath::IsFinite(PitchDegrees);
}

namespace
{
	uint32 MixAirBits(uint32 Value)
	{
		Value ^= Value >> 16; Value *= 0x7feb352du;
		Value ^= Value >> 15; Value *= 0x846ca68bu;
		return Value ^ (Value >> 16);
	}

	float AirUnit(uint32 Seed)
	{
		return static_cast<float>((MixAirBits(Seed) >> 8) * (1.0 / 16777216.0));
	}
}

uint32 GuLiWingmanAttack::MakeAirManeuverSeed(uint32 AgentSeed, uint32 TargetRevision,
	uint32 EntrySerial, uint32 ClientTick, uint32 PhaseSalt, uint32 CandidateIndex)
{
	uint32 Seed = MixAirBits(AgentSeed ^ 0x9e3779b9u);
	for (const uint32 Value : {TargetRevision, EntrySerial, ClientTick, PhaseSalt, CandidateIndex})
		Seed = MixAirBits(Seed ^ MixAirBits(Value + 0x9e3779b9u));
	return Seed ? Seed : 1u;
}

FVector GuLiWingmanAttack::BuildRetreatCandidate(const FVector& Position, const FVector& Target,
	const FVector& Ship, float BreakawayBoundary, const FGuLiWingmanAttackProfile& Profile, uint32 Seed)
{
	if (Position.ContainsNaN() || Target.ContainsNaN() || Ship.ContainsNaN() || !Profile.IsWellFormed()
		|| Profile.Pattern != EGuLiWingmanAttackPattern::AirDogfight || !FMath::IsFinite(BreakawayBoundary))
		return FVector::ZeroVector;
	const FVector Corridor = (Ship - Target).GetSafeNormal(UE_SMALL_NUMBER,
		(Position - Target).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector));
	FVector Right = FVector::CrossProduct(FVector::UpVector, Corridor).GetSafeNormal();
	if (Right.IsNearlyZero()) Right = FVector::RightVector;
	const FVector Up = FVector::CrossProduct(Corridor, Right).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const float Fraction = FMath::Lerp(Profile.RetreatLongitudinalMinFraction,
		Profile.RetreatLongitudinalMaxFraction, AirUnit(Seed ^ 0xa511e9b3u));
	const float CorridorLength = FVector::Distance(Target, Ship);
	const float MinimumLongitudinal = FMath::Max(Profile.RetreatMinimumDistance,
		FMath::Max(0.0f, BreakawayBoundary) + Profile.ManeuverArrivalRadius);
	const float Longitudinal = FMath::Max(CorridorLength * Fraction, MinimumLongitudinal);
	const float DiskRadius = FMath::Sqrt(AirUnit(Seed ^ 0x63d83595u));
	const float Angle = 2.0f * UE_PI * AirUnit(Seed ^ 0xc2b2ae35u);
	return Target + Corridor * Longitudinal
		+ Right * (FMath::Cos(Angle) * DiskRadius * Profile.RetreatLateralRadius)
		+ Up * (FMath::Sin(Angle) * DiskRadius * Profile.RetreatVerticalRadius);
}

bool GuLiWingmanAttack::BuildAirTurnPlan(const FVector& Position, const FVector& Destination,
	float FlightSpeed, float TurnDegreesPerSecond, const FGuLiWingmanAttackProfile& Profile,
	uint32 Seed, FGuLiWingmanAirTurnPlan& OutPlan)
{
	OutPlan = {};
	if (Position.ContainsNaN() || Destination.ContainsNaN() || !Profile.IsWellFormed()
		|| Profile.Pattern != EGuLiWingmanAttackPattern::AirDogfight || !FMath::IsFinite(FlightSpeed)
		|| FlightSpeed <= 0 || !FMath::IsFinite(TurnDegreesPerSecond) || TurnDegreesPerSecond <= 0) return false;
	const FVector Direct = (Destination - Position).GetSafeNormal();
	if (Direct.IsNearlyZero()) return false;
	const float YawMagnitude = FMath::Lerp(Profile.TurnYawMinDegrees, Profile.TurnYawMaxDegrees,
		AirUnit(Seed ^ 0x27d4eb2fu));
	const float SignedYaw = (AirUnit(Seed ^ 0x165667b1u) < 0.5f ? -1.0f : 1.0f) * YawMagnitude;
	const float Pitch = FMath::Lerp(-Profile.TurnPitchMaxDegrees, Profile.TurnPitchMaxDegrees,
		AirUnit(Seed ^ 0xd3a2646cu));
	const FVector Yawed = Direct.RotateAngleAxis(SignedYaw, FVector::UpVector).GetSafeNormal();
	FVector PitchAxis = FVector::CrossProduct(FVector::UpVector, Yawed).GetSafeNormal();
	if (PitchAxis.IsNearlyZero()) PitchAxis = FVector::RightVector;
	const FVector TurnDirection = Yawed.RotateAngleAxis(Pitch, PitchAxis).GetSafeNormal();
	const float PhysicalRadius = FlightSpeed / FMath::DegreesToRadians(TurnDegreesPerSecond);
	OutPlan.Origin = Position; OutPlan.Destination = Destination;
	OutPlan.ControlPoint = Position + TurnDirection * (2.0f * PhysicalRadius);
	OutPlan.SignedYawDegrees = SignedYaw; OutPlan.PitchDegrees = Pitch;
	return OutPlan.IsValid();
}

bool GuLiWingmanAttack::HasReachedOrPassed(const FVector& Position, const FVector& Origin,
	const FVector& Destination, float ArrivalRadius)
{
	if (Position.ContainsNaN() || Origin.ContainsNaN() || Destination.ContainsNaN()
		|| !FMath::IsFinite(ArrivalRadius) || ArrivalRadius < 0) return false;
	if (FVector::DistSquared(Position, Destination) <= FMath::Square(static_cast<double>(ArrivalRadius))) return true;
	const FVector Direction = (Destination - Origin).GetSafeNormal();
	return !Direction.IsNearlyZero() && FVector::DotProduct(Position - Destination, Direction) >= 0.0;
}

EGuLiWingmanAttackPhase GuLiWingmanAttack::NextAirDogfightPhase(EGuLiWingmanAttackPhase Phase)
{
	switch (Phase)
	{
	case EGuLiWingmanAttackPhase::AirApproachFire: return EGuLiWingmanAttackPhase::AirBreakawayTurn;
	case EGuLiWingmanAttackPhase::AirBreakawayTurn: return EGuLiWingmanAttackPhase::AirRetreat;
	case EGuLiWingmanAttackPhase::AirRetreat: return EGuLiWingmanAttackPhase::AirReturnTurn;
	case EGuLiWingmanAttackPhase::AirReturnTurn: return EGuLiWingmanAttackPhase::AirApproachFire;
	default: return EGuLiWingmanAttackPhase::Idle;
	}
}

bool GuLiWingmanAttack::CanQueueAirGun(EGuLiWingmanAttackPhase Phase)
{
	return Phase == EGuLiWingmanAttackPhase::AirApproachFire;
}

bool GuLiWingmanAttack::ShouldFinishAirTurn(const FVector& Position, const FGuLiWingmanAirTurnPlan& Plan,
	float ArrivalRadius, double ElapsedSeconds)
{
	return Plan.IsValid() && FMath::IsFinite(ElapsedSeconds) && ElapsedSeconds >= 0
		&& (ElapsedSeconds >= AirTurnTimeoutSeconds
			|| HasReachedOrPassed(Position, Plan.Origin, Plan.ControlPoint, ArrivalRadius));
}
