// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Combat/GuLiWingmanTargetAssignment.h"

namespace
{
	constexpr int64 MaximumSafeAssignmentCost = TNumericLimits<int64>::Max() / 8;

	bool IsStableGuidLess(const FGuid& Lhs, const FGuid& Rhs)
	{
		if (Lhs.A != Rhs.A) return Lhs.A < Rhs.A;
		if (Lhs.B != Rhs.B) return Lhs.B < Rhs.B;
		if (Lhs.C != Rhs.C) return Lhs.C < Rhs.C;
		return Lhs.D < Rhs.D;
	}

	bool TryMultiplyAndIncrement(const int64 Lhs, const int64 Rhs, int64& OutValue)
	{
		OutValue = 0;
		if (Lhs < 0 || Rhs < 0
			|| (Lhs != 0 && Rhs > (MaximumSafeAssignmentCost - 1) / Lhs))
		{
			return false;
		}
		OutValue = Lhs * Rhs + 1;
		return true;
	}

	bool SolveRectangularHungarian(
		const int32 RowCount,
		const int32 ColumnCount,
		const TFunctionRef<int64(int32, int32)>& Cost,
		TArray<int32>& OutColumnByRow)
	{
		OutColumnByRow.Reset();
		if (RowCount <= 0 || ColumnCount < RowCount)
		{
			return false;
		}

		TArray<int64> RowPotential;
		TArray<int64> ColumnPotential;
		TArray<int32> MatchedRowByColumn;
		TArray<int32> PreviousColumn;
		RowPotential.Init(0, RowCount + 1);
		ColumnPotential.Init(0, ColumnCount + 1);
		MatchedRowByColumn.Init(0, ColumnCount + 1);
		PreviousColumn.Init(0, ColumnCount + 1);

		for (int32 Row = 1; Row <= RowCount; ++Row)
		{
			MatchedRowByColumn[0] = Row;
			int32 CurrentColumn = 0;
			TArray<int64> MinimumReducedCost;
			TArray<uint8> UsedColumns;
			MinimumReducedCost.Init(MaximumSafeAssignmentCost, ColumnCount + 1);
			UsedColumns.Init(0u, ColumnCount + 1);

			do
			{
				UsedColumns[CurrentColumn] = 1u;
				const int32 CurrentRow = MatchedRowByColumn[CurrentColumn];
				int64 Delta = MaximumSafeAssignmentCost;
				int32 NextColumn = 0;
				for (int32 Column = 1; Column <= ColumnCount; ++Column)
				{
					if (UsedColumns[Column] != 0u)
					{
						continue;
					}
					const int64 ReducedCost = Cost(CurrentRow - 1, Column - 1)
						- RowPotential[CurrentRow] - ColumnPotential[Column];
					if (ReducedCost < MinimumReducedCost[Column])
					{
						MinimumReducedCost[Column] = ReducedCost;
						PreviousColumn[Column] = CurrentColumn;
					}
					if (MinimumReducedCost[Column] < Delta
						|| (MinimumReducedCost[Column] == Delta
							&& (NextColumn == 0 || Column < NextColumn)))
					{
						Delta = MinimumReducedCost[Column];
						NextColumn = Column;
					}
				}
				if (NextColumn == 0 || Delta == MaximumSafeAssignmentCost)
				{
					return false;
				}
				for (int32 Column = 0; Column <= ColumnCount; ++Column)
				{
					if (UsedColumns[Column] != 0u)
					{
						RowPotential[MatchedRowByColumn[Column]] += Delta;
						ColumnPotential[Column] -= Delta;
					}
					else
					{
						MinimumReducedCost[Column] -= Delta;
					}
				}
				CurrentColumn = NextColumn;
			}
			while (MatchedRowByColumn[CurrentColumn] != 0);

			do
			{
				const int32 PriorColumn = PreviousColumn[CurrentColumn];
				MatchedRowByColumn[CurrentColumn] = MatchedRowByColumn[PriorColumn];
				CurrentColumn = PriorColumn;
			}
			while (CurrentColumn != 0);
		}

		OutColumnByRow.Init(INDEX_NONE, RowCount);
		for (int32 Column = 1; Column <= ColumnCount; ++Column)
		{
			if (MatchedRowByColumn[Column] == 0)
			{
				continue;
			}
			const int32 RowIndex = MatchedRowByColumn[Column] - 1;
			if (!OutColumnByRow.IsValidIndex(RowIndex))
			{
				OutColumnByRow.Reset();
				return false;
			}
			OutColumnByRow[RowIndex] = Column - 1;
		}
		return !OutColumnByRow.Contains(INDEX_NONE);
	}
}

bool GuLiWingmanTargetAssignment::IsStableMemberLess(
	const FGuLiWingmanHandle& Lhs,
	const FGuLiWingmanHandle& Rhs)
{
	const FGuLiWingmanGroupHandle& LhsGroup = Lhs.Flight.Group;
	const FGuLiWingmanGroupHandle& RhsGroup = Rhs.Flight.Group;
	if (LhsGroup.ShipInstanceId != RhsGroup.ShipInstanceId)
	{
		return IsStableGuidLess(LhsGroup.ShipInstanceId, RhsGroup.ShipInstanceId);
	}
	if (LhsGroup.ShipGeneration != RhsGroup.ShipGeneration)
	{
		return LhsGroup.ShipGeneration < RhsGroup.ShipGeneration;
	}
	if (LhsGroup.GroupGeneration != RhsGroup.GroupGeneration)
	{
		return LhsGroup.GroupGeneration < RhsGroup.GroupGeneration;
	}
	if (Lhs.Flight.FlightIndex != Rhs.Flight.FlightIndex)
	{
		return Lhs.Flight.FlightIndex < Rhs.Flight.FlightIndex;
	}
	if (Lhs.MemberIndex != Rhs.MemberIndex)
	{
		return Lhs.MemberIndex < Rhs.MemberIndex;
	}
	return Lhs.EntityGeneration < Rhs.EntityGeneration;
}

bool GuLiWingmanTargetAssignment::IsStableTargetLess(
	const FGuLiTargetHandle& Lhs,
	const FGuLiTargetHandle& Rhs)
{
	if (Lhs.Kind != Rhs.Kind)
	{
		return static_cast<uint8>(Lhs.Kind) < static_cast<uint8>(Rhs.Kind);
	}
	if (Lhs.AuthorityId != Rhs.AuthorityId)
	{
		return IsStableGuidLess(Lhs.AuthorityId, Rhs.AuthorityId);
	}
	if (Lhs.Generation != Rhs.Generation)
	{
		return Lhs.Generation < Rhs.Generation;
	}
	return Lhs.LocalId < Rhs.LocalId;
}

bool GuLiWingmanTargetAssignment::Solve(
	const FGuLiWingmanTargetAssignmentProblem& Problem,
	TArray<FGuLiWingmanTargetAssignmentPair>& OutAssignments)
{
	OutAssignments.Reset();
	if (Problem.Members.Num() > GULI_WINGMAN_GROUP_SIZE
		|| Problem.SwitchPenaltyCentimeters < 0
		|| Problem.SwitchPenaltyCentimeters > MaximumSafeAssignmentCost)
	{
		return false;
	}

	TArray<FGuLiWingmanHandle> Members = Problem.Members;
	TArray<FGuLiTargetHandle> Targets = Problem.Targets;
	Members.Sort(IsStableMemberLess);
	Targets.Sort(IsStableTargetLess);

	TSet<FGuLiWingmanHandle> MemberSet;
	const FGuLiWingmanGroupHandle ExpectedGroup = Members.IsEmpty()
		? FGuLiWingmanGroupHandle{} : Members[0].Flight.Group;
	for (const FGuLiWingmanHandle& Member : Members)
	{
		if (!Member.IsValid() || Member.Flight.Group != ExpectedGroup
			|| MemberSet.Contains(Member))
		{
			return false;
		}
		MemberSet.Add(Member);
	}
	TSet<FGuLiTargetHandle> TargetSet;
	for (const FGuLiTargetHandle& Target : Targets)
	{
		if (!Target.IsValid() || TargetSet.Contains(Target))
		{
			return false;
		}
		TargetSet.Add(Target);
	}

	TMap<FGuLiWingmanHandle, TMap<FGuLiTargetHandle, int64>> CostByMemberAndTarget;
	for (const FGuLiWingmanTargetAssignmentEdge& Edge : Problem.LegalEdges)
	{
		if (!MemberSet.Contains(Edge.Emitter) || !TargetSet.Contains(Edge.Target)
			|| Edge.DistanceCentimeters < 0 || Edge.DistanceCentimeters > MaximumSafeAssignmentCost
			|| (Edge.bSwitchesFromRetainableTarget
				&& Edge.DistanceCentimeters > MaximumSafeAssignmentCost - Problem.SwitchPenaltyCentimeters))
		{
			return false;
		}
		TMap<FGuLiTargetHandle, int64>& MemberCosts = CostByMemberAndTarget.FindOrAdd(Edge.Emitter);
		if (MemberCosts.Contains(Edge.Target))
		{
			return false;
		}
		MemberCosts.Add(Edge.Target, Edge.DistanceCentimeters
			+ (Edge.bSwitchesFromRetainableTarget ? Problem.SwitchPenaltyCentimeters : 0));
	}

	if (Members.IsEmpty() || Targets.IsEmpty() || Problem.LegalEdges.IsEmpty())
	{
		return Problem.LegalEdges.IsEmpty();
	}

	TArray<FGuLiWingmanHandle> RemainingMembers = Members;
	for (int32 RoundIndex = 0; !RemainingMembers.IsEmpty(); ++RoundIndex)
	{
		int64 MaximumLegalCost = 0;
		bool bHasLegalEdge = false;
		for (const FGuLiWingmanHandle& Member : RemainingMembers)
		{
			const TMap<FGuLiTargetHandle, int64>* MemberCosts = CostByMemberAndTarget.Find(Member);
			if (!MemberCosts)
			{
				continue;
			}
			for (const TPair<FGuLiTargetHandle, int64>& Pair : *MemberCosts)
			{
				bHasLegalEdge = true;
				MaximumLegalCost = FMath::Max(MaximumLegalCost, Pair.Value);
			}
		}
		if (!bHasLegalEdge)
		{
			break;
		}

		const int32 RowCount = RemainingMembers.Num();
		const int32 RealColumnCount = Targets.Num();
		const int32 ColumnCount = RealColumnCount + RowCount;
		int64 UnassignedCost = 0;
		int64 ForbiddenCost = 0;
		if (ColumnCount < RealColumnCount
			|| !TryMultiplyAndIncrement(RowCount, MaximumLegalCost, UnassignedCost)
			|| !TryMultiplyAndIncrement(RowCount, UnassignedCost, ForbiddenCost))
		{
			OutAssignments.Reset();
			return false;
		}

		auto GetCost = [&](const int32 RowIndex, const int32 ColumnIndex)
		{
			if (ColumnIndex >= RealColumnCount)
			{
				return UnassignedCost;
			}
			const TMap<FGuLiTargetHandle, int64>* MemberCosts =
				CostByMemberAndTarget.Find(RemainingMembers[RowIndex]);
			const int64* LegalCost = MemberCosts ? MemberCosts->Find(Targets[ColumnIndex]) : nullptr;
			return LegalCost ? *LegalCost : ForbiddenCost;
		};

		TArray<int32> ColumnByRow;
		if (!SolveRectangularHungarian(RowCount, ColumnCount, GetCost, ColumnByRow))
		{
			OutAssignments.Reset();
			return false;
		}

		TSet<FGuLiWingmanHandle> AssignedThisRound;
		for (int32 RowIndex = 0; RowIndex < RowCount; ++RowIndex)
		{
			const int32 ColumnIndex = ColumnByRow[RowIndex];
			if (ColumnIndex >= RealColumnCount)
			{
				continue;
			}
			const FGuLiWingmanHandle& Member = RemainingMembers[RowIndex];
			const FGuLiTargetHandle& Target = Targets[ColumnIndex];
			const TMap<FGuLiTargetHandle, int64>* MemberCosts = CostByMemberAndTarget.Find(Member);
			const int64* LegalCost = MemberCosts ? MemberCosts->Find(Target) : nullptr;
			if (!LegalCost)
			{
				OutAssignments.Reset();
				return false;
			}
			FGuLiWingmanTargetAssignmentPair& Assignment = OutAssignments.AddDefaulted_GetRef();
			Assignment.Emitter = Member;
			Assignment.Target = Target;
			Assignment.CostCentimeters = *LegalCost;
			Assignment.RoundIndex = RoundIndex;
			AssignedThisRound.Add(Member);
		}
		if (AssignedThisRound.IsEmpty())
		{
			break;
		}
		RemainingMembers.RemoveAll([&AssignedThisRound](const FGuLiWingmanHandle& Member)
		{
			return AssignedThisRound.Contains(Member);
		});
	}

	OutAssignments.Sort([](const FGuLiWingmanTargetAssignmentPair& Lhs,
		const FGuLiWingmanTargetAssignmentPair& Rhs)
	{
		if (Lhs.Emitter != Rhs.Emitter)
		{
			return GuLiWingmanTargetAssignment::IsStableMemberLess(Lhs.Emitter, Rhs.Emitter);
		}
		return GuLiWingmanTargetAssignment::IsStableTargetLess(Lhs.Target, Rhs.Target);
	});
	return true;
}
