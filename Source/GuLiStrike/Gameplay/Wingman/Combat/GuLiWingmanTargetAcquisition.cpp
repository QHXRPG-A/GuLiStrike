// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Wingman/Combat/GuLiWingmanTargetAcquisition.h"

#include "Battle/Combat/GuLiCombatDamageLedger.h"

namespace
{
	constexpr double MinimumSpatialHashCellSizeCentimeters = 100.0;
	constexpr double MaximumSpatialHashCellSizeCentimeters = 100000.0;
	constexpr int32 MaximumSpatialHashAxisCellsPerQuery = 129;

	bool IsFiniteTransform(const FTransform& Transform)
	{
		return !Transform.ContainsNaN()
			&& Transform.GetRotation().IsNormalized();
	}
}

bool FGuLiWingmanTargetSpatialHash::Build(
	const TConstArrayView<FGuLiWingmanTargetObservation> Observations,
	const double InCellSizeCentimeters)
{
	Reset();
	if (!FMath::IsFinite(InCellSizeCentimeters)
		|| InCellSizeCentimeters < MinimumSpatialHashCellSizeCentimeters
		|| InCellSizeCentimeters > MaximumSpatialHashCellSizeCentimeters)
	{
		return false;
	}

	CellSizeCentimeters = InCellSizeCentimeters;
	StableObservations.Reserve(Observations.Num());
	for (const FGuLiWingmanTargetObservation& Observation : Observations)
	{
		if (Observation.Target.IsValid() && !Observation.Location.ContainsNaN())
		{
			StableObservations.Add(Observation);
		}
	}
	StableObservations.Sort([](
		const FGuLiWingmanTargetObservation& Lhs,
		const FGuLiWingmanTargetObservation& Rhs)
	{
		return FGuLiWingmanTargetAcquisition::IsStableHandleLess(Lhs.Target, Rhs.Target);
	});

	for (int32 Index = 0; Index < StableObservations.Num(); ++Index)
	{
		Buckets.FindOrAdd(CellForLocation(
			StableObservations[Index].Location,
			CellSizeCentimeters)).Add(Index);
	}
	return true;
}

bool FGuLiWingmanTargetSpatialHash::QuerySphere(
	const FVector& Center,
	const double RadiusCentimeters,
	TArray<FGuLiWingmanTargetObservation>& OutCandidates) const
{
	OutCandidates.Reset();
	if (CellSizeCentimeters <= 0.0 || Center.ContainsNaN()
		|| !FMath::IsFinite(RadiusCentimeters) || RadiusCentimeters <= 0.0)
	{
		return false;
	}

	const FVector Extent(RadiusCentimeters);
	const FIntVector MinimumCell = CellForLocation(Center - Extent, CellSizeCentimeters);
	const FIntVector MaximumCell = CellForLocation(Center + Extent, CellSizeCentimeters);
	const int64 CellCountX = static_cast<int64>(MaximumCell.X) - MinimumCell.X + 1;
	const int64 CellCountY = static_cast<int64>(MaximumCell.Y) - MinimumCell.Y + 1;
	const int64 CellCountZ = static_cast<int64>(MaximumCell.Z) - MinimumCell.Z + 1;
	if (CellCountX <= 0 || CellCountY <= 0 || CellCountZ <= 0
		|| CellCountX > MaximumSpatialHashAxisCellsPerQuery
		|| CellCountY > MaximumSpatialHashAxisCellsPerQuery
		|| CellCountZ > MaximumSpatialHashAxisCellsPerQuery)
	{
		return false;
	}

	TArray<int32> CandidateIndices;
	for (int32 X = MinimumCell.X; X <= MaximumCell.X; ++X)
	{
		for (int32 Y = MinimumCell.Y; Y <= MaximumCell.Y; ++Y)
		{
			for (int32 Z = MinimumCell.Z; Z <= MaximumCell.Z; ++Z)
			{
				if (const TArray<int32>* Bucket = Buckets.Find(FIntVector(X, Y, Z)))
				{
					CandidateIndices.Append(*Bucket);
				}
			}
		}
	}

	// Cell traversal is geometric; restore canonical protocol order before
	// selection so equal-distance behavior is independent of emitter position.
	CandidateIndices.Sort();
	const double RadiusSquared = FMath::Square(RadiusCentimeters);
	for (const int32 Index : CandidateIndices)
	{
		if (!StableObservations.IsValidIndex(Index))
		{
			continue;
		}
		const double DistanceSquared = FVector::DistSquared(
			Center,
			StableObservations[Index].Location);
		if (FMath::IsFinite(DistanceSquared) && DistanceSquared <= RadiusSquared)
		{
			OutCandidates.Add(StableObservations[Index]);
		}
	}
	return true;
}

void FGuLiWingmanTargetSpatialHash::Reset()
{
	CellSizeCentimeters = 0.0;
	StableObservations.Reset();
	Buckets.Reset();
}

FIntVector FGuLiWingmanTargetSpatialHash::CellForLocation(
	const FVector& Location,
	const double CellSize)
{
	return FIntVector(
		FMath::FloorToInt(Location.X / CellSize),
		FMath::FloorToInt(Location.Y / CellSize),
		FMath::FloorToInt(Location.Z / CellSize));
}

bool FGuLiWingmanTargetAcquisition::SelectBestTarget(
	const FVector& EmitterLocation,
	const FVector& EmitterForward,
	const EGuLiTeam EmitterTeam,
	const FGuLiWingmanWeaponRuntimeConfig& Weapon,
	const TConstArrayView<FGuLiWingmanTargetObservation> Observations,
	FLineOfSightPredicate HasLineOfSight,
	FGuLiWingmanTargetObservation& OutTarget)
{
	OutTarget = FGuLiWingmanTargetObservation{};
	if (EmitterLocation.ContainsNaN() || EmitterForward.ContainsNaN()
		|| EmitterTeam == EGuLiTeam::Unassigned || !Weapon.IsWellFormed())
	{
		return false;
	}

	const FVector SafeForward = EmitterForward.GetSafeNormal();
	if (SafeForward.IsNearlyZero())
	{
		return false;
	}
	const double MaximumDistanceSquared = FMath::Square(
		static_cast<double>(Weapon.RangeCentimeters));
	const double MinimumAimDot = FMath::Cos(FMath::DegreesToRadians(
		static_cast<double>(Weapon.TargetConeHalfAngleDegrees)));
	double BestDistanceSquared = TNumericLimits<double>::Max();
	bool bFound = false;

	for (const FGuLiWingmanTargetObservation& Observation : Observations)
	{
		if (!Observation.Target.IsValid() || !Observation.bAlive
			|| !Observation.bFromAcceptedOrReliableState
			|| Observation.Team == EGuLiTeam::Unassigned
			|| Observation.Team == EmitterTeam
			|| Observation.Location.ContainsNaN())
		{
			continue;
		}

		const FVector ToTarget = Observation.Location - EmitterLocation;
		const double DistanceSquared = ToTarget.SizeSquared();
		if (!FMath::IsFinite(DistanceSquared) || DistanceSquared <= UE_DOUBLE_SMALL_NUMBER
			|| DistanceSquared > MaximumDistanceSquared)
		{
			continue;
		}
		const double AimDot = FVector::DotProduct(
			SafeForward,
			ToTarget / FMath::Sqrt(DistanceSquared));
		if (!FMath::IsFinite(AimDot) || AimDot < MinimumAimDot)
		{
			continue;
		}
		if (Weapon.bRequiresLineOfSight && !HasLineOfSight(Observation))
		{
			continue;
		}

		const bool bDistanceTie = FMath::IsNearlyEqual(
			DistanceSquared,
			BestDistanceSquared,
			1.0);
		if (!bFound || (DistanceSquared < BestDistanceSquared && !bDistanceTie)
			|| (bDistanceTie && IsStableHandleLess(Observation.Target, OutTarget.Target)))
		{
			OutTarget = Observation;
			BestDistanceSquared = DistanceSquared;
			bFound = true;
		}
	}
	return bFound;
}

bool FGuLiWingmanTargetAcquisition::InitializeStaggeredSchedule(
	const double NowSeconds,
	const int32 EmitterCount,
	TArray<double>& OutNextScanSeconds)
{
	OutNextScanSeconds.Reset();
	if (!FMath::IsFinite(NowSeconds) || NowSeconds < 0.0 || EmitterCount <= 0)
	{
		return false;
	}
	OutNextScanSeconds.SetNumUninitialized(EmitterCount);
	for (int32 Index = 0; Index < EmitterCount; ++Index)
	{
		OutNextScanSeconds[Index] = NowSeconds
			+ TargetScanIntervalSeconds * static_cast<double>(Index)
				/ static_cast<double>(EmitterCount);
	}
	return true;
}

bool FGuLiWingmanTargetAcquisition::ConsumeDueScans(
	const double NowSeconds,
	TArray<double>& InOutNextScanSeconds,
	TArray<int32>& OutDueEmitterIndices)
{
	OutDueEmitterIndices.Reset();
	if (!FMath::IsFinite(NowSeconds) || NowSeconds < 0.0
		|| InOutNextScanSeconds.IsEmpty())
	{
		return false;
	}

	for (int32 Index = 0; Index < InOutNextScanSeconds.Num(); ++Index)
	{
		double& NextScanSeconds = InOutNextScanSeconds[Index];
		if (!FMath::IsFinite(NextScanSeconds) || NextScanSeconds < 0.0)
		{
			return false;
		}
		if (NowSeconds + UE_DOUBLE_SMALL_NUMBER < NextScanSeconds)
		{
			continue;
		}

		OutDueEmitterIndices.Add(Index);
		const double MissedIntervals = FMath::FloorToDouble(
			FMath::Max(0.0, NowSeconds - NextScanSeconds) / TargetScanIntervalSeconds);
		NextScanSeconds += (MissedIntervals + 1.0) * TargetScanIntervalSeconds;
		// Floating-point division can round an exact boundary just below an integer.
		// Never leave an already-consumed emitter due at the same timestamp.
		if (NextScanSeconds <= NowSeconds + UE_DOUBLE_SMALL_NUMBER)
		{
			NextScanSeconds += TargetScanIntervalSeconds;
		}
	}
	return true;
}

int32 FGuLiWingmanTargetAcquisition::AppendCommanderObservations(
	const TConstArrayView<FGuLiSoldierStateItem> ReliableStates,
	const uint32 SnapshotMatchEpoch,
	const uint32 ExpectedMatchEpoch,
	FCommanderAuthoritativeTransformReader ReadAuthoritativeTransform,
	TArray<FGuLiWingmanTargetObservation>& InOutObservations)
{
	if (SnapshotMatchEpoch == 0u || SnapshotMatchEpoch != ExpectedMatchEpoch)
	{
		return 0;
	}

	const int32 InitialCount = InOutObservations.Num();
	for (const FGuLiSoldierStateItem& State : ReliableStates)
	{
		if (!State.SoldierId.IsValid() || !State.IsAlive()
			|| State.Team == EGuLiTeam::Unassigned)
		{
			continue;
		}
		FTransform AuthoritativeTransform;
		if (!ReadAuthoritativeTransform(State.SoldierId, AuthoritativeTransform)
			|| !IsFiniteTransform(AuthoritativeTransform))
		{
			continue;
		}

		FGuLiWingmanTargetObservation& Observation = InOutObservations.AddDefaulted_GetRef();
		Observation.Target = GuLiCombatTargets::MakeCommanderSoldierTargetHandle(
			ExpectedMatchEpoch,
			State.SoldierId.Value);
		Observation.Team = State.Team;
		Observation.Location = AuthoritativeTransform.GetLocation();
		Observation.Source = EGuLiWingmanTargetObservationSource::CommanderAuthoritativePose;
		Observation.bAlive = true;
		Observation.bFromAcceptedOrReliableState = Observation.Target.IsValid();
		if (!Observation.bFromAcceptedOrReliableState)
		{
			InOutObservations.Pop(EAllowShrinking::No);
		}
	}
	return InOutObservations.Num() - InitialCount;
}

bool FGuLiWingmanTargetAcquisition::IsStableHandleLess(
	const FGuLiTargetHandle& Lhs,
	const FGuLiTargetHandle& Rhs)
{
	if (Lhs.Kind != Rhs.Kind)
	{
		return static_cast<uint8>(Lhs.Kind) < static_cast<uint8>(Rhs.Kind);
	}
	if (Lhs.AuthorityId.A != Rhs.AuthorityId.A) return Lhs.AuthorityId.A < Rhs.AuthorityId.A;
	if (Lhs.AuthorityId.B != Rhs.AuthorityId.B) return Lhs.AuthorityId.B < Rhs.AuthorityId.B;
	if (Lhs.AuthorityId.C != Rhs.AuthorityId.C) return Lhs.AuthorityId.C < Rhs.AuthorityId.C;
	if (Lhs.AuthorityId.D != Rhs.AuthorityId.D) return Lhs.AuthorityId.D < Rhs.AuthorityId.D;
	if (Lhs.Generation != Rhs.Generation) return Lhs.Generation < Rhs.Generation;
	return Lhs.LocalId < Rhs.LocalId;
}
