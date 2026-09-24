// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/Navigation/GuLiCommanderDestinationPlanner.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"

namespace GuLiCommanderDestinationPlannerTests
{
	using namespace GuLiCommanderDestinationPlanner;

	FRequest MakeRequest(const int32 SoldierCount)
	{
		FRequest Request;
		Request.TargetAnchor = FVector(100000.0, -50000.0, 250.0);
		Request.MemberSpacingCentimeters = 1800.0f;
		uint32 SoldierId = 1u;
		const int32 CohortCount = FMath::DivideAndRoundUp(SoldierCount, MembersPerBlock);
		for (int32 CohortIndex = 0; CohortIndex < CohortCount; ++CohortIndex)
		{
			FCohortInput& Cohort = Request.Cohorts.AddDefaulted_GetRef();
			Cohort.CohortId = FGuLiControlCohortId(static_cast<uint32>(CohortIndex + 1));
			const int32 CohortMemberCount = FMath::Min(
				MembersPerBlock,
				SoldierCount - CohortIndex * MembersPerBlock);
			for (int32 MemberIndex = 0; MemberIndex < CohortMemberCount; ++MemberIndex)
			{
				FMemberInput& Member = Cohort.Members.AddDefaulted_GetRef();
				Member.SoldierId = FGuLiSoldierId(SoldierId++);
				Member.Location = FVector(
					-80000.0 + static_cast<double>(CohortIndex) * 12000.0
						+ static_cast<double>(MemberIndex / ColumnsPerBlock) * 1800.0,
					-30000.0 + static_cast<double>(MemberIndex % ColumnsPerBlock) * 1800.0,
					250.0);
				Member.Facing = FVector::ForwardVector;
			}
		}
		return Request;
	}

	void GatherWorldCandidates(const FPlan& Plan, TArray<FVector>& OutCandidates)
	{
		OutCandidates.Reset();
		for (const FCohortCandidate& Cohort : Plan.Cohorts)
		{
			for (const FMemberCandidate& Member : Cohort.Members)
			{
				OutCandidates.Add(Member.WorldCandidate);
			}
		}
	}

	bool ValidateUniqueSpacing(
		FAutomationTestBase& Test,
		const FPlan& Plan,
		const int32 ExpectedCount,
		const float MinimumSpacingCentimeters)
	{
		TArray<FVector> Candidates;
		GatherWorldCandidates(Plan, Candidates);
		bool bValid = Test.TestEqual(TEXT("all Soldiers receive one candidate"), Candidates.Num(), ExpectedCount);
		const double MinimumSpacingSquared = FMath::Square(
			static_cast<double>(MinimumSpacingCentimeters) - 0.1);
		for (int32 FirstIndex = 0; FirstIndex < Candidates.Num(); ++FirstIndex)
		{
			for (int32 SecondIndex = FirstIndex + 1; SecondIndex < Candidates.Num(); ++SecondIndex)
			{
				if (FVector::DistSquared2D(Candidates[FirstIndex], Candidates[SecondIndex])
					< MinimumSpacingSquared)
				{
					Test.AddError(FString::Printf(
						TEXT("candidates %d and %d violate %.1fcm spacing"),
						FirstIndex,
						SecondIndex,
						MinimumSpacingCentimeters));
					return false;
				}
			}
		}
		return bValid;
	}

	TMap<uint32, FVector> MakeAssignmentMap(const FPlan& Plan)
	{
		TMap<uint32, FVector> Result;
		for (const FCohortCandidate& Cohort : Plan.Cohorts)
		{
			for (const FMemberCandidate& Member : Cohort.Members)
			{
				Result.Add(Member.SoldierId.Value, Member.WorldCandidate);
			}
		}
		return Result;
	}

	FFreeDestinationSlot MakeFreeSlot(
		const FFreeDestinationCandidate& Candidate,
		const double ProjectedZOffset = 0.0)
	{
		FFreeDestinationSlot Slot;
		Slot.CandidateIndex = Candidate.CandidateIndex;
		Slot.AxialQ = Candidate.AxialQ;
		Slot.AxialR = Candidate.AxialR;
		Slot.OriginalWorldCandidate = Candidate.WorldCandidate;
		Slot.WorldDestination = Candidate.WorldCandidate + FVector(0.0, 0.0, ProjectedZOffset);
		return Slot;
	}

	TMap<uint32, TPair<int32, FVector>> MakeFreeAssignmentMap(
		const FFreeAssignmentPlan& Plan)
	{
		TMap<uint32, TPair<int32, FVector>> Result;
		for (const FFreeCohortAssignment& Cohort : Plan.Cohorts)
		{
			for (const FFreeMemberAssignment& Member : Cohort.Members)
			{
				Result.Add(
					Member.SoldierId.Value,
					TPair<int32, FVector>(Member.CandidateIndex, Member.WorldDestination));
			}
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiDestinationPlannerHexCandidateGeometryTest,
	"GuLiStrike.Commander.Mass.Navigation.DestinationPlanner.HexCandidateGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiDestinationPlannerHexCandidateGeometryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderDestinationPlanner;
	FHexCandidateRequest Request;
	Request.TargetAnchor = FVector(12345.0, -67890.0, 321.0);
	TArray<FFreeDestinationCandidate> Candidates;
	if (!TestTrue(TEXT("default hex candidate build succeeds"), BuildHexCandidates(Request, Candidates)))
	{
		return false;
	}

	TestEqual(TEXT("default 450m hex disk contains 2263 candidates"),
		Candidates.Num(), DefaultFreeCandidateCount);
	if (Candidates.IsEmpty())
	{
		return false;
	}
	TestEqual(TEXT("center candidate is first"), Candidates[0].CandidateIndex, 0);
	TestEqual(TEXT("center axial Q is zero"), Candidates[0].AxialQ, 0);
	TestEqual(TEXT("center axial R is zero"), Candidates[0].AxialR, 0);
	TestTrue(TEXT("center candidate equals requested anchor"),
		Candidates[0].WorldCandidate.Equals(Request.TargetAnchor, 0.001));

	TSet<FIntPoint> SeenCoordinates;
	bool bFoundBoundaryCandidate = false;
	for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
	{
		const FFreeDestinationCandidate& Candidate = Candidates[CandidateIndex];
		TestEqual(TEXT("candidate indices follow sorted array order"),
			Candidate.CandidateIndex, CandidateIndex);
		TestFalse(TEXT("axial coordinate is unique"),
			SeenCoordinates.Contains(FIntPoint(Candidate.AxialQ, Candidate.AxialR)));
		SeenCoordinates.Add(FIntPoint(Candidate.AxialQ, Candidate.AxialR));
		TestTrue(TEXT("candidate stays inside the configured 450m radius"),
			Candidate.DistanceSquaredFromAnchor
				<= FMath::Square(static_cast<double>(Request.MaximumRadiusCentimeters)) + 0.01);
		TestTrue(TEXT("world candidate preserves anchor-relative offset"),
			Candidate.WorldCandidate.Equals(
				Request.TargetAnchor + Candidate.AnchorLocalOffset, 0.001));
		bFoundBoundaryCandidate |= FMath::IsNearlyEqual(
			Candidate.DistanceSquaredFromAnchor,
			FMath::Square(static_cast<double>(Request.MaximumRadiusCentimeters)),
			0.01);
	}
	TestTrue(TEXT("the inclusive disk contains candidates exactly on the 450m edge"),
		bFoundBoundaryCandidate);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiDestinationPlannerHexCandidateOrderingTest,
	"GuLiStrike.Commander.Mass.Navigation.DestinationPlanner.HexCandidateOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiDestinationPlannerHexCandidateOrderingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderDestinationPlanner;
	TArray<FFreeDestinationCandidate> Candidates;
	if (!TestTrue(TEXT("hex candidate build succeeds"),
		BuildHexCandidates(FHexCandidateRequest(), Candidates)))
	{
		return false;
	}

	for (int32 CandidateIndex = 1; CandidateIndex < Candidates.Num(); ++CandidateIndex)
	{
		const FFreeDestinationCandidate& Previous = Candidates[CandidateIndex - 1];
		const FFreeDestinationCandidate& Current = Candidates[CandidateIndex];
		const bool bDistanceIncreases =
			Previous.DistanceSquaredFromAnchor < Current.DistanceSquaredFromAnchor;
		const bool bSameDistance =
			Previous.DistanceSquaredFromAnchor == Current.DistanceSquaredFromAnchor;
		const bool bCoordinatesIncrease = Previous.AxialQ < Current.AxialQ
			|| (Previous.AxialQ == Current.AxialQ && Previous.AxialR < Current.AxialR);
		if (!TestTrue(TEXT("candidates are sorted by distance then axial coordinates"),
			bDistanceIncreases || (bSameDistance && bCoordinatesIncrease)))
		{
			return false;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiDestinationPlannerProjectionPrefixTest,
	"GuLiStrike.Commander.Mass.Navigation.DestinationPlanner.ProjectionPrefix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiDestinationPlannerProjectionPrefixTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderDestinationPlanner;
	TArray<FFreeDestinationCandidate> Candidates;
	if (!TestTrue(TEXT("hex candidate build succeeds"),
		BuildHexCandidates(FHexCandidateRequest(), Candidates)))
	{
		return false;
	}

	TestEqual(TEXT("zero requested candidates produces an empty prefix"),
		FindProjectionPrefixEnd(Candidates, 0), 0);
	TestEqual(TEXT("negative requested candidates produces an empty prefix"),
		FindProjectionPrefixEnd(Candidates, -10), 0);
	TestEqual(TEXT("an empty candidate view produces an empty prefix"),
		FindProjectionPrefixEnd(TConstArrayView<FFreeDestinationCandidate>(), 10), 0);
	TestEqual(TEXT("the center is a complete one-candidate shell"),
		FindProjectionPrefixEnd(Candidates, 1), 1);
	TestEqual(TEXT("requesting the first neighbor completes the seven-candidate shell"),
		FindProjectionPrefixEnd(Candidates, 2), 7);
	TestEqual(TEXT("requesting beyond the first ring completes the next distance shell"),
		FindProjectionPrefixEnd(Candidates, 8), 13);
	TestEqual(TEXT("request beyond the pool clamps to the complete pool"),
		FindProjectionPrefixEnd(Candidates, Candidates.Num() + 100), Candidates.Num());

	int32 PreviousPrefixEnd = 0;
	for (int32 MinimumCandidateCount = 1; MinimumCandidateCount <= 320;
		++MinimumCandidateCount)
	{
		const int32 PrefixEnd = FindProjectionPrefixEnd(Candidates, MinimumCandidateCount);
		if (!TestTrue(TEXT("prefix contains the requested number of candidates"),
			PrefixEnd >= MinimumCandidateCount))
		{
			return false;
		}
		if (!TestTrue(TEXT("prefix limits increase monotonically"),
			PrefixEnd >= PreviousPrefixEnd))
		{
			return false;
		}
		if (PrefixEnd < Candidates.Num()
			&& !TestTrue(TEXT("prefix ends between complete distance shells"),
				Candidates[PrefixEnd - 1].DistanceSquaredFromAnchor
					< Candidates[PrefixEnd].DistanceSquaredFromAnchor))
		{
			return false;
		}
		if (!TestEqual(TEXT("prefix is the smallest complete shell containing the request"),
			Candidates[PrefixEnd - 1].DistanceSquaredFromAnchor,
			Candidates[MinimumCandidateCount - 1].DistanceSquaredFromAnchor))
		{
			return false;
		}
		PreviousPrefixEnd = PrefixEnd;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiDestinationPlannerProjectionPrefixAssignmentEquivalenceTest,
	"GuLiStrike.Commander.Mass.Navigation.DestinationPlanner.ProjectionPrefixAssignmentEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiDestinationPlannerProjectionPrefixAssignmentEquivalenceTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderDestinationPlanner;
	using namespace GuLiCommanderDestinationPlannerTests;
	for (const int32 SoldierCount : { 1, 25, 50, 250, 500 })
	{
		const FRequest Request = MakeRequest(SoldierCount);
		FHexCandidateRequest CandidateRequest;
		CandidateRequest.TargetAnchor = Request.TargetAnchor;
		TArray<FFreeDestinationCandidate> Candidates;
		if (!TestTrue(TEXT("hex candidates build"),
			BuildHexCandidates(CandidateRequest, Candidates)))
		{
			return false;
		}

		const int32 ReserveSlotCount = FMath::Clamp(
			FMath::CeilToInt(static_cast<float>(SoldierCount) * 0.25f), 8, 32);
		const int32 DesiredLegalSlotCount = SoldierCount + ReserveSlotCount;
		TArray<FFreeDestinationSlot> FullLegalSlots;
		int32 DesiredLegalCandidateCount = INDEX_NONE;
		for (const FFreeDestinationCandidate& Candidate : Candidates)
		{
			// A stable sparse mask models projection/reservation rejection without
			// introducing any world or NavMesh dependency into the pure test.
			if (Candidate.CandidateIndex % 7 == 3)
			{
				continue;
			}
			FullLegalSlots.Add(MakeFreeSlot(Candidate));
			if (FullLegalSlots.Num() == DesiredLegalSlotCount)
			{
				DesiredLegalCandidateCount = Candidate.CandidateIndex + 1;
			}
		}
		if (!TestTrue(TEXT("sparse pool contains the desired legal-slot reserve"),
			DesiredLegalCandidateCount > 0))
		{
			return false;
		}

		const int32 PrefixEnd = FindProjectionPrefixEnd(
			Candidates, DesiredLegalCandidateCount);
		TArray<FFreeDestinationSlot> PrefixLegalSlots;
		for (const FFreeDestinationSlot& Slot : FullLegalSlots)
		{
			if (Slot.CandidateIndex >= PrefixEnd)
			{
				break;
			}
			PrefixLegalSlots.Add(Slot);
		}
		Algo::Reverse(PrefixLegalSlots);

		FFreeAssignmentPlan PrefixPlan;
		FFreeAssignmentPlan FullPlan;
		if (!TestTrue(TEXT("prefix assignment succeeds"), AssignFreeDestinations(
				Request, PrefixLegalSlots, DefaultSoftAnchorPitchCentimeters, PrefixPlan))
			|| !TestTrue(TEXT("complete-pool assignment succeeds"), AssignFreeDestinations(
				Request, FullLegalSlots, DefaultSoftAnchorPitchCentimeters, FullPlan)))
		{
			return false;
		}
		if (!TestTrue(TEXT("prefix assignment is complete"), PrefixPlan.IsComplete())
			|| !TestTrue(TEXT("complete-pool assignment is complete"), FullPlan.IsComplete()))
		{
			return false;
		}

		const TMap<uint32, TPair<int32, FVector>> PrefixAssignments =
			MakeFreeAssignmentMap(PrefixPlan);
		const TMap<uint32, TPair<int32, FVector>> FullAssignments =
			MakeFreeAssignmentMap(FullPlan);
		if (!TestEqual(TEXT("prefix and complete pool assign the same member count"),
			PrefixAssignments.Num(), FullAssignments.Num()))
		{
			return false;
		}
		for (const TPair<uint32, TPair<int32, FVector>>& Assignment : PrefixAssignments)
		{
			const TPair<int32, FVector>* FullAssignment =
				FullAssignments.Find(Assignment.Key);
			if (!TestNotNull(TEXT("member exists in complete-pool assignment"), FullAssignment))
			{
				return false;
			}
			if (!TestEqual(TEXT("member keeps the same candidate"),
				Assignment.Value.Key, FullAssignment->Key)
				|| !TestTrue(TEXT("member keeps the same world destination"),
					Assignment.Value.Value.Equals(FullAssignment->Value, 0.001)))
			{
				return false;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiDestinationPlannerFreeAssignmentInvariantTest,
	"GuLiStrike.Commander.Mass.Navigation.DestinationPlanner.FreeAssignmentInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiDestinationPlannerFreeAssignmentInvariantTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderDestinationPlanner;
	using namespace GuLiCommanderDestinationPlannerTests;
	FRequest ForwardRequest = MakeRequest(50);
	TArray<FFreeDestinationCandidate> Candidates;
	FHexCandidateRequest CandidateRequest;
	CandidateRequest.TargetAnchor = ForwardRequest.TargetAnchor;
	TestTrue(TEXT("hex candidates build"), BuildHexCandidates(CandidateRequest, Candidates));

	TArray<FFreeDestinationSlot> ForwardSlots;
	for (int32 CandidateIndex = 0; CandidateIndex < 80; ++CandidateIndex)
	{
		ForwardSlots.Add(MakeFreeSlot(Candidates[CandidateIndex],
			static_cast<double>(CandidateIndex % 3) * 25.0));
	}
	FRequest ReversedRequest = ForwardRequest;
	Algo::Reverse(ReversedRequest.Cohorts);
	for (FCohortInput& Cohort : ReversedRequest.Cohorts)
	{
		Algo::Reverse(Cohort.Members);
	}
	TArray<FFreeDestinationSlot> ReversedSlots = ForwardSlots;
	Algo::Reverse(ReversedSlots);

	FFreeAssignmentPlan ForwardPlan;
	FFreeAssignmentPlan ReversedPlan;
	TestTrue(TEXT("forward free assignment succeeds"), AssignFreeDestinations(
		ForwardRequest, ForwardSlots, DefaultSoftAnchorPitchCentimeters, ForwardPlan));
	TestTrue(TEXT("reversed free assignment succeeds"), AssignFreeDestinations(
		ReversedRequest, ReversedSlots, DefaultSoftAnchorPitchCentimeters, ReversedPlan));
	TestTrue(TEXT("forward free assignment is complete"), ForwardPlan.IsComplete());
	TestTrue(TEXT("reversed free assignment is complete"), ReversedPlan.IsComplete());

	const TMap<uint32, TPair<int32, FVector>> ForwardAssignments =
		MakeFreeAssignmentMap(ForwardPlan);
	const TMap<uint32, TPair<int32, FVector>> ReversedAssignments =
		MakeFreeAssignmentMap(ReversedPlan);
	TestEqual(TEXT("both free plans assign the same Soldier count"),
		ForwardAssignments.Num(), ReversedAssignments.Num());
	for (const TPair<uint32, TPair<int32, FVector>>& Assignment : ForwardAssignments)
	{
		const TPair<int32, FVector>* ReversedAssignment =
			ReversedAssignments.Find(Assignment.Key);
		TestNotNull(TEXT("Soldier remains assigned after every input permutation"),
			ReversedAssignment);
		if (ReversedAssignment)
		{
			TestEqual(TEXT("Soldier keeps the same free candidate"),
				Assignment.Value.Key, ReversedAssignment->Key);
			TestTrue(TEXT("Soldier keeps the same projected destination"),
				Assignment.Value.Value.Equals(ReversedAssignment->Value, 0.001));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiDestinationPlannerFreePartialAssignmentTest,
	"GuLiStrike.Commander.Mass.Navigation.DestinationPlanner.FreePartialAssignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiDestinationPlannerFreePartialAssignmentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderDestinationPlanner;
	using namespace GuLiCommanderDestinationPlannerTests;
	const FRequest Request = MakeRequest(6);
	FHexCandidateRequest CandidateRequest;
	CandidateRequest.TargetAnchor = Request.TargetAnchor;
	TArray<FFreeDestinationCandidate> Candidates;
	TestTrue(TEXT("hex candidates build"), BuildHexCandidates(CandidateRequest, Candidates));

	TArray<FFreeDestinationSlot> ExtraSlots;
	ExtraSlots.Add(MakeFreeSlot(Candidates[17]));
	ExtraSlots.Add(MakeFreeSlot(Candidates[0]));
	ExtraSlots.Add(MakeFreeSlot(Candidates[2]));
	ExtraSlots.Add(MakeFreeSlot(Candidates[8]));
	FFreeAssignmentPlan CenterPriorityPlan;
	FRequest OneMemberRequest = MakeRequest(1);
	TestTrue(TEXT("center-priority assignment succeeds"), AssignFreeDestinations(
		OneMemberRequest,
		ExtraSlots,
		DefaultSoftAnchorPitchCentimeters,
		CenterPriorityPlan));
	TestEqual(TEXT("one member receives one slot"), CenterPriorityPlan.AssignedMemberCount, 1);
	TestEqual(TEXT("closest legal slot wins regardless of input order"),
		CenterPriorityPlan.Cohorts[0].Members[0].CandidateIndex, 0);

	TArray<FFreeDestinationSlot> PartialSlots;
	PartialSlots.Add(MakeFreeSlot(Candidates[8]));
	PartialSlots.Add(MakeFreeSlot(Candidates[2]));
	PartialSlots.Add(MakeFreeSlot(Candidates[0]));
	FFreeAssignmentPlan PartialPlan;
	TestTrue(TEXT("slot-starved free assignment is still a valid plan"), AssignFreeDestinations(
		Request,
		PartialSlots,
		DefaultSoftAnchorPitchCentimeters,
		PartialPlan));
	TestEqual(TEXT("plan retains requested member count"), PartialPlan.RequestedMemberCount, 6);
	TestEqual(TEXT("only available slots are assigned"), PartialPlan.AssignedMemberCount, 3);
	TestFalse(TEXT("slot-starved assignment reports incomplete"), PartialPlan.IsComplete());

	TSet<int32> AssignedCandidateIndices;
	bool bUsesHexVerticalOffset = false;
	for (const FFreeMemberAssignment& Assignment : PartialPlan.Cohorts[0].Members)
	{
		AssignedCandidateIndices.Add(Assignment.CandidateIndex);
		bUsesHexVerticalOffset |= !FMath::IsNearlyZero(
			FMath::Fmod(
				FMath::Abs(Assignment.WorldDestination.Y - Request.TargetAnchor.Y),
				1800.0),
			0.01);
	}
	TestEqual(TEXT("partial assignments use unique free candidates"),
		AssignedCandidateIndices.Num(), 3);
	TestTrue(TEXT("free assignments are not restricted to the legacy five-by-five lattice"),
		bUsesHexVerticalOffset);

	const TArray<FFreeDestinationSlot> EmptySlots;
	FFreeAssignmentPlan ExhaustedPlan;
	TestTrue(TEXT("an exhausted candidate pool is a valid empty assignment"),
		AssignFreeDestinations(
			Request,
			EmptySlots,
			DefaultSoftAnchorPitchCentimeters,
			ExhaustedPlan));
	TestEqual(TEXT("exhausted pool preserves requested member count"),
		ExhaustedPlan.RequestedMemberCount, 6);
	TestEqual(TEXT("exhausted pool assigns no members"),
		ExhaustedPlan.AssignedMemberCount, 0);
	TestFalse(TEXT("exhausted pool is incomplete"), ExhaustedPlan.IsComplete());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiDestinationPlannerScaleAndSpacingTest,
	"GuLiStrike.Commander.Mass.Navigation.DestinationPlanner.ScaleAndSpacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiDestinationPlannerScaleAndSpacingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderDestinationPlanner;
	using namespace GuLiCommanderDestinationPlannerTests;
	for (const int32 SoldierCount : {25, 250, 500})
	{
		const FRequest Request = MakeRequest(SoldierCount);
		FPlan Plan;
		if (!TestTrue(
			*FString::Printf(TEXT("%d-Soldier plan succeeds"), SoldierCount),
			Build(Request, Plan)))
		{
			continue;
		}
		TestEqual(
			*FString::Printf(TEXT("%d-Soldier cohort count"), SoldierCount),
			Plan.Cohorts.Num(),
			FMath::DivideAndRoundUp(SoldierCount, MembersPerBlock));
		TestEqual(TEXT("block pitch remains five member spacings"), Plan.BlockPitchCentimeters, 9000.0f);
		ValidateUniqueSpacing(*this, Plan, SoldierCount, 1800.0f);

		FVector BlockCentroid = FVector::ZeroVector;
		for (const FCohortCandidate& Cohort : Plan.Cohorts)
		{
			BlockCentroid += Cohort.WorldBlockCenter;
		}
		BlockCentroid /= static_cast<double>(Plan.Cohorts.Num());
		TestTrue(
			TEXT("the complete block grid is centered on the requested click"),
			BlockCentroid.Equals(Request.TargetAnchor, 0.1));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiDestinationPlannerInputOrderInvariantTest,
	"GuLiStrike.Commander.Mass.Navigation.DestinationPlanner.InputOrderInvariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiDestinationPlannerInputOrderInvariantTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderDestinationPlanner;
	using namespace GuLiCommanderDestinationPlannerTests;
	FRequest ForwardRequest = MakeRequest(250);
	FRequest ReversedRequest = ForwardRequest;
	Algo::Reverse(ReversedRequest.Cohorts);
	for (FCohortInput& Cohort : ReversedRequest.Cohorts)
	{
		Algo::Reverse(Cohort.Members);
	}

	FPlan ForwardPlan;
	FPlan ReversedPlan;
	TestTrue(TEXT("forward plan succeeds"), Build(ForwardRequest, ForwardPlan));
	TestTrue(TEXT("reversed plan succeeds"), Build(ReversedRequest, ReversedPlan));
	const TMap<uint32, FVector> ForwardAssignments = MakeAssignmentMap(ForwardPlan);
	const TMap<uint32, FVector> ReversedAssignments = MakeAssignmentMap(ReversedPlan);
	TestEqual(TEXT("both plans assign every Soldier"), ForwardAssignments.Num(), ReversedAssignments.Num());
	for (const TPair<uint32, FVector>& Assignment : ForwardAssignments)
	{
		const FVector* ReversedCandidate = ReversedAssignments.Find(Assignment.Key);
		TestNotNull(TEXT("Soldier remains present after permutation"), ReversedCandidate);
		if (ReversedCandidate)
		{
			TestTrue(
				*FString::Printf(TEXT("Soldier %u keeps its deterministic slot"), Assignment.Key),
				Assignment.Value.Equals(*ReversedCandidate, 0.01));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiDestinationPlannerDirectionFallbackTest,
	"GuLiStrike.Commander.Mass.Navigation.DestinationPlanner.DirectionFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiDestinationPlannerDirectionFallbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderDestinationPlanner;
	FRequest FacingRequest;
	FacingRequest.TargetAnchor = FVector(5000.0, 6000.0, 100.0);
	FCohortInput& Cohort = FacingRequest.Cohorts.AddDefaulted_GetRef();
	Cohort.CohortId = FGuLiControlCohortId(1u);
	for (uint32 SoldierId = 1u; SoldierId <= 4u; ++SoldierId)
	{
		FMemberInput& Member = Cohort.Members.AddDefaulted_GetRef();
		Member.SoldierId = FGuLiSoldierId(SoldierId);
		Member.Location = FacingRequest.TargetAnchor;
		Member.Facing = FVector::RightVector;
	}

	FPlan FacingPlan;
	TestTrue(TEXT("coincident target plan succeeds"), Build(FacingRequest, FacingPlan));
	TestTrue(TEXT("mean Soldier facing supplies zero-distance direction"),
		FacingPlan.Forward.Equals(FVector::RightVector, 0.001));

	for (FMemberInput& Member : FacingRequest.Cohorts[0].Members)
	{
		Member.Facing = FVector::ZeroVector;
	}
	FPlan WorldForwardPlan;
	TestTrue(TEXT("zero-facing plan succeeds"), Build(FacingRequest, WorldForwardPlan));
	TestTrue(TEXT("world +X is the final deterministic fallback"),
		WorldForwardPlan.Forward.Equals(FVector::ForwardVector, 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiDestinationPlannerUnderstrengthCenteringTest,
	"GuLiStrike.Commander.Mass.Navigation.DestinationPlanner.UnderstrengthCentering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiDestinationPlannerUnderstrengthCenteringTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderDestinationPlanner;
	using namespace GuLiCommanderDestinationPlannerTests;
	bool bAllValid = true;
	for (int32 MemberCount = 1; MemberCount <= MembersPerBlock; ++MemberCount)
	{
		const FRequest Request = MakeRequest(MemberCount);
		FPlan Plan;
		if (!TestTrue(
			*FString::Printf(TEXT("%d-member final block succeeds"), MemberCount),
			Build(Request, Plan)))
		{
			bAllValid = false;
			continue;
		}
		TestEqual(TEXT("understrength selection remains one cohort"), Plan.Cohorts.Num(), 1);
		FVector LocalCentroid = FVector::ZeroVector;
		for (const FMemberCandidate& Member : Plan.Cohorts[0].Members)
		{
			LocalCentroid += Member.BlockLocalOffset;
			bAllValid &= TestTrue(
				TEXT("understrength member keeps a stable five-by-five slot index"),
				Member.SlotIndex >= 0 && Member.SlotIndex < MembersPerBlock);
			bAllValid &= TestTrue(
				TEXT("understrength member remains on the fixed five-by-five lattice"),
				FMath::IsNearlyZero(
					FMath::Fmod(FMath::Abs(Member.BlockLocalOffset.X), 1800.0),
					0.01)
				&& FMath::IsNearlyZero(
					FMath::Fmod(FMath::Abs(Member.BlockLocalOffset.Y), 1800.0),
					0.01));
		}
		LocalCentroid /= static_cast<double>(Plan.Cohorts[0].Members.Num());
		bAllValid &= TestTrue(
			TEXT("actual understrength slots are centered inside their block"),
			LocalCentroid.IsNearlyZero(0.01));
		bAllValid &= ValidateUniqueSpacing(*this, Plan, MemberCount, 1800.0f);
	}
	return bAllValid;
}

#endif // WITH_DEV_AUTOMATION_TESTS
