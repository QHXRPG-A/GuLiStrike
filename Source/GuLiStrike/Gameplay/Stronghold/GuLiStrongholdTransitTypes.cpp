#include "Gameplay/Stronghold/GuLiStrongholdTransitTypes.h"

void FGuLiTransitTiming::Initialize(double InLength, float InPeakSpeed, float AccelerationSeconds,
	float DecelerationSeconds, float InInitialSpeed)
{
	check(InLength >= 0 && InPeakSpeed > 0 && AccelerationSeconds >= 0 && DecelerationSeconds > 0);
	Length = InLength; PeakSpeed = InPeakSpeed; InitialSpeed = InInitialSpeed;
	Acceleration = AccelerationSeconds; Deceleration = DecelerationSeconds;
	const double Ramps = .5 * (InitialSpeed + PeakSpeed) * Acceleration + .5 * PeakSpeed * Deceleration;
	if (Length < Ramps)
	{
		const float Scale = Length / Ramps;
		Acceleration *= Scale; Deceleration *= Scale; Cruise = 0;
	}
	else Cruise = (Length - Ramps) / PeakSpeed;
}
double FGuLiTransitTiming::DistanceAt(double Seconds) const
{
	Seconds = FMath::Clamp(Seconds, 0., Duration());
	if (Acceleration > 0 && Seconds < Acceleration)
		return InitialSpeed * Seconds + .5 * (PeakSpeed - InitialSpeed) * Seconds * Seconds / Acceleration;
	double Distance = .5 * (InitialSpeed + PeakSpeed) * Acceleration;
	Seconds -= Acceleration;
	Distance += PeakSpeed * FMath::Min(Seconds, double(Cruise));
	Seconds = FMath::Max(0., Seconds - Cruise);
	if (Deceleration > 0) Distance += PeakSpeed * (Seconds - .5 * Seconds * Seconds / Deceleration);
	return FMath::Min(Distance, Length);
}
float FGuLiTransitTiming::SpeedAt(double Seconds) const
{
	if (Seconds <= 0) return InitialSpeed;
	if (Seconds < Acceleration) return FMath::Lerp(InitialSpeed, PeakSpeed, float(Seconds / Acceleration));
	if (Seconds < Acceleration + Cruise) return PeakSpeed;
	return Deceleration > 0 ? PeakSpeed * FMath::Clamp(1. - (Seconds - Acceleration - Cruise) / Deceleration, 0.,1.) : 0;
}
FVector FGuLiStrongholdTransitState::SamplePosition(double ServerTime, int32* OutLeg) const
{
	if (OutLeg) *OutLeg = 0;
	if (Phase == EGuLiTransitPhase::Ground || Phase == EGuLiTransitPhase::ExitFlash) return ExitPosition;
	if (Phase == EGuLiTransitPhase::ApproachingGate) return EntryPosition;
	check(!Route.IsEmpty());
	const double Elapsed = FMath::Max(0., ServerTime - StartServerTime);
	if (AscentSeconds > 0 && Elapsed < AscentSeconds)
		return FMath::Lerp(FVector(EntryPosition), FVector(Route[0].Position), Elapsed / AscentSeconds);
	double Distance = Timing.DistanceAt(Elapsed - AscentSeconds);
	for (int32 Index = 0; Index + 1 < Route.Num(); ++Index)
	{
		const FVector A = Route[Index].Position, B = Route[Index + 1].Position;
		const double Length = FVector::Distance(A,B);
		if (Distance < Length)
		{
			if (OutLeg) *OutLeg = Index;
			return FMath::Lerp(A,B, Distance / Length);
		}
		Distance -= Length;
	}
	if (OutLeg) *OutLeg = FMath::Max(0, Route.Num() - 2);
	return Route.Last().Position;
}
