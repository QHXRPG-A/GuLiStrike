#include "Gameplay/GroundMech/GuLiGroundMassCollisionTypes.h"

bool FGuLiGroundMassBody::IsValid() const
{
	return SoldierId.IsValid()
		&& !Location.ContainsNaN()
		&& !Velocity.ContainsNaN()
		&& FMath::IsFinite(RadiusCentimeters)
		&& RadiusCentimeters > 0.0f
		&& FMath::IsFinite(BottomZ)
		&& FMath::IsFinite(TopZ)
		&& TopZ > BottomZ;
}

FGuLiGroundMassBody FGuLiGroundMassBody::Extrapolated(const float Seconds) const
{
	FGuLiGroundMassBody Result = *this;
	const float SafeSeconds = FMath::IsFinite(Seconds) ? FMath::Clamp(Seconds, 0.0f, 0.1f) : 0.0f;
	const FVector Offset = Velocity * SafeSeconds;
	Result.Location += Offset;
	Result.BottomZ += Offset.Z;
	Result.TopZ += Offset.Z;
	return Result;
}

namespace
{
	FIntPoint MakeCell(const FVector2D& Location)
	{
		return FIntPoint(
			FMath::FloorToInt(Location.X / 1000.0f),
			FMath::FloorToInt(Location.Y / 1000.0f));
	}

	bool IsApproachingCircle(
		const FVector2D& RelativeStart,
		const FVector2D& RelativeDelta,
		const float CombinedRadius,
		float& OutTime,
		FVector2D& OutNormal)
	{
		const double RadiusSquared = FMath::Square(static_cast<double>(CombinedRadius));
		const double C = RelativeStart.SizeSquared() - RadiusSquared;
		if (C <= 0.0)
		{
			OutTime = 0.0f;
			OutNormal = RelativeStart.GetSafeNormal();
			if (OutNormal.IsNearlyZero()) OutNormal = FVector2D::UnitX();
			return FVector2D::DotProduct(RelativeDelta, OutNormal) < -UE_SMALL_NUMBER;
		}

		const double A = RelativeDelta.SizeSquared();
		if (A <= UE_DOUBLE_SMALL_NUMBER) return false;
		const double B = 2.0 * FVector2D::DotProduct(RelativeStart, RelativeDelta);
		const double Discriminant = B * B - 4.0 * A * C;
		if (Discriminant < 0.0) return false;
		const double Time = (-B - FMath::Sqrt(Discriminant)) / (2.0 * A);
		if (Time < 0.0 || Time > 1.0) return false;
		const FVector2D ContactOffset = RelativeStart + RelativeDelta * Time;
		const FVector2D Normal = ContactOffset.GetSafeNormal();
		if (Normal.IsNearlyZero() || FVector2D::DotProduct(RelativeDelta, Normal) >= -UE_SMALL_NUMBER)
		{
			return false;
		}
		OutTime = static_cast<float>(Time);
		OutNormal = Normal;
		return true;
	}
}

void FGuLiGroundMassSpatialIndex::Reset()
{
	Bodies.Reset();
	IndexBySoldierId.Reset();
	Grid.Reset();
	MaximumRadiusCentimeters = 0.0f;
	MaximumPlanarSpeedCentimetersPerSecond = 0.0f;
}

void FGuLiGroundMassSpatialIndex::Rebuild(const TConstArrayView<FGuLiGroundMassBody> InBodies)
{
	Reset();
	Bodies.Reserve(InBodies.Num());
	IndexBySoldierId.Reserve(InBodies.Num());
	for (const FGuLiGroundMassBody& Body : InBodies)
	{
		if (!Body.IsValid() || IndexBySoldierId.Contains(Body.SoldierId.Value)) continue;
		const int32 Index = Bodies.Add(Body);
		IndexBySoldierId.Add(Body.SoldierId.Value, Index);
		Grid.FindOrAdd(MakeCell(FVector2D(Body.Location))).Add(Index);
		MaximumRadiusCentimeters = FMath::Max(MaximumRadiusCentimeters, Body.RadiusCentimeters);
		MaximumPlanarSpeedCentimetersPerSecond = FMath::Max(
			MaximumPlanarSpeedCentimetersPerSecond,
			Body.Velocity.Size2D());
	}
}

void FGuLiGroundMassSpatialIndex::Query(
	const FBox2D& Bounds,
	const float ExtrapolationSeconds,
	const float MovementSeconds,
	TArray<FGuLiGroundMassBody>& OutBodies,
	int32* OutRawCandidateCount) const
{
	OutBodies.Reset();
	if (OutRawCandidateCount) *OutRawCandidateCount = 0;
	if (!Bounds.bIsValid || Bodies.IsEmpty()) return;
	const float SafeSeconds = FMath::IsFinite(ExtrapolationSeconds)
		? FMath::Clamp(ExtrapolationSeconds, 0.0f, 0.1f)
		: 0.0f;
	const float SafeMovementSeconds = FMath::IsFinite(MovementSeconds)
		? FMath::Clamp(MovementSeconds, 0.0f, 0.1f)
		: 0.0f;
	const float SearchPadding = MaximumRadiusCentimeters
		+ MaximumPlanarSpeedCentimetersPerSecond * (SafeSeconds + SafeMovementSeconds);
	const FBox2D SearchBounds = Bounds.ExpandBy(SearchPadding);
	const FIntPoint MinimumCell = MakeCell(SearchBounds.Min);
	const FIntPoint MaximumCell = MakeCell(SearchBounds.Max);
	for (int32 X = MinimumCell.X; X <= MaximumCell.X; ++X)
	{
		for (int32 Y = MinimumCell.Y; Y <= MaximumCell.Y; ++Y)
		{
			const TArray<int32, TInlineAllocator<8>>* Bucket = Grid.Find(FIntPoint(X, Y));
			if (!Bucket) continue;
			if (OutRawCandidateCount) *OutRawCandidateCount += Bucket->Num();
			for (const int32 Index : *Bucket)
			{
				if (!Bodies.IsValidIndex(Index)) continue;
				const FGuLiGroundMassBody Body = Bodies[Index].Extrapolated(SafeSeconds);
				const FVector2D StartCenter(Body.Location);
				const FVector2D EndCenter = StartCenter
					+ FVector2D(Body.Velocity) * SafeMovementSeconds;
				const FVector2D Extent(Body.RadiusCentimeters, Body.RadiusCentimeters);
				const FVector2D Minimum(
					FMath::Min(StartCenter.X, EndCenter.X),
					FMath::Min(StartCenter.Y, EndCenter.Y));
				const FVector2D Maximum(
					FMath::Max(StartCenter.X, EndCenter.X),
					FMath::Max(StartCenter.Y, EndCenter.Y));
				if (FBox2D(Minimum - Extent, Maximum + Extent).Intersect(Bounds)) OutBodies.Add(Body);
			}
		}
	}
}

bool FGuLiGroundMassSpatialIndex::Find(
	const FGuLiSoldierId SoldierId,
	const float ExtrapolationSeconds,
	FGuLiGroundMassBody& OutBody) const
{
	const int32* Index = IndexBySoldierId.Find(SoldierId.Value);
	if (!SoldierId.IsValid() || !Index || !Bodies.IsValidIndex(*Index)) return false;
	OutBody = Bodies[*Index].Extrapolated(ExtrapolationSeconds);
	return true;
}

bool GuLiGroundMassCollision::HasVerticalOverlap(
	const float CapsuleCenterZ,
	const float CapsuleHalfHeight,
	const float BodyBottomZ,
	const float BodyTopZ,
	const float Tolerance)
{
	if (!FMath::IsFinite(CapsuleCenterZ) || !FMath::IsFinite(CapsuleHalfHeight)
		|| !FMath::IsFinite(BodyBottomZ) || !FMath::IsFinite(BodyTopZ)
		|| CapsuleHalfHeight <= 0.0f || BodyTopZ <= BodyBottomZ)
	{
		return false;
	}
	return CapsuleCenterZ + CapsuleHalfHeight > BodyBottomZ + Tolerance
		&& CapsuleCenterZ - CapsuleHalfHeight < BodyTopZ - Tolerance;
}

FGuLiGroundMassMoveResult GuLiGroundMassCollision::ResolvePlanarMove(
	const FVector& Start,
	const FVector& IntendedDelta,
	const float CapsuleRadius,
	const float CapsuleHalfHeight,
	const float DeltaSeconds,
	const TConstArrayView<FGuLiGroundMassBody> Candidates,
	const FGuLiSoldierId IgnoredSoldier)
{
	FGuLiGroundMassMoveResult Result;
	Result.Delta = IntendedDelta;
	Result.CandidateCount = Candidates.Num();
	if (Start.ContainsNaN() || IntendedDelta.ContainsNaN()
		|| !FMath::IsFinite(CapsuleRadius) || CapsuleRadius <= 0.0f
		|| !FMath::IsFinite(CapsuleHalfHeight) || CapsuleHalfHeight <= 0.0f)
	{
		return Result;
	}

	const float SafeDeltaSeconds = FMath::IsFinite(DeltaSeconds)
		? FMath::Clamp(DeltaSeconds, 0.0f, 0.1f)
		: 0.0f;
	FVector2D Current(Start);
	FVector2D Accumulated = FVector2D::ZeroVector;
	FVector2D Remaining(IntendedDelta);

	// Resolve only a bounded amount per call so stale/malformed snapshots cannot teleport the player.
	for (int32 Iteration = 0; Iteration < MaximumCollisionIterations
		&& Result.DepenetrationCentimeters < MaximumDepenetrationCentimeters; ++Iteration)
	{
		float DeepestPenetration = 0.0f;
		FVector2D BestNormal = FVector2D::ZeroVector;
		uint32 BestId = MAX_uint32;
		for (const FGuLiGroundMassBody& Body : Candidates)
		{
			if (!Body.IsValid() || Body.SoldierId == IgnoredSoldier
				|| !HasVerticalOverlap(Start.Z, CapsuleHalfHeight, Body.BottomZ, Body.TopZ)) continue;
			const FVector2D Offset = Current - FVector2D(Body.Location);
			const float Distance = Offset.Size();
			const float Penetration = CapsuleRadius + Body.RadiusCentimeters - Distance;
			if (Penetration <= ContactToleranceCentimeters) continue;
			if (Penetration > DeepestPenetration
				|| (FMath::IsNearlyEqual(Penetration, DeepestPenetration)
					&& Body.SoldierId.Value < BestId))
			{
				DeepestPenetration = Penetration;
				BestId = Body.SoldierId.Value;
				BestNormal = Distance > UE_SMALL_NUMBER
					? Offset / Distance
					: FVector2D(Body.SoldierId.Value & 1u ? 1.0f : -1.0f, 0.0f);
			}
		}
		if (DeepestPenetration <= 0.0f) break;
		const float Push = FMath::Min(
			DeepestPenetration + ContactToleranceCentimeters,
			MaximumDepenetrationCentimeters - Result.DepenetrationCentimeters);
		Current += BestNormal * Push;
		Accumulated += BestNormal * Push;
		Result.DepenetrationCentimeters += Push;
	}

	float ElapsedFraction = 0.0f;
	float RemainingFraction = 1.0f;
	for (int32 Iteration = 0; Iteration < MaximumCollisionIterations; ++Iteration)
	{
		float BestTime = 2.0f;
		FVector2D BestNormal = FVector2D::ZeroVector;
		FVector2D BestBodyVelocity = FVector2D::ZeroVector;
		float BestRelativeTravel = 1.0f;
		FGuLiSoldierId BestSoldier;
		for (const FGuLiGroundMassBody& Body : Candidates)
		{
			if (!Body.IsValid() || Body.SoldierId == IgnoredSoldier) continue;
			const FVector BodyOffset = Body.Velocity * SafeDeltaSeconds * ElapsedFraction;
			const FVector2D BodyStart = FVector2D(Body.Location + BodyOffset);
			const FVector2D RelativeStart = Current - BodyStart;
			const FVector2D RelativeDelta = Remaining
				- FVector2D(Body.Velocity) * (SafeDeltaSeconds * RemainingFraction);
			float Time = 1.0f;
			FVector2D Normal;
			if (!IsApproachingCircle(
				RelativeStart,
				RelativeDelta,
				CapsuleRadius + Body.RadiusCentimeters,
				Time,
				Normal)) continue;

			const float AbsoluteFraction = ElapsedFraction + RemainingFraction * Time;
			const float CenterZ = Start.Z + IntendedDelta.Z * AbsoluteFraction;
			const float BodyZOffset = Body.Velocity.Z * SafeDeltaSeconds * AbsoluteFraction;
			if (!HasVerticalOverlap(
				CenterZ,
				CapsuleHalfHeight,
				Body.BottomZ + BodyZOffset,
				Body.TopZ + BodyZOffset)) continue;
			if (Time < BestTime - UE_KINDA_SMALL_NUMBER
				|| (FMath::IsNearlyEqual(Time, BestTime)
					&& Body.SoldierId.Value < BestSoldier.Value))
			{
				BestTime = Time;
				BestNormal = Normal;
				BestBodyVelocity = FVector2D(Body.Velocity);
				BestRelativeTravel = FMath::Max(1.0f, RelativeDelta.Size());
				BestSoldier = Body.SoldierId;
			}
		}

		if (!BestSoldier.IsValid())
		{
			Accumulated += Remaining;
			break;
		}

		const float SafeTime = FMath::Max(
			0.0f,
			BestTime - ContactToleranceCentimeters / BestRelativeTravel);
		const FVector2D SafeDelta = Remaining * SafeTime;
		Current += SafeDelta;
		Accumulated += SafeDelta;
		if (!Result.FirstHit.IsValid()) Result.FirstHit = BestSoldier;
		++Result.SideHitCount;

		ElapsedFraction += RemainingFraction * BestTime;
		RemainingFraction *= 1.0f - BestTime;
		Remaining *= 1.0f - BestTime;
		const FVector2D BodyRemaining = BestBodyVelocity
			* (SafeDeltaSeconds * RemainingFraction);
		FVector2D RelativeRemaining = Remaining - BodyRemaining;
		const float InwardDistance = FVector2D::DotProduct(RelativeRemaining, BestNormal);
		if (InwardDistance < 0.0f) RelativeRemaining -= BestNormal * InwardDistance;
		Remaining = RelativeRemaining + BodyRemaining;
	}

	Result.Delta.X = Accumulated.X;
	Result.Delta.Y = Accumulated.Y;
	return Result;
}

FGuLiGroundMassLandingResult GuLiGroundMassCollision::FindLandingSupport(
	const FVector& Start,
	const FVector& IntendedDelta,
	const float CapsuleHalfHeight,
	const float DeltaSeconds,
	const TConstArrayView<FGuLiGroundMassBody> Candidates)
{
	FGuLiGroundMassLandingResult Result;
	if (Start.ContainsNaN() || IntendedDelta.ContainsNaN() || IntendedDelta.Z >= 0.0f
		|| !FMath::IsFinite(CapsuleHalfHeight) || CapsuleHalfHeight <= 0.0f) return Result;
	const float SafeDeltaSeconds = FMath::IsFinite(DeltaSeconds)
		? FMath::Clamp(DeltaSeconds, 0.0f, 0.1f)
		: 0.0f;
	const float StartBottom = Start.Z - CapsuleHalfHeight;
	for (const FGuLiGroundMassBody& Body : Candidates)
	{
		if (!Body.IsValid()) continue;
		const float RelativeStart = StartBottom - Body.TopZ;
		const float RelativeDelta = IntendedDelta.Z - Body.Velocity.Z * SafeDeltaSeconds;
		if (RelativeStart < -ContactToleranceCentimeters || RelativeDelta >= -UE_SMALL_NUMBER) continue;
		const float Time = FMath::Clamp(-RelativeStart / RelativeDelta, 0.0f, 1.0f);
		if (RelativeStart + RelativeDelta > ContactToleranceCentimeters) continue;
		const FVector MechAtContact = Start + IntendedDelta * Time;
		const FVector BodyAtContact = Body.Location + Body.Velocity * (SafeDeltaSeconds * Time);
		if (FVector::DistSquared2D(MechAtContact, BodyAtContact)
			> FMath::Square(Body.RadiusCentimeters + ContactToleranceCentimeters)) continue;
		const float TopAtContact = Body.TopZ + Body.Velocity.Z * SafeDeltaSeconds * Time;
		if (!Result.IsValid() || TopAtContact > Result.TopZ + UE_KINDA_SMALL_NUMBER
			|| (FMath::IsNearlyEqual(TopAtContact, Result.TopZ, UE_KINDA_SMALL_NUMBER)
				&& Body.SoldierId.Value < Result.SoldierId.Value))
		{
			Result.SoldierId = Body.SoldierId;
			Result.Time = Time;
			Result.TopZ = TopAtContact;
			Result.BodyLocation = BodyAtContact;
		}
	}
	return Result;
}

void GuLiGroundMassCollision::SelectFriendlyYieldCandidates(
	const TConstArrayView<FGuLiGroundMassYieldCandidate> Candidates,
	const EGuLiTeam MechTeam,
	const FVector& MechLocation,
	const float MechRadiusCentimeters,
	const float ActivationPaddingCentimeters,
	const float MaximumHeightDifferenceCentimeters,
	const int32 MaximumCount,
	TArray<int32>& OutCandidateIndices)
{
	OutCandidateIndices.Reset();
	if ((MechTeam != EGuLiTeam::Red && MechTeam != EGuLiTeam::Blue)
		|| MechLocation.ContainsNaN() || MechRadiusCentimeters <= 0.0f || MaximumCount <= 0) return;
	struct FEntry { int32 Index = INDEX_NONE; double DistanceSquared = 0.0; uint32 Id = 0u; };
	auto IsBefore = [](const FEntry& Left, const FEntry& Right)
	{
		return Left.DistanceSquared != Right.DistanceSquared
			? Left.DistanceSquared < Right.DistanceSquared
			: Left.Id < Right.Id;
	};
	TArray<FEntry, TInlineAllocator<16>> Entries;
	Entries.Reserve(FMath::Min(MaximumCount, Candidates.Num()));
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FGuLiGroundMassYieldCandidate& Candidate = Candidates[Index];
		if (!Candidate.bCanYield || Candidate.Team != MechTeam || Candidate.StableSoldierId == 0u
			|| Candidate.Location.ContainsNaN() || Candidate.RadiusCentimeters <= 0.0f) continue;
		if (FMath::IsFinite(MaximumHeightDifferenceCentimeters)
			&& MaximumHeightDifferenceCentimeters > 0.0f
			&& FMath::Abs(Candidate.Location.Z - MechLocation.Z)
				>= MaximumHeightDifferenceCentimeters) continue;
		const float ActivationDistance = MechRadiusCentimeters
			+ Candidate.RadiusCentimeters + FMath::Max(0.0f, ActivationPaddingCentimeters);
		const double DistanceSquared = FVector::DistSquared2D(MechLocation, Candidate.Location);
		if (DistanceSquared > FMath::Square(static_cast<double>(ActivationDistance))) continue;
		const FEntry Entry{Index, DistanceSquared, Candidate.StableSoldierId};
		if (Entries.Num() == MaximumCount && !IsBefore(Entry, Entries.Last())) continue;
		int32 InsertIndex = 0;
		while (InsertIndex < Entries.Num() && !IsBefore(Entry, Entries[InsertIndex]))
			++InsertIndex;
		Entries.Insert(Entry, InsertIndex);
		if (Entries.Num() > MaximumCount)
			Entries.RemoveAt(MaximumCount, Entries.Num() - MaximumCount, EAllowShrinking::No);
	}
	OutCandidateIndices.Reserve(Entries.Num());
	for (const FEntry& Entry : Entries) OutCandidateIndices.Add(Entry.Index);
}

FVector GuLiGroundMassCollision::ComputeYieldTarget(
	const FVector& Anchor,
	const FVector& CurrentLocation,
	const FVector& MechLocation,
	const uint32 MechStableId,
	const uint32 SoldierStableId,
	const float RequiredCenterDistanceCentimeters,
	const float MaximumAnchorOffsetCentimeters)
{
	if (Anchor.ContainsNaN() || CurrentLocation.ContainsNaN() || MechLocation.ContainsNaN()
		|| RequiredCenterDistanceCentimeters <= 0.0f || MaximumAnchorOffsetCentimeters <= 0.0f)
		return CurrentLocation;
	FVector Away = CurrentLocation - MechLocation;
	Away.Z = 0.0f;
	const float CurrentDistance = Away.Size2D();
	if (CurrentDistance <= UE_SMALL_NUMBER)
	{
		const uint32 Axis = (MechStableId * 31u + SoldierStableId) & 3u;
		Away = Axis == 0u ? FVector::ForwardVector
			: Axis == 1u ? FVector::RightVector
			: Axis == 2u ? -FVector::ForwardVector
			: -FVector::RightVector;
	}
	else Away /= CurrentDistance;
	const float TargetDistance = FMath::Max(CurrentDistance, RequiredCenterDistanceCentimeters);
	FVector Target = MechLocation + Away * TargetDistance;
	Target.Z = CurrentLocation.Z;
	FVector FromAnchor = Target - Anchor;
	FromAnchor.Z = 0.0f;
	if (FromAnchor.SizeSquared2D() > FMath::Square(MaximumAnchorOffsetCentimeters))
		Target = Anchor + FromAnchor.GetSafeNormal2D() * MaximumAnchorOffsetCentimeters;
	Target.Z = CurrentLocation.Z;
	return Target;
}

FVector GuLiGroundMassCollision::ResolveYieldTargetWithoutPressure(
	const FVector& Anchor,
	const FVector& CurrentTarget,
	const double CurrentSimulationSeconds,
	const double LastPressureSimulationSeconds,
	const double ReturnDelaySeconds,
	bool& bOutReturning)
{
	bOutReturning = !Anchor.ContainsNaN()
		&& FMath::IsFinite(CurrentSimulationSeconds)
		&& FMath::IsFinite(LastPressureSimulationSeconds)
		&& LastPressureSimulationSeconds >= 0.0
		&& FMath::IsFinite(ReturnDelaySeconds)
		&& ReturnDelaySeconds >= 0.0
		&& CurrentSimulationSeconds - LastPressureSimulationSeconds >= ReturnDelaySeconds;
	return bOutReturning ? Anchor : CurrentTarget;
}
