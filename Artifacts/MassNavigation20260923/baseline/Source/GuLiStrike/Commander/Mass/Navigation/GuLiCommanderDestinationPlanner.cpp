// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/Navigation/GuLiCommanderDestinationPlanner.h"

#include <limits>

namespace GuLiCommanderDestinationPlanner
{
	static_assert(MembersPerBlock == static_cast<int32>(GULI_CONTROL_COHORT_TARGET_SIZE));

	namespace Private
	{
		// Facing inputs are normalized unit vectors, so a one-square-centimetre
		// threshold would discard every valid facing and incorrectly fall back to +X.
		constexpr double MinimumDirectionLengthSquared = 1.0e-6;
		constexpr double SqrtThree = 1.7320508075688772935274463415059;

		struct FNormalizedRequest
		{
			TArray<int32> SortedCohortIndices;
			int32 TotalMemberCount = 0;
			FVector SelectionCentroid = FVector::ZeroVector;
			FVector Forward = FVector::ForwardVector;
			FVector Right = FVector::RightVector;
		};

		int64 ComputeHexNormSquared(const int32 AxialQ, const int32 AxialR)
		{
			const int64 Q = static_cast<int64>(AxialQ);
			const int64 R = static_cast<int64>(AxialR);
			return Q * Q + Q * R + R * R;
		}

		struct FFixedBlockSlot
		{
			int32 SlotIndex = INDEX_NONE;
			FVector LocalOffset = FVector::ZeroVector;
		};

		struct FOppositeSlotPair
		{
			int32 FirstSlotIndex = INDEX_NONE;
			int32 SecondSlotIndex = INDEX_NONE;
			double DistanceSquared = 0.0;
		};

		FVector LocalToWorld(
			const FVector& LocalOffset,
			const FVector& Origin,
			const FVector& Forward,
			const FVector& Right)
		{
			return Origin + Forward * LocalOffset.X + Right * LocalOffset.Y;
		}

		void MakeCenteredGridOffsets(
			const int32 Count,
			const int32 MaximumColumns,
			const float PitchCentimeters,
			TArray<FVector>& OutOffsets)
		{
			OutOffsets.Reset();
			if (Count <= 0 || MaximumColumns <= 0 || !FMath::IsFinite(PitchCentimeters)
				|| PitchCentimeters <= 0.0f)
			{
				return;
			}

			const int32 ColumnCount = FMath::Clamp(
				FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count))),
				1,
				MaximumColumns);
			const int32 RowCount = FMath::DivideAndRoundUp(Count, ColumnCount);
			OutOffsets.Reserve(Count);

			int32 Remaining = Count;
			for (int32 RowIndex = 0; RowIndex < RowCount; ++RowIndex)
			{
				const int32 MembersInRow = FMath::Min(ColumnCount, Remaining);
				const float ForwardOffset =
					(static_cast<float>(RowIndex) - static_cast<float>(RowCount - 1) * 0.5f)
					* PitchCentimeters;
				for (int32 ColumnIndex = 0; ColumnIndex < MembersInRow; ++ColumnIndex)
				{
					const float RightOffset =
						(static_cast<float>(ColumnIndex)
							- static_cast<float>(MembersInRow - 1) * 0.5f)
						* PitchCentimeters;
					OutOffsets.Add(FVector(ForwardOffset, RightOffset, 0.0f));
				}
				Remaining -= MembersInRow;
			}

			FVector OffsetCentroid = FVector::ZeroVector;
			for (const FVector& Offset : OutOffsets)
			{
				OffsetCentroid += Offset;
			}
			OffsetCentroid /= static_cast<double>(OutOffsets.Num());
			OffsetCentroid.Z = 0.0f;
			for (FVector& Offset : OutOffsets)
			{
				Offset -= OffsetCentroid;
			}
		}

		void MakeFixedBlockMemberSlots(
			const int32 MemberCount,
			const float SpacingCentimeters,
			TArray<FFixedBlockSlot>& OutSlots)
		{
			OutSlots.Reset();
			if (MemberCount <= 0 || MemberCount > MembersPerBlock
				|| !FMath::IsFinite(SpacingCentimeters) || SpacingCentimeters <= 0.0f)
			{
				return;
			}

			TArray<FFixedBlockSlot> FullBlock;
			FullBlock.Reserve(MembersPerBlock);
			for (int32 RowIndex = 0; RowIndex < ColumnsPerBlock; ++RowIndex)
			{
				for (int32 ColumnIndex = 0; ColumnIndex < ColumnsPerBlock; ++ColumnIndex)
				{
					FFixedBlockSlot& Slot = FullBlock.AddDefaulted_GetRef();
					Slot.SlotIndex = RowIndex * ColumnsPerBlock + ColumnIndex;
					Slot.LocalOffset = FVector(
						static_cast<float>(RowIndex - ColumnsPerBlock / 2) * SpacingCentimeters,
						static_cast<float>(ColumnIndex - ColumnsPerBlock / 2) * SpacingCentimeters,
						0.0f);
				}
			}

			const int32 CenterSlotIndex = MembersPerBlock / 2;
			if ((MemberCount & 1) != 0)
			{
				OutSlots.Add(FullBlock[CenterSlotIndex]);
			}

			TArray<FOppositeSlotPair> Pairs;
			Pairs.Reserve(MembersPerBlock / 2);
			for (int32 FirstSlotIndex = 0; FirstSlotIndex < CenterSlotIndex; ++FirstSlotIndex)
			{
				FOppositeSlotPair& Pair = Pairs.AddDefaulted_GetRef();
				Pair.FirstSlotIndex = FirstSlotIndex;
				Pair.SecondSlotIndex = MembersPerBlock - 1 - FirstSlotIndex;
				Pair.DistanceSquared = FullBlock[FirstSlotIndex].LocalOffset.SizeSquared2D();
			}
			Pairs.Sort([](const FOppositeSlotPair& Lhs, const FOppositeSlotPair& Rhs)
			{
				if (!FMath::IsNearlyEqual(Lhs.DistanceSquared, Rhs.DistanceSquared))
				{
					return Lhs.DistanceSquared < Rhs.DistanceSquared;
				}
				return Lhs.FirstSlotIndex < Rhs.FirstSlotIndex;
			});

			const int32 PairCount = MemberCount / 2;
			for (int32 PairIndex = 0; PairIndex < PairCount; ++PairIndex)
			{
				OutSlots.Add(FullBlock[Pairs[PairIndex].FirstSlotIndex]);
				OutSlots.Add(FullBlock[Pairs[PairIndex].SecondSlotIndex]);
			}
			OutSlots.Sort([](const FFixedBlockSlot& Lhs, const FFixedBlockSlot& Rhs)
			{
				return Lhs.SlotIndex < Rhs.SlotIndex;
			});
		}

		/**
		 * Deterministic rectangular Hungarian assignment. Every row is assigned
		 * to one unique column, so RowLocations.Num() must be <= ColumnLocations.Num().
		 */
		bool AssignMinimumDistanceRowsToColumns(
			const TConstArrayView<FVector> RowLocations,
			const TConstArrayView<FVector> ColumnLocations,
			TArray<int32>& OutColumnByRow)
		{
			OutColumnByRow.Reset();
			const int32 RowCount = RowLocations.Num();
			const int32 ColumnCount = ColumnLocations.Num();
			if (RowCount <= 0 || ColumnCount < RowCount)
			{
				return false;
			}

			TArray<double> RowPotential;
			TArray<double> ColumnPotential;
			TArray<int32> RowByColumn;
			TArray<int32> PreviousColumn;
			RowPotential.Init(0.0, RowCount + 1);
			ColumnPotential.Init(0.0, ColumnCount + 1);
			RowByColumn.Init(0, ColumnCount + 1);
			PreviousColumn.Init(0, ColumnCount + 1);

			for (int32 Row = 1; Row <= RowCount; ++Row)
			{
				RowByColumn[0] = Row;
				int32 CurrentColumn = 0;
				TArray<double> MinimumReducedCost;
				TArray<uint8> UsedColumns;
				MinimumReducedCost.Init(
					std::numeric_limits<double>::infinity(), ColumnCount + 1);
				UsedColumns.Init(0u, ColumnCount + 1);

				do
				{
					UsedColumns[CurrentColumn] = 1u;
					const int32 CurrentRow = RowByColumn[CurrentColumn];
					double Delta = std::numeric_limits<double>::infinity();
					int32 NextColumn = 0;
					for (int32 Column = 1; Column <= ColumnCount; ++Column)
					{
						if (UsedColumns[Column] != 0u)
						{
							continue;
						}

						const double Cost = FVector::DistSquared2D(
							RowLocations[CurrentRow - 1],
							ColumnLocations[Column - 1]);
						const double ReducedCost = Cost
							- RowPotential[CurrentRow]
							- ColumnPotential[Column];
						if (ReducedCost < MinimumReducedCost[Column])
						{
							MinimumReducedCost[Column] = ReducedCost;
							PreviousColumn[Column] = CurrentColumn;
						}
						if (MinimumReducedCost[Column] < Delta)
						{
							Delta = MinimumReducedCost[Column];
							NextColumn = Column;
						}
					}

					if (!FMath::IsFinite(Delta) || NextColumn == 0)
					{
						return false;
					}
					for (int32 Column = 0; Column <= ColumnCount; ++Column)
					{
						if (UsedColumns[Column] != 0u)
						{
							RowPotential[RowByColumn[Column]] += Delta;
							ColumnPotential[Column] -= Delta;
						}
						else
						{
							MinimumReducedCost[Column] -= Delta;
						}
					}
					CurrentColumn = NextColumn;
				}
				while (RowByColumn[CurrentColumn] != 0);

				do
				{
					const int32 PriorColumn = PreviousColumn[CurrentColumn];
					RowByColumn[CurrentColumn] = RowByColumn[PriorColumn];
					CurrentColumn = PriorColumn;
				}
				while (CurrentColumn != 0);
			}

			OutColumnByRow.Init(INDEX_NONE, RowCount);
			for (int32 Column = 1; Column <= ColumnCount; ++Column)
			{
				if (RowByColumn[Column] == 0)
				{
					continue;
				}
				const int32 Row = RowByColumn[Column] - 1;
				if (!OutColumnByRow.IsValidIndex(Row))
				{
					OutColumnByRow.Reset();
					return false;
				}
				OutColumnByRow[Row] = Column - 1;
			}
			for (const int32 Column : OutColumnByRow)
			{
				if (Column == INDEX_NONE)
				{
					OutColumnByRow.Reset();
					return false;
				}
			}
			return true;
		}

		/** Deterministic O(N^3) Hungarian assignment for equal-sized arrays. */
		bool AssignMinimumDistanceSlots(
			const TConstArrayView<FVector> AgentLocations,
			const TConstArrayView<FVector> SlotLocations,
			TArray<int32>& OutSlotByAgent)
		{
			if (AgentLocations.Num() != SlotLocations.Num())
			{
				OutSlotByAgent.Reset();
				return false;
			}
			return AssignMinimumDistanceRowsToColumns(
				AgentLocations, SlotLocations, OutSlotByAgent);
		}

		FVector ComputeCentroid(const TConstArrayView<FMemberInput> Members)
		{
			FVector Centroid = FVector::ZeroVector;
			for (const FMemberInput& Member : Members)
			{
				Centroid += Member.Location;
			}
			return Members.IsEmpty()
				? FVector::ZeroVector
				: Centroid / static_cast<double>(Members.Num());
		}

		bool NormalizeRequest(const FRequest& Request, FNormalizedRequest& OutRequest)
		{
			OutRequest = FNormalizedRequest();
			if (Request.Cohorts.IsEmpty() || Request.TargetAnchor.ContainsNaN()
				|| !FMath::IsFinite(Request.MemberSpacingCentimeters)
				|| Request.MemberSpacingCentimeters <= 0.0f)
			{
				return false;
			}

			TSet<FGuLiControlCohortId> SeenCohortIds;
			TSet<FGuLiSoldierId> SeenSoldierIds;
			FVector MeanFacing = FVector::ZeroVector;
			OutRequest.SortedCohortIndices.Reserve(Request.Cohorts.Num());
			for (int32 CohortIndex = 0; CohortIndex < Request.Cohorts.Num(); ++CohortIndex)
			{
				const FCohortInput& Cohort = Request.Cohorts[CohortIndex];
				if (!Cohort.CohortId.IsValid() || SeenCohortIds.Contains(Cohort.CohortId)
					|| Cohort.Members.IsEmpty() || Cohort.Members.Num() > MembersPerBlock)
				{
					return false;
				}
				SeenCohortIds.Add(Cohort.CohortId);
				OutRequest.SortedCohortIndices.Add(CohortIndex);

				for (const FMemberInput& Member : Cohort.Members)
				{
					if (!Member.SoldierId.IsValid() || SeenSoldierIds.Contains(Member.SoldierId)
						|| Member.Location.ContainsNaN())
					{
						return false;
					}
					SeenSoldierIds.Add(Member.SoldierId);
					++OutRequest.TotalMemberCount;
					OutRequest.SelectionCentroid += Member.Location;

					FVector Facing = Member.Facing;
					Facing.Z = 0.0f;
					if (!Facing.ContainsNaN()
						&& Facing.SizeSquared2D() > MinimumDirectionLengthSquared)
					{
						MeanFacing += Facing.GetSafeNormal2D();
					}
				}
			}

			OutRequest.SortedCohortIndices.Sort([&Request](const int32 Lhs, const int32 Rhs)
			{
				return Request.Cohorts[Lhs].CohortId < Request.Cohorts[Rhs].CohortId;
			});
			OutRequest.SelectionCentroid /= static_cast<double>(OutRequest.TotalMemberCount);

			OutRequest.Forward = Request.TargetAnchor - OutRequest.SelectionCentroid;
			OutRequest.Forward.Z = 0.0f;
			if (OutRequest.Forward.ContainsNaN()
				|| OutRequest.Forward.SizeSquared2D() <= MinimumDirectionLengthSquared)
			{
				OutRequest.Forward = MeanFacing;
				OutRequest.Forward.Z = 0.0f;
			}
			if (OutRequest.Forward.ContainsNaN()
				|| OutRequest.Forward.SizeSquared2D() <= MinimumDirectionLengthSquared)
			{
				OutRequest.Forward = FVector::ForwardVector;
			}
			else
			{
				OutRequest.Forward.Normalize();
			}
			OutRequest.Right = FVector(
				-OutRequest.Forward.Y, OutRequest.Forward.X, 0.0f);
			return true;
		}
	}

	void FPlan::Reset()
	{
		*this = FPlan();
	}

	void FSoftAnchorPlan::Reset()
	{
		*this = FSoftAnchorPlan();
	}

	void FFreeAssignmentPlan::Reset()
	{
		*this = FFreeAssignmentPlan();
	}

	bool BuildHexCandidates(
		const FHexCandidateRequest& Request,
		TArray<FFreeDestinationCandidate>& OutCandidates)
	{
		OutCandidates.Reset();
		if (Request.TargetAnchor.ContainsNaN()
			|| !FMath::IsFinite(Request.CandidatePitchCentimeters)
			|| Request.CandidatePitchCentimeters <= 0.0f
			|| !FMath::IsFinite(Request.MaximumRadiusCentimeters)
			|| Request.MaximumRadiusCentimeters < 0.0f)
		{
			return false;
		}

		const double Pitch = static_cast<double>(Request.CandidatePitchCentimeters);
		const double MaximumRadius = static_cast<double>(Request.MaximumRadiusCentimeters);
		const double MaximumRadiusSquared = MaximumRadius * MaximumRadius;
		const double BoundaryTolerance = FMath::Max(1.0e-6, MaximumRadiusSquared * 1.0e-12);
		const int32 CoordinateExtent = FMath::CeilToInt(
			(2.0 * MaximumRadius) / (Private::SqrtThree * Pitch)) + 1;

		for (int32 AxialQ = -CoordinateExtent; AxialQ <= CoordinateExtent; ++AxialQ)
		{
			for (int32 AxialR = -CoordinateExtent; AxialR <= CoordinateExtent; ++AxialR)
			{
				const int64 HexNormSquared =
					Private::ComputeHexNormSquared(AxialQ, AxialR);
				const double DistanceSquared =
					static_cast<double>(HexNormSquared) * Pitch * Pitch;
				if (DistanceSquared > MaximumRadiusSquared + BoundaryTolerance)
				{
					continue;
				}

				FFreeDestinationCandidate& Candidate = OutCandidates.AddDefaulted_GetRef();
				Candidate.AxialQ = AxialQ;
				Candidate.AxialR = AxialR;
				Candidate.AnchorLocalOffset = FVector(
					Pitch * (static_cast<double>(AxialQ) + static_cast<double>(AxialR) * 0.5),
					Pitch * (Private::SqrtThree * 0.5 * static_cast<double>(AxialR)),
					0.0);
				Candidate.WorldCandidate = Request.TargetAnchor + Candidate.AnchorLocalOffset;
				Candidate.DistanceSquaredFromAnchor = DistanceSquared;
			}
		}

		OutCandidates.Sort([](
			const FFreeDestinationCandidate& Lhs,
			const FFreeDestinationCandidate& Rhs)
		{
			if (Lhs.DistanceSquaredFromAnchor != Rhs.DistanceSquaredFromAnchor)
			{
				return Lhs.DistanceSquaredFromAnchor < Rhs.DistanceSquaredFromAnchor;
			}
			if (Lhs.AxialQ != Rhs.AxialQ)
			{
				return Lhs.AxialQ < Rhs.AxialQ;
			}
			return Lhs.AxialR < Rhs.AxialR;
		});
		for (int32 CandidateIndex = 0; CandidateIndex < OutCandidates.Num(); ++CandidateIndex)
		{
			OutCandidates[CandidateIndex].CandidateIndex = CandidateIndex;
		}
		return true;
	}

	int32 FindProjectionPrefixEnd(
		const TConstArrayView<FFreeDestinationCandidate> Candidates,
		const int32 MinimumCandidateCount)
	{
		if (Candidates.IsEmpty() || MinimumCandidateCount <= 0)
		{
			return 0;
		}

		int32 PrefixEnd = FMath::Min(MinimumCandidateCount, Candidates.Num());
		if (PrefixEnd >= Candidates.Num())
		{
			return Candidates.Num();
		}

		const double BoundaryDistanceSquared =
			Candidates[PrefixEnd - 1].DistanceSquaredFromAnchor;
		while (PrefixEnd < Candidates.Num()
			&& Candidates[PrefixEnd].DistanceSquaredFromAnchor == BoundaryDistanceSquared)
		{
			++PrefixEnd;
		}
		return PrefixEnd;
	}

	bool BuildSoftCohortAnchors(
		const FRequest& Request,
		const float AnchorPitchCentimeters,
		FSoftAnchorPlan& OutPlan)
	{
		OutPlan.Reset();
		if (!FMath::IsFinite(AnchorPitchCentimeters) || AnchorPitchCentimeters <= 0.0f)
		{
			return false;
		}

		Private::FNormalizedRequest Normalized;
		if (!Private::NormalizeRequest(Request, Normalized))
		{
			return false;
		}

		TArray<FVector> AnchorLocalOffsets;
		Private::MakeCenteredGridOffsets(
			Request.Cohorts.Num(),
			Request.Cohorts.Num(),
			AnchorPitchCentimeters,
			AnchorLocalOffsets);
		TArray<FVector> AnchorWorldLocations;
		AnchorWorldLocations.Reserve(AnchorLocalOffsets.Num());
		for (const FVector& LocalOffset : AnchorLocalOffsets)
		{
			AnchorWorldLocations.Add(Private::LocalToWorld(
				LocalOffset,
				Request.TargetAnchor,
				Normalized.Forward,
				Normalized.Right));
		}

		TArray<FVector> CohortCentroids;
		CohortCentroids.Reserve(Request.Cohorts.Num());
		for (const int32 CohortIndex : Normalized.SortedCohortIndices)
		{
			CohortCentroids.Add(Private::ComputeCentroid(Request.Cohorts[CohortIndex].Members));
		}
		TArray<int32> AnchorByCohort;
		if (!Private::AssignMinimumDistanceSlots(
			CohortCentroids, AnchorWorldLocations, AnchorByCohort))
		{
			return false;
		}

		FSoftAnchorPlan CandidatePlan;
		CandidatePlan.TargetAnchor = Request.TargetAnchor;
		CandidatePlan.SelectionCentroid = Normalized.SelectionCentroid;
		CandidatePlan.Forward = Normalized.Forward;
		CandidatePlan.Right = Normalized.Right;
		CandidatePlan.YawDegrees = FRotator::NormalizeAxis(Normalized.Forward.Rotation().Yaw);
		CandidatePlan.AnchorPitchCentimeters = AnchorPitchCentimeters;
		CandidatePlan.Cohorts.Reserve(Request.Cohorts.Num());
		for (int32 SortedIndex = 0; SortedIndex < Normalized.SortedCohortIndices.Num(); ++SortedIndex)
		{
			const FCohortInput& SourceCohort =
				Request.Cohorts[Normalized.SortedCohortIndices[SortedIndex]];
			const int32 AnchorIndex = AnchorByCohort[SortedIndex];
			if (!AnchorLocalOffsets.IsValidIndex(AnchorIndex))
			{
				return false;
			}

			FSoftCohortAnchor& Anchor = CandidatePlan.Cohorts.AddDefaulted_GetRef();
			Anchor.CohortId = SourceCohort.CohortId;
			Anchor.AnchorIndex = AnchorIndex;
			Anchor.SourceCentroid = CohortCentroids[SortedIndex];
			Anchor.BatchLocalOffset = AnchorLocalOffsets[AnchorIndex];
			Anchor.WorldAnchor = AnchorWorldLocations[AnchorIndex];
		}

		OutPlan = MoveTemp(CandidatePlan);
		return true;
	}

	bool AssignFreeDestinations(
		const FRequest& Request,
		const TConstArrayView<FFreeDestinationSlot> AvailableSlots,
		const float SoftAnchorPitchCentimeters,
		FFreeAssignmentPlan& OutPlan)
	{
		OutPlan.Reset();
		FSoftAnchorPlan SoftAnchorPlan;
		if (!BuildSoftCohortAnchors(Request, SoftAnchorPitchCentimeters, SoftAnchorPlan))
		{
			return false;
		}

		TSet<int32> SeenCandidateIndices;
		TSet<FIntPoint> SeenAxialCoordinates;
		TArray<int32> SortedSlotIndices;
		SortedSlotIndices.Reserve(AvailableSlots.Num());
		for (int32 SlotIndex = 0; SlotIndex < AvailableSlots.Num(); ++SlotIndex)
		{
			const FFreeDestinationSlot& Slot = AvailableSlots[SlotIndex];
			const FIntPoint AxialCoordinate(Slot.AxialQ, Slot.AxialR);
			if (Slot.CandidateIndex < 0 || SeenCandidateIndices.Contains(Slot.CandidateIndex)
				|| SeenAxialCoordinates.Contains(AxialCoordinate)
				|| Slot.OriginalWorldCandidate.ContainsNaN()
				|| Slot.WorldDestination.ContainsNaN())
			{
				return false;
			}
			SeenCandidateIndices.Add(Slot.CandidateIndex);
			SeenAxialCoordinates.Add(AxialCoordinate);
			SortedSlotIndices.Add(SlotIndex);
		}
		SortedSlotIndices.Sort([&AvailableSlots](const int32 Lhs, const int32 Rhs)
		{
			const FFreeDestinationSlot& LhsSlot = AvailableSlots[Lhs];
			const FFreeDestinationSlot& RhsSlot = AvailableSlots[Rhs];
			const int64 LhsHexNormSquared =
				Private::ComputeHexNormSquared(LhsSlot.AxialQ, LhsSlot.AxialR);
			const int64 RhsHexNormSquared =
				Private::ComputeHexNormSquared(RhsSlot.AxialQ, RhsSlot.AxialR);
			if (LhsHexNormSquared != RhsHexNormSquared)
			{
				return LhsHexNormSquared < RhsHexNormSquared;
			}
			if (LhsSlot.AxialQ != RhsSlot.AxialQ)
			{
				return LhsSlot.AxialQ < RhsSlot.AxialQ;
			}
			if (LhsSlot.AxialR != RhsSlot.AxialR)
			{
				return LhsSlot.AxialR < RhsSlot.AxialR;
			}
			return LhsSlot.CandidateIndex < RhsSlot.CandidateIndex;
		});

		FFreeAssignmentPlan CandidatePlan;
		CandidatePlan.TargetAnchor = Request.TargetAnchor;
		CandidatePlan.Cohorts.Reserve(SoftAnchorPlan.Cohorts.Num());
		TArray<TArray<int32>> SlotIndicesByCohort;
		SlotIndicesByCohort.SetNum(SoftAnchorPlan.Cohorts.Num());
		for (const FSoftCohortAnchor& SoftAnchor : SoftAnchorPlan.Cohorts)
		{
			const FCohortInput* SourceCohort = Request.Cohorts.FindByPredicate(
				[&SoftAnchor](const FCohortInput& Cohort)
				{
					return Cohort.CohortId == SoftAnchor.CohortId;
				});
			if (SourceCohort == nullptr)
			{
				return false;
			}

			FFreeCohortAssignment& Cohort = CandidatePlan.Cohorts.AddDefaulted_GetRef();
			Cohort.CohortId = SoftAnchor.CohortId;
			Cohort.RequestedMemberCount = SourceCohort->Members.Num();
			Cohort.SoftAnchorIndex = SoftAnchor.AnchorIndex;
			Cohort.SourceCentroid = SoftAnchor.SourceCentroid;
			Cohort.WorldSoftAnchor = SoftAnchor.WorldAnchor;
			CandidatePlan.RequestedMemberCount += Cohort.RequestedMemberCount;
		}

		for (const int32 SlotIndex : SortedSlotIndices)
		{
			int32 BestCohortIndex = INDEX_NONE;
			double BestDistanceSquared = std::numeric_limits<double>::infinity();
			for (int32 CohortIndex = 0; CohortIndex < CandidatePlan.Cohorts.Num(); ++CohortIndex)
			{
				const FFreeCohortAssignment& Cohort = CandidatePlan.Cohorts[CohortIndex];
				if (SlotIndicesByCohort[CohortIndex].Num() >= Cohort.RequestedMemberCount)
				{
					continue;
				}
				const double DistanceSquared = FVector::DistSquared2D(
					AvailableSlots[SlotIndex].WorldDestination,
					Cohort.WorldSoftAnchor);
				if (DistanceSquared < BestDistanceSquared
					|| (DistanceSquared == BestDistanceSquared
						&& (BestCohortIndex == INDEX_NONE
							|| Cohort.CohortId < CandidatePlan.Cohorts[BestCohortIndex].CohortId)))
				{
					BestDistanceSquared = DistanceSquared;
					BestCohortIndex = CohortIndex;
				}
			}
			if (BestCohortIndex == INDEX_NONE)
			{
				break;
			}
			SlotIndicesByCohort[BestCohortIndex].Add(SlotIndex);
		}

		for (int32 CohortIndex = 0; CohortIndex < CandidatePlan.Cohorts.Num(); ++CohortIndex)
		{
			FFreeCohortAssignment& CohortAssignment = CandidatePlan.Cohorts[CohortIndex];
			const FCohortInput* SourceCohort = Request.Cohorts.FindByPredicate(
				[&CohortAssignment](const FCohortInput& Cohort)
				{
					return Cohort.CohortId == CohortAssignment.CohortId;
				});
			if (SourceCohort == nullptr)
			{
				return false;
			}

			TArray<int32> SortedMemberIndices;
			SortedMemberIndices.Reserve(SourceCohort->Members.Num());
			for (int32 MemberIndex = 0; MemberIndex < SourceCohort->Members.Num(); ++MemberIndex)
			{
				SortedMemberIndices.Add(MemberIndex);
			}
			SortedMemberIndices.Sort([SourceCohort](const int32 Lhs, const int32 Rhs)
			{
				return SourceCohort->Members[Lhs].SoldierId
					< SourceCohort->Members[Rhs].SoldierId;
			});

			const TArray<int32>& AssignedSlotIndices = SlotIndicesByCohort[CohortIndex];
			if (AssignedSlotIndices.IsEmpty())
			{
				continue;
			}
			TArray<FVector> DestinationLocations;
			TArray<FVector> MemberLocations;
			DestinationLocations.Reserve(AssignedSlotIndices.Num());
			MemberLocations.Reserve(SortedMemberIndices.Num());
			for (const int32 SlotIndex : AssignedSlotIndices)
			{
				DestinationLocations.Add(AvailableSlots[SlotIndex].WorldDestination);
			}
			for (const int32 MemberIndex : SortedMemberIndices)
			{
				MemberLocations.Add(SourceCohort->Members[MemberIndex].Location);
			}

			TArray<int32> MemberByDestination;
			if (!Private::AssignMinimumDistanceRowsToColumns(
				DestinationLocations, MemberLocations, MemberByDestination))
			{
				return false;
			}
			CohortAssignment.Members.Reserve(AssignedSlotIndices.Num());
			for (int32 DestinationIndex = 0;
				DestinationIndex < AssignedSlotIndices.Num();
				++DestinationIndex)
			{
				const int32 SortedMemberIndex = MemberByDestination[DestinationIndex];
				if (!SortedMemberIndices.IsValidIndex(SortedMemberIndex))
				{
					return false;
				}
				const FFreeDestinationSlot& Slot =
					AvailableSlots[AssignedSlotIndices[DestinationIndex]];
				FFreeMemberAssignment& Assignment =
					CohortAssignment.Members.AddDefaulted_GetRef();
				Assignment.SoldierId =
					SourceCohort->Members[SortedMemberIndices[SortedMemberIndex]].SoldierId;
				Assignment.CandidateIndex = Slot.CandidateIndex;
				Assignment.WorldDestination = Slot.WorldDestination;
			}
			CohortAssignment.Members.Sort([](
				const FFreeMemberAssignment& Lhs,
				const FFreeMemberAssignment& Rhs)
			{
				return Lhs.SoldierId < Rhs.SoldierId;
			});
			CandidatePlan.AssignedMemberCount += CohortAssignment.Members.Num();
		}

		OutPlan = MoveTemp(CandidatePlan);
		return true;
	}

	bool Build(const FRequest& Request, FPlan& OutPlan)
	{
		OutPlan.Reset();
		if (Request.Cohorts.IsEmpty() || Request.TargetAnchor.ContainsNaN()
			|| !FMath::IsFinite(Request.MemberSpacingCentimeters)
			|| Request.MemberSpacingCentimeters <= 0.0f)
		{
			return false;
		}

		TSet<FGuLiControlCohortId> SeenCohortIds;
		TSet<FGuLiSoldierId> SeenSoldierIds;
		TArray<int32> SortedCohortIndices;
		SortedCohortIndices.Reserve(Request.Cohorts.Num());
		int32 TotalMemberCount = 0;
		for (int32 CohortIndex = 0; CohortIndex < Request.Cohorts.Num(); ++CohortIndex)
		{
			const FCohortInput& Cohort = Request.Cohorts[CohortIndex];
			if (!Cohort.CohortId.IsValid() || SeenCohortIds.Contains(Cohort.CohortId)
				|| Cohort.Members.IsEmpty() || Cohort.Members.Num() > MembersPerBlock)
			{
				return false;
			}
			SeenCohortIds.Add(Cohort.CohortId);
			SortedCohortIndices.Add(CohortIndex);
			TotalMemberCount += Cohort.Members.Num();

			for (const FMemberInput& Member : Cohort.Members)
			{
				if (!Member.SoldierId.IsValid() || SeenSoldierIds.Contains(Member.SoldierId)
					|| Member.Location.ContainsNaN())
				{
					return false;
				}
				SeenSoldierIds.Add(Member.SoldierId);
			}
		}

		SortedCohortIndices.Sort([&Request](const int32 Lhs, const int32 Rhs)
		{
			return Request.Cohorts[Lhs].CohortId < Request.Cohorts[Rhs].CohortId;
		});

		FVector SelectionCentroid = FVector::ZeroVector;
		FVector MeanFacing = FVector::ZeroVector;
		for (const FCohortInput& Cohort : Request.Cohorts)
		{
			for (const FMemberInput& Member : Cohort.Members)
			{
				SelectionCentroid += Member.Location;
				FVector Facing = Member.Facing;
				Facing.Z = 0.0f;
				if (!Facing.ContainsNaN()
					&& Facing.SizeSquared2D() > Private::MinimumDirectionLengthSquared)
				{
					MeanFacing += Facing.GetSafeNormal2D();
				}
			}
		}
		SelectionCentroid /= static_cast<double>(TotalMemberCount);

		FVector Forward = Request.TargetAnchor - SelectionCentroid;
		Forward.Z = 0.0f;
		if (Forward.ContainsNaN()
			|| Forward.SizeSquared2D() <= Private::MinimumDirectionLengthSquared)
		{
			Forward = MeanFacing;
			Forward.Z = 0.0f;
		}
		if (Forward.ContainsNaN()
			|| Forward.SizeSquared2D() <= Private::MinimumDirectionLengthSquared)
		{
			Forward = FVector::ForwardVector;
		}
		else
		{
			Forward.Normalize();
		}
		const FVector Right(-Forward.Y, Forward.X, 0.0f);
		const float BlockPitch = Request.MemberSpacingCentimeters
			* static_cast<float>(ColumnsPerBlock);

		TArray<FVector> BlockLocalOffsets;
		Private::MakeCenteredGridOffsets(
			Request.Cohorts.Num(),
			Request.Cohorts.Num(),
			BlockPitch,
			BlockLocalOffsets);
		TArray<FVector> BlockWorldLocations;
		BlockWorldLocations.Reserve(BlockLocalOffsets.Num());
		for (const FVector& LocalOffset : BlockLocalOffsets)
		{
			BlockWorldLocations.Add(Private::LocalToWorld(
				LocalOffset, Request.TargetAnchor, Forward, Right));
		}

		TArray<FVector> CohortCentroids;
		CohortCentroids.Reserve(Request.Cohorts.Num());
		for (const int32 CohortIndex : SortedCohortIndices)
		{
			CohortCentroids.Add(Private::ComputeCentroid(Request.Cohorts[CohortIndex].Members));
		}
		TArray<int32> BlockByCohort;
		if (!Private::AssignMinimumDistanceSlots(
			CohortCentroids, BlockWorldLocations, BlockByCohort))
		{
			return false;
		}

		FPlan CandidatePlan;
		CandidatePlan.TargetAnchor = Request.TargetAnchor;
		CandidatePlan.SelectionCentroid = SelectionCentroid;
		CandidatePlan.Forward = Forward;
		CandidatePlan.Right = Right;
		CandidatePlan.YawDegrees = FRotator::NormalizeAxis(Forward.Rotation().Yaw);
		CandidatePlan.MemberSpacingCentimeters = Request.MemberSpacingCentimeters;
		CandidatePlan.BlockPitchCentimeters = BlockPitch;
		CandidatePlan.Cohorts.Reserve(Request.Cohorts.Num());

		for (int32 SortedCohortIndex = 0;
			SortedCohortIndex < SortedCohortIndices.Num();
			++SortedCohortIndex)
		{
			const FCohortInput& SourceCohort =
				Request.Cohorts[SortedCohortIndices[SortedCohortIndex]];
			const int32 BlockIndex = BlockByCohort[SortedCohortIndex];
			if (!BlockLocalOffsets.IsValidIndex(BlockIndex))
			{
				return false;
			}

			FCohortCandidate& CohortCandidate = CandidatePlan.Cohorts.AddDefaulted_GetRef();
			CohortCandidate.CohortId = SourceCohort.CohortId;
			CohortCandidate.BlockIndex = BlockIndex;
			CohortCandidate.SourceCentroid = CohortCentroids[SortedCohortIndex];
			CohortCandidate.BatchLocalOffset = BlockLocalOffsets[BlockIndex];
			CohortCandidate.WorldBlockCenter = BlockWorldLocations[BlockIndex];

			TArray<int32> SortedMemberIndices;
			SortedMemberIndices.Reserve(SourceCohort.Members.Num());
			for (int32 MemberIndex = 0; MemberIndex < SourceCohort.Members.Num(); ++MemberIndex)
			{
				SortedMemberIndices.Add(MemberIndex);
			}
			SortedMemberIndices.Sort([&SourceCohort](const int32 Lhs, const int32 Rhs)
			{
				return SourceCohort.Members[Lhs].SoldierId < SourceCohort.Members[Rhs].SoldierId;
			});

			TArray<Private::FFixedBlockSlot> MemberSlots;
			Private::MakeFixedBlockMemberSlots(
				SourceCohort.Members.Num(),
				Request.MemberSpacingCentimeters,
				MemberSlots);
			TArray<FVector> MemberWorldSlots;
			TArray<FVector> SortedMemberLocations;
			MemberWorldSlots.Reserve(MemberSlots.Num());
			SortedMemberLocations.Reserve(SourceCohort.Members.Num());
			for (const Private::FFixedBlockSlot& MemberSlot : MemberSlots)
			{
				MemberWorldSlots.Add(Private::LocalToWorld(
					CohortCandidate.BatchLocalOffset + MemberSlot.LocalOffset,
					Request.TargetAnchor,
					Forward,
					Right));
			}
			for (const int32 MemberIndex : SortedMemberIndices)
			{
				SortedMemberLocations.Add(SourceCohort.Members[MemberIndex].Location);
			}

			TArray<int32> SlotByMember;
			if (!Private::AssignMinimumDistanceSlots(
				SortedMemberLocations, MemberWorldSlots, SlotByMember))
			{
				return false;
			}

			CohortCandidate.Members.Reserve(SourceCohort.Members.Num());
			for (int32 SortedMemberIndex = 0;
				SortedMemberIndex < SortedMemberIndices.Num();
				++SortedMemberIndex)
			{
				const FMemberInput& SourceMember =
					SourceCohort.Members[SortedMemberIndices[SortedMemberIndex]];
				const int32 SlotIndex = SlotByMember[SortedMemberIndex];
				if (!MemberSlots.IsValidIndex(SlotIndex))
				{
					return false;
				}

				FMemberCandidate& MemberCandidate =
					CohortCandidate.Members.AddDefaulted_GetRef();
				MemberCandidate.SoldierId = SourceMember.SoldierId;
				MemberCandidate.SlotIndex = MemberSlots[SlotIndex].SlotIndex;
				MemberCandidate.BlockLocalOffset = MemberSlots[SlotIndex].LocalOffset;
				MemberCandidate.BatchLocalOffset =
					CohortCandidate.BatchLocalOffset + MemberCandidate.BlockLocalOffset;
				MemberCandidate.WorldCandidate = MemberWorldSlots[SlotIndex];
			}
		}

		OutPlan = MoveTemp(CandidatePlan);
		return true;
	}
}
