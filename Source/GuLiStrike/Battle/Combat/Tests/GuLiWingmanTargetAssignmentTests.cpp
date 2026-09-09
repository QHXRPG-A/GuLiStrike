// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Combat/GuLiWingmanTargetAssignment.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"

namespace GuLiWingmanTargetAssignmentTests
{
	FGuLiWingmanGroupHandle MakeGroup()
	{
		FGuLiWingmanGroupHandle Group;
		Group.ShipInstanceId = FGuid(1u, 2u, 3u, 4u);
		Group.ShipGeneration = 1u;
		Group.GroupGeneration = 1u;
		return Group;
	}

	FGuLiWingmanHandle MakeMember(const int32 Index)
	{
		FGuLiWingmanHandle Member;
		Member.Flight.Group = MakeGroup();
		Member.Flight.FlightIndex = static_cast<uint8>(Index / GULI_WINGMAN_MEMBERS_PER_FLIGHT);
		Member.MemberIndex = static_cast<uint8>(Index % GULI_WINGMAN_MEMBERS_PER_FLIGHT);
		Member.EntityGeneration = 1u;
		return Member;
	}

	FGuLiTargetHandle MakeTarget(const int32 Index)
	{
		FGuLiTargetHandle Target;
		Target.Kind = (Index % 2) == 0
			? EGuLiTargetKind::Ship : EGuLiTargetKind::CommanderSoldier;
		Target.AuthorityId = FGuid(10u, 20u, 30u, static_cast<uint32>(Index + 1));
		Target.Generation = 1u;
		Target.LocalId = static_cast<uint32>(Index + 1);
		return Target;
	}

	FGuLiWingmanTargetAssignmentProblem MakeCompleteProblem(
		const int32 MemberCount,
		const int32 TargetCount)
	{
		FGuLiWingmanTargetAssignmentProblem Problem;
		for (int32 MemberIndex = 0; MemberIndex < MemberCount; ++MemberIndex)
		{
			Problem.Members.Add(MakeMember(MemberIndex));
		}
		for (int32 TargetIndex = 0; TargetIndex < TargetCount; ++TargetIndex)
		{
			Problem.Targets.Add(MakeTarget(TargetIndex));
		}
		for (int32 MemberIndex = 0; MemberIndex < MemberCount; ++MemberIndex)
		{
			for (int32 TargetIndex = 0; TargetIndex < TargetCount; ++TargetIndex)
			{
				FGuLiWingmanTargetAssignmentEdge& Edge = Problem.LegalEdges.AddDefaulted_GetRef();
				Edge.Emitter = Problem.Members[MemberIndex];
				Edge.Target = Problem.Targets[TargetIndex];
				Edge.DistanceCentimeters = static_cast<int64>(TargetIndex) * 100
					+ FMath::Abs(MemberIndex - TargetIndex);
			}
		}
		return Problem;
	}

	bool AreAssignmentsEqual(
		const TArray<FGuLiWingmanTargetAssignmentPair>& Lhs,
		const TArray<FGuLiWingmanTargetAssignmentPair>& Rhs)
	{
		if (Lhs.Num() != Rhs.Num()) return false;
		for (int32 Index = 0; Index < Lhs.Num(); ++Index)
		{
			if (Lhs[Index].Emitter != Rhs[Index].Emitter
				|| Lhs[Index].Target != Rhs[Index].Target
				|| Lhs[Index].CostCentimeters != Rhs[Index].CostCentimeters
				|| Lhs[Index].RoundIndex != Rhs[Index].RoundIndex)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanTargetAssignmentDistributionTest,
	"GuLiStrike.Wingman.Attack.TargetAssignment.Distribution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanTargetAssignmentDistributionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanTargetAssignmentTests;
	for (const TPair<int32, int32>& Shape : {
		TPair<int32, int32>(25, 25), TPair<int32, int32>(25, 8),
		TPair<int32, int32>(25, 1), TPair<int32, int32>(25, 100)})
	{
		const FGuLiWingmanTargetAssignmentProblem Problem = MakeCompleteProblem(Shape.Key, Shape.Value);
		TArray<FGuLiWingmanTargetAssignmentPair> Assignments;
		if (!TestTrue(FString::Printf(TEXT("%dx%d complete problem solves"), Shape.Key, Shape.Value),
			GuLiWingmanTargetAssignment::Solve(Problem, Assignments)))
		{
			return false;
		}
		TestEqual(FString::Printf(TEXT("%dx%d assigns every member"), Shape.Key, Shape.Value),
			Assignments.Num(), Shape.Key);
		TSet<FGuLiWingmanHandle> AssignedMembers;
		TMap<FGuLiTargetHandle, int32> CountsByTarget;
		for (const FGuLiWingmanTargetAssignmentPair& Assignment : Assignments)
		{
			AssignedMembers.Add(Assignment.Emitter);
			++CountsByTarget.FindOrAdd(Assignment.Target);
		}
		TestEqual(TEXT("Each complete-problem member appears once"), AssignedMembers.Num(), Shape.Key);
		if (Shape.Value >= Shape.Key)
		{
			TestEqual(TEXT("A target-rich first round covers distinct targets"), CountsByTarget.Num(), Shape.Key);
		}
		else
		{
			TestEqual(TEXT("Every target is reopened across rounds"), CountsByTarget.Num(), Shape.Value);
			const int32 MinimumExpected = Shape.Key / Shape.Value;
			const int32 MaximumExpected = FMath::DivideAndRoundUp(Shape.Key, Shape.Value);
			for (const TPair<FGuLiTargetHandle, int32>& Count : CountsByTarget)
			{
				TestTrue(TEXT("Repeated rounds balance each target within floor/ceil"),
					Count.Value >= MinimumExpected && Count.Value <= MaximumExpected);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanTargetAssignmentLegalityTest,
	"GuLiStrike.Wingman.Attack.TargetAssignment.LegalityAndDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanTargetAssignmentLegalityTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanTargetAssignmentTests;
	FGuLiWingmanTargetAssignmentProblem Sparse;
	Sparse.Members = {MakeMember(0), MakeMember(1), MakeMember(2)};
	Sparse.Targets = {MakeTarget(0), MakeTarget(1)};
	for (int32 MemberIndex = 0; MemberIndex < 2; ++MemberIndex)
	{
		FGuLiWingmanTargetAssignmentEdge& Edge = Sparse.LegalEdges.AddDefaulted_GetRef();
		Edge.Emitter = Sparse.Members[MemberIndex];
		Edge.Target = Sparse.Targets[0];
		Edge.DistanceCentimeters = MemberIndex;
	}
	TArray<FGuLiWingmanTargetAssignmentPair> SparseAssignments;
	TestTrue(TEXT("Sparse legal graph solves"),
		GuLiWingmanTargetAssignment::Solve(Sparse, SparseAssignments));
	TestEqual(TEXT("Sparse graph leaves the member with no legal edge unassigned"),
		SparseAssignments.Num(), 2);
	TestTrue(TEXT("No illegal edge is synthesized"),
		SparseAssignments.ContainsByPredicate([&Sparse](const FGuLiWingmanTargetAssignmentPair& Assignment)
		{
			return Assignment.Emitter == Sparse.Members[0] && Assignment.Target == Sparse.Targets[0];
		}) && SparseAssignments.ContainsByPredicate([&Sparse](const FGuLiWingmanTargetAssignmentPair& Assignment)
		{
			return Assignment.Emitter == Sparse.Members[1] && Assignment.Target == Sparse.Targets[0];
		}));

	FGuLiWingmanTargetAssignmentProblem Ordered = MakeCompleteProblem(8, 5);
	TArray<FGuLiWingmanTargetAssignmentPair> OrderedResult;
	TArray<FGuLiWingmanTargetAssignmentPair> ReversedResult;
	TestTrue(TEXT("Ordered problem solves"),
		GuLiWingmanTargetAssignment::Solve(Ordered, OrderedResult));
	Algo::Reverse(Ordered.Members);
	Algo::Reverse(Ordered.Targets);
	Algo::Reverse(Ordered.LegalEdges);
	TestTrue(TEXT("Reversed input solves"),
		GuLiWingmanTargetAssignment::Solve(Ordered, ReversedResult));
	TestTrue(TEXT("Input ordering cannot change the stable result"),
		AreAssignmentsEqual(OrderedResult, ReversedResult));

	FGuLiWingmanTargetAssignmentProblem EqualCosts = MakeCompleteProblem(2, 2);
	for (FGuLiWingmanTargetAssignmentEdge& Edge : EqualCosts.LegalEdges)
	{
		Edge.DistanceCentimeters = 10;
	}
	TArray<FGuLiWingmanTargetAssignmentPair> EqualResult;
	TestTrue(TEXT("Equal-cost problem solves"),
		GuLiWingmanTargetAssignment::Solve(EqualCosts, EqualResult));
	TestTrue(TEXT("Equal costs resolve by stable member and target identity"),
		EqualResult.Num() == 2
		&& EqualResult[0].Emitter == MakeMember(0) && EqualResult[0].Target == MakeTarget(0)
		&& EqualResult[1].Emitter == MakeMember(1) && EqualResult[1].Target == MakeTarget(1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiWingmanTargetAssignmentPenaltyAndFailureTest,
	"GuLiStrike.Wingman.Attack.TargetAssignment.SwitchPenaltyAndFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanTargetAssignmentPenaltyAndFailureTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanTargetAssignmentTests;
	FGuLiWingmanTargetAssignmentProblem ZeroCost = MakeCompleteProblem(1, 1);
	TArray<FGuLiWingmanTargetAssignmentPair> ZeroCostResult;
	TestTrue(TEXT("A legal zero-distance edge solves"),
		GuLiWingmanTargetAssignment::Solve(ZeroCost, ZeroCostResult));
	TestTrue(TEXT("The zero-distance edge remains a real assignment"),
		ZeroCostResult.Num() == 1 && ZeroCostResult[0].CostCentimeters == 0);

	FGuLiWingmanTargetAssignmentProblem Penalized;
	Penalized.Members = {MakeMember(0), MakeMember(1)};
	Penalized.Targets = {MakeTarget(0), MakeTarget(1)};
	for (int32 MemberIndex = 0; MemberIndex < 2; ++MemberIndex)
	{
		for (int32 TargetIndex = 0; TargetIndex < 2; ++TargetIndex)
		{
			FGuLiWingmanTargetAssignmentEdge& Edge = Penalized.LegalEdges.AddDefaulted_GetRef();
			Edge.Emitter = Penalized.Members[MemberIndex];
			Edge.Target = Penalized.Targets[TargetIndex];
			Edge.DistanceCentimeters = MemberIndex == TargetIndex ? 100 : 0;
			Edge.bSwitchesFromRetainableTarget = MemberIndex != TargetIndex;
		}
	}
	TArray<FGuLiWingmanTargetAssignmentPair> PenalizedResult;
	TestTrue(TEXT("One-hundred-metre switch penalty problem solves"),
		GuLiWingmanTargetAssignment::Solve(Penalized, PenalizedResult));
	TestTrue(TEXT("The 10000 cm penalty retains both old targets"),
		PenalizedResult.Num() == 2
		&& PenalizedResult[0].Target == MakeTarget(0)
		&& PenalizedResult[1].Target == MakeTarget(1));
	for (FGuLiWingmanTargetAssignmentEdge& Edge : Penalized.LegalEdges)
	{
		Edge.DistanceCentimeters = Edge.bSwitchesFromRetainableTarget ? 0 : 10001;
	}
	TestTrue(TEXT("A distance gain beyond 100 metres can overcome the switch penalty"),
		GuLiWingmanTargetAssignment::Solve(Penalized, PenalizedResult));
	TestTrue(TEXT("The exact 10000 cm threshold permits the globally cheaper switch"),
		PenalizedResult.Num() == 2
		&& PenalizedResult[0].Target == MakeTarget(1)
		&& PenalizedResult[1].Target == MakeTarget(0));

	FGuLiWingmanTargetAssignmentProblem Overflow = MakeCompleteProblem(2, 2);
	for (FGuLiWingmanTargetAssignmentEdge& Edge : Overflow.LegalEdges)
	{
		Edge.DistanceCentimeters = TNumericLimits<int64>::Max() / 8;
	}
	TArray<FGuLiWingmanTargetAssignmentPair> FailureOutput = PenalizedResult;
	TestFalse(TEXT("Penalty arithmetic overflow fails closed"),
		GuLiWingmanTargetAssignment::Solve(Overflow, FailureOutput));
	TestTrue(TEXT("Failure clears all output"), FailureOutput.IsEmpty());

	FGuLiWingmanTargetAssignmentProblem Duplicate = MakeCompleteProblem(1, 1);
	const FGuLiWingmanHandle DuplicateMember = Duplicate.Members[0];
	Duplicate.Members.Add(DuplicateMember);
	FailureOutput = PenalizedResult;
	TestFalse(TEXT("Duplicate member identity fails closed"),
		GuLiWingmanTargetAssignment::Solve(Duplicate, FailureOutput));
	TestTrue(TEXT("Duplicate input also clears output"), FailureOutput.IsEmpty());

	FGuLiWingmanTargetAssignmentProblem DuplicateTarget = MakeCompleteProblem(1, 1);
	const FGuLiTargetHandle DuplicateTargetIdentity = DuplicateTarget.Targets[0];
	DuplicateTarget.Targets.Add(DuplicateTargetIdentity);
	FailureOutput = PenalizedResult;
	TestFalse(TEXT("Duplicate target identity fails closed"),
		GuLiWingmanTargetAssignment::Solve(DuplicateTarget, FailureOutput));
	TestTrue(TEXT("Duplicate-target failure clears output"), FailureOutput.IsEmpty());

	FGuLiWingmanTargetAssignmentProblem DuplicateEdge = MakeCompleteProblem(1, 1);
	const FGuLiWingmanTargetAssignmentEdge DuplicateLegalEdge = DuplicateEdge.LegalEdges[0];
	DuplicateEdge.LegalEdges.Add(DuplicateLegalEdge);
	FailureOutput = PenalizedResult;
	TestFalse(TEXT("Duplicate legal edge fails closed"),
		GuLiWingmanTargetAssignment::Solve(DuplicateEdge, FailureOutput));
	TestTrue(TEXT("Duplicate-edge failure clears output"), FailureOutput.IsEmpty());

	FGuLiWingmanTargetAssignmentProblem InvalidIdentity = MakeCompleteProblem(1, 1);
	InvalidIdentity.Members[0].EntityGeneration = 0u;
	InvalidIdentity.LegalEdges[0].Emitter = InvalidIdentity.Members[0];
	FailureOutput = PenalizedResult;
	TestFalse(TEXT("Invalid member identity fails closed"),
		GuLiWingmanTargetAssignment::Solve(InvalidIdentity, FailureOutput));
	TestTrue(TEXT("Invalid-identity failure clears output"), FailureOutput.IsEmpty());

	FGuLiWingmanTargetAssignmentProblem CrossGroup = MakeCompleteProblem(1, 1);
	CrossGroup.Members[0].Flight.Group.ShipInstanceId.D++;
	CrossGroup.LegalEdges[0].Emitter = CrossGroup.Members[0];
	CrossGroup.Members.Add(MakeMember(1));
	FailureOutput = PenalizedResult;
	TestFalse(TEXT("One Ship solve cannot mix members from different groups"),
		GuLiWingmanTargetAssignment::Solve(CrossGroup, FailureOutput));
	TestTrue(TEXT("Cross-group input also clears output"), FailureOutput.IsEmpty());

	FGuLiWingmanTargetAssignmentProblem NegativeCost = MakeCompleteProblem(1, 1);
	NegativeCost.LegalEdges[0].DistanceCentimeters = -1;
	FailureOutput = PenalizedResult;
	TestFalse(TEXT("Negative cost fails closed"),
		GuLiWingmanTargetAssignment::Solve(NegativeCost, FailureOutput));
	TestTrue(TEXT("Negative-cost failure clears output"), FailureOutput.IsEmpty());
	return true;
}
#endif
