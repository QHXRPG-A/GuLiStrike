#include "Gameplay/Navigation/GuLiGroundMassCollisionTypes.h"

bool FGuLiGroundMassBody::IsValid() const
{
	return SoldierId.IsValid() && !Location.ContainsNaN() && !Velocity.ContainsNaN() && !Rotation.ContainsNaN() &&
		   FMath::IsFinite(SampleSimulationSeconds) && FMath::IsFinite(RadiusCentimeters) && RadiusCentimeters > 0.0f &&
		   FMath::IsFinite(BottomZ) && FMath::IsFinite(TopZ) && TopZ > BottomZ;
}

float FGuLiGroundMassBody::RemainingExtrapolationSeconds() const
{
	return FMath::Max(0.0f, GuLiGroundMassCollision::MaximumExtrapolationSeconds - ExtrapolatedSeconds);
}

FVector FGuLiGroundMassBody::TranslationDuring(const float Seconds) const
{
	checkSlow(FMath::IsFinite(Seconds));
	return Velocity * FMath::Clamp(Seconds, 0.0f, RemainingExtrapolationSeconds());
}

FGuLiGroundMassBody FGuLiGroundMassBody::Extrapolated(const float Seconds) const
{
	checkSlow(FMath::IsFinite(Seconds));
	FGuLiGroundMassBody Result = *this;
	const float Advance = FMath::Clamp(Seconds, 0.0f, RemainingExtrapolationSeconds());
	const FVector Offset = Velocity * Advance;
	Result.Location += Offset;
	Result.BottomZ += Offset.Z;
	Result.TopZ += Offset.Z;
	Result.ExtrapolatedSeconds += Advance;
	return Result;
}

void GuLiGroundMassCollision::ResolveBodyBounds(const FBox &ModelBounds, FGuLiGroundMassBody &Body)
{
	if (ModelBounds.IsValid)
	{
		// Model bounds already include PresentationScale. The logical pose has unit scale.
		const FBox Bounds = ModelBounds.TransformBy(FTransform(Body.Rotation, Body.Location));
		if (Bounds.IsValid && FMath::IsFinite(Bounds.Min.Z) && FMath::IsFinite(Bounds.Max.Z) &&
			Bounds.Max.Z > Bounds.Min.Z)
		{
			Body.BottomZ = Bounds.Min.Z;
			Body.TopZ = Bounds.Max.Z;
			return;
		}
	}
	Body.BottomZ = Body.Location.Z;
	Body.TopZ = Body.BottomZ + FMath::Max(200.0f, 2.0f * Body.RadiusCentimeters);
}

namespace
{
FIntPoint MakeCell(const FVector2D &Location)
{
	return FIntPoint(FMath::FloorToInt(Location.X / 1000.0), FMath::FloorToInt(Location.Y / 1000.0));
}

bool CircleInterval(const FVector2D &P, const FVector2D &D, double Radius, double &Enter, double &Exit)
{
	const double A = D.SizeSquared();
	const double C = P.SizeSquared() - Radius * Radius;
	if (A <= UE_DOUBLE_SMALL_NUMBER)
		return C <= 0.0;
	const double B = FVector2D::DotProduct(P, D);
	const double Disc = B * B - A * C;
	if (Disc < 0.0)
		return false;
	const double Root = FMath::Sqrt(Disc);
	Enter = FMath::Max(Enter, (-B - Root) / A);
	Exit = FMath::Min(Exit, (-B + Root) / A);
	return Enter <= Exit;
}

bool VerticalInterval(double Z, double D, double Bottom, double Top, double &Enter, double &Exit)
{
	if (FMath::Abs(D) <= UE_DOUBLE_SMALL_NUMBER)
		return Z > Bottom && Z < Top;
	double A = (Bottom - Z) / D;
	double B = (Top - Z) / D;
	if (A > B)
		Swap(A, B);
	Enter = FMath::Max(Enter, A);
	Exit = FMath::Min(Exit, B);
	return Enter <= Exit;
}

bool IsEarlier(const FGuLiGroundMassContact &A, const FGuLiGroundMassContact &B)
{
	if (!B.IsValid())
		return true;
	if (!FMath::IsNearlyEqual(A.Time, B.Time, 1.e-5f))
		return A.Time < B.Time;
	if (A.Kind != B.Kind)
		return A.Kind == EGuLiGroundMassContactKind::Top;
	if (A.Kind == EGuLiGroundMassContactKind::Top && !FMath::IsNearlyEqual(A.Body.TopZ, B.Body.TopZ))
		return A.Body.TopZ > B.Body.TopZ;
	return A.Body.SoldierId.Value < B.Body.SoldierId.Value;
}
} // namespace

void FGuLiGroundMassSpatialIndex::Reset()
{
	Bodies.Reset();
	IndexBySoldierId.Reset();
	Grid.Reset();
	MaximumRadiusCentimeters = 0.0f;
	MaximumPlanarSpeedCentimetersPerSecond = 0.0f;
}

void FGuLiGroundMassSpatialIndex::Rebuild(TConstArrayView<FGuLiGroundMassBody> InBodies, double ReferenceSeconds)
{
	Reset();
	ReferenceSimulationSeconds = ReferenceSeconds;
	Bodies.Reserve(InBodies.Num());
	IndexBySoldierId.Reserve(InBodies.Num());
	for (const auto &Body : InBodies)
	{
		if (!ensureMsgf(Body.IsValid() && !IndexBySoldierId.Contains(Body.SoldierId.Value),
						TEXT("Invalid or duplicate Mass collision body")))
			continue;
		const int32 Index = Bodies.Add(Body);
		IndexBySoldierId.Add(Body.SoldierId.Value, Index);
		Grid.FindOrAdd(MakeCell(FVector2D(Body.Location))).Add(Index);
		MaximumRadiusCentimeters = FMath::Max(MaximumRadiusCentimeters, Body.RadiusCentimeters);
		MaximumPlanarSpeedCentimetersPerSecond =
			FMath::Max(MaximumPlanarSpeedCentimetersPerSecond, Body.Velocity.Size2D());
	}
}

void FGuLiGroundMassSpatialIndex::Query(const FBox2D &Bounds, float Age, float Duration,
										TArray<FGuLiGroundMassBody> &OutBodies, int32 *RawCount) const
{
	OutBodies.Reset();
	if (RawCount)
		*RawCount = 0;
	if (!Bounds.bIsValid || Bodies.IsEmpty())
		return;
	checkSlow(FMath::IsFinite(Age) && FMath::IsFinite(Duration));
	const float Padding = MaximumRadiusCentimeters +
						  MaximumPlanarSpeedCentimetersPerSecond *
							  FMath::Clamp(Age + Duration, 0.0f, GuLiGroundMassCollision::MaximumExtrapolationSeconds);
	const FBox2D Search = Bounds.ExpandBy(Padding);
	const FIntPoint First = MakeCell(Search.Min), Last = MakeCell(Search.Max);
	for (int32 X = First.X; X <= Last.X; ++X)
		for (int32 Y = First.Y; Y <= Last.Y; ++Y)
		{
			const auto *Bucket = Grid.Find(FIntPoint(X, Y));
			if (!Bucket)
				continue;
			if (RawCount)
				*RawCount += Bucket->Num();
			for (int32 Index : *Bucket)
			{
				checkSlow(Bodies.IsValidIndex(Index));
				const auto Body = Bodies[Index].Extrapolated(
					Age - static_cast<float>(
							  FMath::Max(0.0, Bodies[Index].SampleSimulationSeconds - ReferenceSimulationSeconds)));
				const FVector2D Start(Body.Location), End(Body.Location + Body.TranslationDuring(Duration));
				const FVector2D Extent(Body.RadiusCentimeters);
				const FVector2D Min(FMath::Min(Start.X, End.X), FMath::Min(Start.Y, End.Y));
				const FVector2D Max(FMath::Max(Start.X, End.X), FMath::Max(Start.Y, End.Y));
				if (FBox2D(Min - Extent, Max + Extent).Intersect(Bounds))
					OutBodies.Add(Body);
			}
		}
}

bool FGuLiGroundMassSpatialIndex::Find(FGuLiSoldierId Id, float Age, FGuLiGroundMassBody &Out) const
{
	const int32 *Index = IndexBySoldierId.Find(Id.Value);
	if (!Index)
		return false;
	checkSlow(Bodies.IsValidIndex(*Index));
	Out = Bodies[*Index].Extrapolated(
		Age - static_cast<float>(FMath::Max(0.0, Bodies[*Index].SampleSimulationSeconds - ReferenceSimulationSeconds)));
	return true;
}

void FGuLiGroundMassSnapshot::Query(const FBox2D &Bounds, double StartSeconds, float Duration,
									TArray<FGuLiGroundMassBody> &Out, int32 *RawCount) const
{
	Out.Reset();
	if (RawCount)
		*RawCount = 0;
	const float Age = static_cast<float>(FMath::Max(0.0, StartSeconds - SimulationSeconds));
	if (Index)
		Index->Query(Bounds, Age, Duration, Out, RawCount);
	if (Overrides.IsEmpty())
		return;
	Out.RemoveAll([this](const auto &Body) { return Overrides.Contains(Body.SoldierId.Value); });
	if (OverrideIndex)
	{
		TArray<FGuLiGroundMassBody> Extra;
		int32 ExtraCount = 0;
		OverrideIndex->Query(Bounds, Age, Duration, Extra, &ExtraCount);
		Out.Append(Extra);
		if (RawCount)
			*RawCount += ExtraCount;
	}
}

bool FGuLiGroundMassSnapshot::Find(FGuLiSoldierId Id, double AtSeconds, FGuLiGroundMassBody &Out) const
{
	const float Age = static_cast<float>(FMath::Max(0.0, AtSeconds - SimulationSeconds));
	if (const auto *Override = Overrides.Find(Id.Value))
	{
		if (!Override->IsValid())
			return false;
		Out = Override->Extrapolated(
			Age - static_cast<float>(FMath::Max(0.0, Override->SampleSimulationSeconds - SimulationSeconds)));
		return true;
	}
	return Index && Index->Find(Id, Age, Out);
}

bool GuLiGroundMassCollision::HasVerticalOverlap(float Z, float HalfHeight, float Bottom, float Top, float Tolerance)
{
	return Z + HalfHeight > Bottom + Tolerance && Z - HalfHeight < Top - Tolerance;
}

FGuLiGroundMassContact GuLiGroundMassCollision::Sweep(const FVector &Start, const FVector &Delta, float Radius,
													  float HalfHeight, float Duration,
													  TConstArrayView<FGuLiGroundMassBody> Bodies,
													  FGuLiSoldierId Ignored, bool bAllowLanding)
{
	checkSlow(!Start.ContainsNaN() && !Delta.ContainsNaN() && Duration >= 0.0f);
	FGuLiGroundMassContact Best;
	for (const auto &Original : Bodies)
	{
		if (Original.SoldierId == Ignored)
			continue;
		// Split at the extrapolation deadline; replacing this with average velocity misses contacts.
		const float Stop = Duration > SMALL_NUMBER
							   ? FMath::Clamp(Original.RemainingExtrapolationSeconds() / Duration, 0.0f, 1.0f)
							   : 1.0f;
		const float Cuts[] = {0.0f, Stop, 1.0f};
		for (int32 Segment = 0; Segment < 2; ++Segment)
		{
			const float Begin = Cuts[Segment], Length = Cuts[Segment + 1] - Begin;
			if (Length <= SMALL_NUMBER)
				continue;
			const auto Body = Original.Extrapolated(Duration * Begin);
			const FVector P = Start + Delta * Begin;
			const FVector D = Delta * Length;
			const FVector BD = Body.TranslationDuring(Duration * Length);
			const FVector Relative = P - Body.Location, RD = D - BD;
			double XYEnter = 0.0, XYExit = 1.0;
			if (!CircleInterval(FVector2D(Relative), FVector2D(RD), Radius + Body.RadiusCentimeters, XYEnter, XYExit))
				continue;
			if (bAllowLanding && Delta.Z < 0.0f && RD.Z < -SMALL_NUMBER)
			{
				const double Gap = P.Z - HalfHeight - Body.TopZ;
				const double TopTime = -Gap / RD.Z;
				if (Gap >= -ContactToleranceCentimeters && TopTime >= -SMALL_NUMBER && TopTime <= 1.0)
				{
					const float T = FMath::Clamp(static_cast<float>(TopTime), 0.0f, 1.0f);
					if (FVector2D(Relative + RD * T).SizeSquared() <=
						FMath::Square(Body.RadiusCentimeters + ContactToleranceCentimeters))
					{
						FGuLiGroundMassContact Hit;
						Hit.Kind = EGuLiGroundMassContactKind::Top;
						Hit.Time = Begin + Length * T;
						Hit.Normal = FVector::UpVector;
						Hit.Body = Original.Extrapolated(Duration * Hit.Time);
						if (IsEarlier(Hit, Best))
							Best = Hit;
						// Do not replace a legitimate landing with the same cylinder's expanded side.
						continue;
					}
				}
			}
			double Enter = XYEnter, Exit = XYExit;
			if (!VerticalInterval(P.Z, RD.Z, Body.BottomZ - HalfHeight, Body.TopZ + HalfHeight, Enter, Exit) ||
				Enter < 0.0 || Enter > 1.0 || Exit < 0.0)
				continue;
			const FVector2D Offset(Relative + RD * Enter);
			const double Distance = Offset.Size();
			FVector Normal = Distance > SMALL_NUMBER ? FVector(Offset.X / Distance, Offset.Y / Distance, 0.0)
													 : FVector(Original.SoldierId.Value & 1u ? 1.0 : -1.0, 0.0, 0.0);
			const float Penetration = FMath::Max(0.0f, Radius + Body.RadiusCentimeters - static_cast<float>(Distance));
			const bool bInitialVerticalOverlap = HasVerticalOverlap(P.Z, HalfHeight, Body.BottomZ, Body.TopZ, 0.0f);
			if (Enter <= SMALL_NUMBER && Penetration <= ContactToleranceCentimeters &&
				FVector::DotProduct(RD, Normal) >= -SMALL_NUMBER)
				continue;
			if (Enter <= SMALL_NUMBER && !bInitialVerticalOverlap && FMath::IsNearlyZero(RD.Z))
				continue;
			FGuLiGroundMassContact Hit;
			Hit.Kind = EGuLiGroundMassContactKind::Side;
			Hit.Time = Begin + Length * static_cast<float>(Enter);
			Hit.Normal = Normal;
			Hit.Penetration = Penetration;
			Hit.Body = Original.Extrapolated(Duration * Hit.Time);
			if (IsEarlier(Hit, Best))
				Best = Hit;
		}
	}
	return Best;
}

FGuLiGroundMassMoveResult GuLiGroundMassCollision::ResolvePlanarMove(const FVector &Start, const FVector &Delta,
																	 float Radius, float HalfHeight, float Duration,
																	 TConstArrayView<FGuLiGroundMassBody> Candidates,
																	 FGuLiSoldierId Ignored)
{
	FGuLiGroundMassMoveResult Result;
	Result.CandidateCount = Candidates.Num();
	FVector Current = Start, Remaining = Delta;
	float Elapsed = 0.0f, Left = Duration;
	for (int32 Iteration = 0; Iteration < MaximumCollisionIterations; ++Iteration)
	{
		TArray<FGuLiGroundMassBody> Bodies;
		Bodies.Reserve(Candidates.Num());
		for (const auto &B : Candidates)
			Bodies.Add(B.Extrapolated(Elapsed));
		const auto Hit = Sweep(Current, Remaining, Radius, HalfHeight, Left, Bodies, Ignored, false);
		if (!Hit.IsValid())
		{
			Current += Remaining;
			break;
		}
		if (!Result.FirstHit.IsValid())
			Result.FirstHit = Hit.Body.SoldierId;
		++Result.SideHitCount;
		const float Travel = Hit.Time;
		Current += Remaining * Travel;
		const float Consumed = Left * Travel;
		Elapsed += Consumed;
		Left -= Consumed;
		Remaining *= 1.0f - Travel;
		const float Push = Hit.Penetration + ContactToleranceCentimeters;
		if (Result.DepenetrationCentimeters + Push > MaximumDepenetrationCentimeters)
		{
			const float Available = MaximumDepenetrationCentimeters - Result.DepenetrationCentimeters;
			Current += Hit.Normal * Available;
			Result.DepenetrationCentimeters += Available;
			break;
		}
		Current += Hit.Normal * Push;
		Result.DepenetrationCentimeters += Push;
		const FVector BodyRemaining = Hit.Body.TranslationDuring(Left);
		FVector Relative = Remaining - BodyRemaining;
		Relative -= Hit.Normal * FMath::Min(0.0, FVector::DotProduct(Relative, Hit.Normal));
		Remaining = Relative + BodyRemaining;
	}
	Result.Delta = Current - Start;
	return Result;
}

FGuLiGroundMassLandingResult GuLiGroundMassCollision::FindLandingSupport(
	const FVector &Start, const FVector &Delta, float HalfHeight, float Duration,
	TConstArrayView<FGuLiGroundMassBody> Candidates)
{
	FGuLiGroundMassLandingResult Result;
	FGuLiGroundMassContact Best;
	for (const auto &Body : Candidates)
	{
		const auto Hit = Sweep(Start, Delta, 0.0f, HalfHeight, Duration, MakeArrayView(&Body, 1), {}, true);
		if (Hit.Kind == EGuLiGroundMassContactKind::Top && IsEarlier(Hit, Best))
			Best = Hit;
	}
	if (Best.IsValid())
	{
		Result.SoldierId = Best.Body.SoldierId;
		Result.Time = Best.Time;
		Result.TopZ = Best.Body.TopZ;
		Result.BodyLocation = Best.Body.Location;
	}
	return Result;
}

void GuLiGroundMassCollision::SelectFriendlyYieldCandidates(
	const TConstArrayView<FGuLiGroundMassYieldCandidate> Candidates, const EGuLiTeam MechTeam,
	const FVector &MechLocation, const float MechRadiusCentimeters, const float ActivationPaddingCentimeters,
	const float MaximumHeightDifferenceCentimeters, const int32 MaximumCount, TArray<int32> &OutCandidateIndices)
{
	OutCandidateIndices.Reset();
	if ((MechTeam != EGuLiTeam::Red && MechTeam != EGuLiTeam::Blue) || MechLocation.ContainsNaN() ||
		MechRadiusCentimeters <= 0.0f || MaximumCount <= 0)
		return;
	struct FEntry
	{
		int32 Index = INDEX_NONE;
		double DistanceSquared = 0.0;
		uint32 Id = 0u;
	};
	auto IsBefore = [](const FEntry &Left, const FEntry &Right)
	{
		return Left.DistanceSquared != Right.DistanceSquared ? Left.DistanceSquared < Right.DistanceSquared
															 : Left.Id < Right.Id;
	};
	TArray<FEntry, TInlineAllocator<16>> Entries;
	Entries.Reserve(FMath::Min(MaximumCount, Candidates.Num()));
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FGuLiGroundMassYieldCandidate &Candidate = Candidates[Index];
		if (!Candidate.bCanYield || Candidate.Team != MechTeam || Candidate.StableSoldierId == 0u ||
			Candidate.Location.ContainsNaN() || Candidate.RadiusCentimeters <= 0.0f)
			continue;
		if (FMath::IsFinite(MaximumHeightDifferenceCentimeters) && MaximumHeightDifferenceCentimeters > 0.0f &&
			FMath::Abs(Candidate.Location.Z - MechLocation.Z) >= MaximumHeightDifferenceCentimeters)
			continue;
		const float ActivationDistance =
			MechRadiusCentimeters + Candidate.RadiusCentimeters + FMath::Max(0.0f, ActivationPaddingCentimeters);
		const double DistanceSquared = FVector::DistSquared2D(MechLocation, Candidate.Location);
		if (DistanceSquared > FMath::Square(static_cast<double>(ActivationDistance)))
			continue;
		const FEntry Entry{Index, DistanceSquared, Candidate.StableSoldierId};
		if (Entries.Num() == MaximumCount && !IsBefore(Entry, Entries.Last()))
			continue;
		int32 InsertIndex = 0;
		while (InsertIndex < Entries.Num() && !IsBefore(Entry, Entries[InsertIndex]))
			++InsertIndex;
		Entries.Insert(Entry, InsertIndex);
		if (Entries.Num() > MaximumCount)
			Entries.RemoveAt(MaximumCount, Entries.Num() - MaximumCount, EAllowShrinking::No);
	}
	OutCandidateIndices.Reserve(Entries.Num());
	for (const FEntry &Entry : Entries)
		OutCandidateIndices.Add(Entry.Index);
}

FVector GuLiGroundMassCollision::ComputeYieldTarget(const FVector &Anchor, const FVector &CurrentLocation,
													const FVector &MechLocation, const uint32 MechStableId,
													const uint32 SoldierStableId,
													const float RequiredCenterDistanceCentimeters,
													const float MaximumAnchorOffsetCentimeters)
{
	if (Anchor.ContainsNaN() || CurrentLocation.ContainsNaN() || MechLocation.ContainsNaN() ||
		RequiredCenterDistanceCentimeters <= 0.0f || MaximumAnchorOffsetCentimeters <= 0.0f)
		return CurrentLocation;
	FVector Away = CurrentLocation - MechLocation;
	Away.Z = 0.0f;
	const float CurrentDistance = Away.Size2D();
	if (CurrentDistance <= UE_SMALL_NUMBER)
	{
		const uint32 Axis = (MechStableId * 31u + SoldierStableId) & 3u;
		Away = Axis == 0u	? FVector::ForwardVector
			   : Axis == 1u ? FVector::RightVector
			   : Axis == 2u ? -FVector::ForwardVector
							: -FVector::RightVector;
	}
	else
		Away /= CurrentDistance;
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

FVector GuLiGroundMassCollision::ResolveYieldTargetWithoutPressure(const FVector &Anchor, const FVector &CurrentTarget,
																   const double CurrentSimulationSeconds,
																   const double LastPressureSimulationSeconds,
																   const double ReturnDelaySeconds, bool &bOutReturning)
{
	bOutReturning = !Anchor.ContainsNaN() && FMath::IsFinite(CurrentSimulationSeconds) &&
					FMath::IsFinite(LastPressureSimulationSeconds) && LastPressureSimulationSeconds >= 0.0 &&
					FMath::IsFinite(ReturnDelaySeconds) && ReturnDelaySeconds >= 0.0 &&
					CurrentSimulationSeconds - LastPressureSimulationSeconds >= ReturnDelaySeconds;
	return bOutReturning ? Anchor : CurrentTarget;
}
