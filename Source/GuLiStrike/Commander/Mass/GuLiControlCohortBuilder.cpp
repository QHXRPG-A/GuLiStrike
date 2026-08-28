// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/GuLiControlCohortBuilder.h"

namespace GuLiControlCohortBuilder
{
	namespace Private
	{
		bool IsCandidateLess(
			const FCandidate& Lhs,
			const FCandidate& Rhs,
			const FVector& Center)
		{
			const double LhsDistance = FVector::DistSquared2D(Lhs.Location, Center);
			const double RhsDistance = FVector::DistSquared2D(Rhs.Location, Center);
			if (!FMath::IsNearlyEqual(LhsDistance, RhsDistance))
			{
				return LhsDistance < RhsDistance;
			}
			return Lhs.SoldierId.Value < Rhs.SoldierId.Value;
		}

		FVector ComputeCentroid(
			TConstArrayView<FGuLiSoldierId> Ids,
			const TMap<uint32, FVector>& Locations)
		{
			FVector Sum = FVector::ZeroVector;
			int32 Count = 0;
			for (const FGuLiSoldierId SoldierId : Ids)
			{
				if (const FVector* Location = Locations.Find(SoldierId.Value))
				{
					Sum += *Location;
					++Count;
				}
			}
			return Count > 0 ? Sum / static_cast<double>(Count) : FVector::ZeroVector;
		}
	}

	void Build(
		const TConstArrayView<FCandidate> InCircleSeeds,
		const TConstArrayView<FCandidate> FillCandidates,
		const float MaximumFillDistanceCentimeters,
		TArray<TArray<FGuLiSoldierId>>& OutCohorts)
	{
		OutCohorts.Reset();
		if (InCircleSeeds.IsEmpty())
		{
			return;
		}

		TMap<uint32, FVector> Locations;
		TArray<FCandidate> RemainingSeeds;
		TSet<uint32> UsedIds;
		RemainingSeeds.Reserve(InCircleSeeds.Num());
		for (const FCandidate& Candidate : InCircleSeeds)
		{
			if (!Candidate.SoldierId.IsValid() || Candidate.Location.ContainsNaN()
				|| Locations.Contains(Candidate.SoldierId.Value))
			{
				continue;
			}
			Locations.Add(Candidate.SoldierId.Value, Candidate.Location);
			RemainingSeeds.Add(Candidate);
		}

		TArray<FCandidate> ValidFillCandidates;
		ValidFillCandidates.Reserve(FillCandidates.Num());
		for (const FCandidate& Candidate : FillCandidates)
		{
			if (!Candidate.SoldierId.IsValid() || Candidate.Location.ContainsNaN()
				|| Locations.Contains(Candidate.SoldierId.Value))
			{
				continue;
			}
			Locations.Add(Candidate.SoldierId.Value, Candidate.Location);
			ValidFillCandidates.Add(Candidate);
		}

		while (!RemainingSeeds.IsEmpty())
		{
			TArray<FGuLiSoldierId>& Cohort = OutCohorts.AddDefaulted_GetRef();
			Cohort.Reserve(static_cast<int32>(GULI_CONTROL_COHORT_TARGET_SIZE));

			const FCandidate First = RemainingSeeds[0];
			RemainingSeeds.RemoveAt(0, 1, EAllowShrinking::No);
			Cohort.Add(First.SoldierId);
			UsedIds.Add(First.SoldierId.Value);

			while (Cohort.Num() < static_cast<int32>(GULI_CONTROL_COHORT_TARGET_SIZE)
				&& !RemainingSeeds.IsEmpty())
			{
				const FVector Centroid = Private::ComputeCentroid(Cohort, Locations);
				int32 BestIndex = 0;
				for (int32 Index = 1; Index < RemainingSeeds.Num(); ++Index)
				{
					if (Private::IsCandidateLess(RemainingSeeds[Index], RemainingSeeds[BestIndex], Centroid))
					{
						BestIndex = Index;
					}
				}

				const FCandidate Chosen = RemainingSeeds[BestIndex];
				RemainingSeeds.RemoveAt(BestIndex, 1, EAllowShrinking::No);
				Cohort.Add(Chosen.SoldierId);
				UsedIds.Add(Chosen.SoldierId.Value);
			}
		}

		TArray<FGuLiSoldierId>& FinalCohort = OutCohorts.Last();
		const float MaximumFillDistanceSquared = FMath::Square(
			FMath::Max(0.0f, MaximumFillDistanceCentimeters));
		while (FinalCohort.Num() < static_cast<int32>(GULI_CONTROL_COHORT_TARGET_SIZE))
		{
			const FVector Centroid = Private::ComputeCentroid(FinalCohort, Locations);
			int32 BestIndex = INDEX_NONE;
			for (int32 Index = 0; Index < ValidFillCandidates.Num(); ++Index)
			{
				const FCandidate& Candidate = ValidFillCandidates[Index];
				if (UsedIds.Contains(Candidate.SoldierId.Value)
					|| FVector::DistSquared2D(Candidate.Location, Centroid) > MaximumFillDistanceSquared)
				{
					continue;
				}
				if (BestIndex == INDEX_NONE
					|| Private::IsCandidateLess(Candidate, ValidFillCandidates[BestIndex], Centroid))
				{
					BestIndex = Index;
				}
			}

			if (BestIndex == INDEX_NONE)
			{
				break;
			}

			const FCandidate Chosen = ValidFillCandidates[BestIndex];
			FinalCohort.Add(Chosen.SoldierId);
			UsedIds.Add(Chosen.SoldierId.Value);
		}
	}
}
