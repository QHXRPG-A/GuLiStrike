// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/GuLiCommanderSelectionQuery.h"
#include "Commander/Mass/GuLiControlCohortBuilder.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace GuLiCommanderSelectionTests
{
	GuLiCommanderSelectionQuery::FCandidate MakeCandidate(
		const uint32 Id,
		const FVector& Location,
		const uint16 UnitTypeId = GULI_DEFAULT_SOLDIER_UNIT_TYPE_ID,
		const EGuLiTeam Team = EGuLiTeam::Red,
		const bool bAlive = true)
	{
		return {FGuLiSoldierId(Id), Location, FVector::ZeroVector, Team, UnitTypeId, bAlive};
	}

	FGuLiSelectionRequest MakePointRequest(const EGuLiSelectionKind Kind = EGuLiSelectionKind::Point)
	{
		FGuLiSelectionRequest Request;
		Request.Kind = Kind;
		Request.ClientRequestId = 1u;
		Request.SeedSoldierId = FGuLiSoldierId(1u);
		Request.RayOrigin = FVector(0.0, 0.0, 10000.0);
		Request.RayDirection = FVector(0.0, 0.0, -1.0);
		if (Kind == EGuLiSelectionKind::SameType)
		{
			Request.BoxTopLeftRay = FVector(-6,-6,-1).GetSafeNormal();
			Request.BoxTopRightRay = FVector(6,-6,-1).GetSafeNormal();
			Request.BoxBottomRightRay = FVector(6,6,-1).GetSafeNormal();
			Request.BoxBottomLeftRay = FVector(-6,6,-1).GetSafeNormal();
		}
		return Request;
	}

	FGuLiSelectionRequest MakeBoxRequest()
	{
		FGuLiSelectionRequest Request = MakePointRequest(EGuLiSelectionKind::Box);
		Request.BoxTopLeftRay = FVector(-0.1, -0.1, -1.0).GetSafeNormal();
		Request.BoxTopRightRay = FVector(0.1, -0.1, -1.0).GetSafeNormal();
		Request.BoxBottomRightRay = FVector(0.1, 0.1, -1.0).GetSafeNormal();
		Request.BoxBottomLeftRay = FVector(-0.1, 0.1, -1.0).GetSafeNormal();
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderExactSelectionShapesTest,
	"GuLiStrike.Commander.Selection.ExactPointBoxAndRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderExactSelectionShapesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderSelectionTests;
	using namespace GuLiCommanderSelectionQuery;
	const TArray<FCandidate> Population = {
		MakeCandidate(1u, FVector::ZeroVector),
		MakeCandidate(2u, FVector(500.0, 0.0, 0.0)),
		MakeCandidate(3u, FVector(1500.0, 0.0, 0.0)),
		MakeCandidate(4u, FVector(8100.0, 0.0, 0.0)),
		MakeCandidate(5u, FVector::ZeroVector, 1u, EGuLiTeam::Blue),
		MakeCandidate(6u, FVector::ZeroVector, 1u, EGuLiTeam::Red, false),
		MakeCandidate(7u, FVector(0.0, 0.0, 20000.0))
	};
	TArray<FGuLiSoldierId> Ids;
	TestTrue(TEXT("point hint accepted"), ResolveCandidates(MakePointRequest(), EGuLiTeam::Red, Population, Ids));
	TestTrue(TEXT("one point selects exactly the hinted soldier"), Ids == TArray<FGuLiSoldierId>{FGuLiSoldierId(1u)});
	TestTrue(TEXT("box accepted"), ResolveCandidates(MakeBoxRequest(), EGuLiTeam::Red, Population, Ids));
	TestTrue(TEXT("box selects foot points in its frustum, excluding behind camera/enemy/dead"),
		Ids == TArray<FGuLiSoldierId>{FGuLiSoldierId(1u), FGuLiSoldierId(2u)});

	FGuLiSelectionRequest Radius;
	Radius.ClientRequestId = 2u;
	TestTrue(TEXT("legacy native requests still default to radius"), Radius.Kind == EGuLiSelectionKind::Radius);
	TestTrue(TEXT("radius accepted"), ResolveCandidates(Radius, EGuLiTeam::Red, Population, Ids));
	TestTrue(TEXT("circle includes actual 2D hits only"), Ids.Contains(FGuLiSoldierId(3u)));
	TestFalse(TEXT("circle does not auto-fill a nearby soldier just outside its boundary"), Ids.Contains(FGuLiSoldierId(4u)));
	TestFalse(TEXT("circle never includes enemy"), Ids.Contains(FGuLiSoldierId(5u)));
	TestFalse(TEXT("circle never includes dead"), Ids.Contains(FGuLiSoldierId(6u)));
	Radius.Center = FVector(100000.0, 0.0, 0.0);
	TestTrue(TEXT("empty area is a valid selection"), ResolveCandidates(Radius, EGuLiTeam::Red, Population, Ids));
	TestTrue(TEXT("empty area stays empty"), Ids.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderRotatedSlopeBoxTest,
	"GuLiStrike.Commander.Selection.BoxRotatedSlopeExact26",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderRotatedSlopeBoxTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderSelectionTests;
	using namespace GuLiCommanderSelectionQuery;
	// Camera-local coordinates use X forward, Y screen right and Z screen up.
	// These 26 foot points lie on a slope at different depths, well inside a
	// rectangle whose horizontal/vertical half extents are 0.30/0.20 at X = 1.
	TArray<FCandidate> LocalPopulation;
	TArray<FGuLiSoldierId> ExpectedIds;
	for (int32 Index = 0; Index < 26; ++Index)
	{
		const double Depth = 9000.0 + (Index % 13) * 300.0 + (Index / 13) * 1100.0;
		const double Right = ((Index % 13) - 6) * 120.0;
		const double Height = 250.0 + Right * 0.15 + (Depth - 9000.0) * 0.04;
		const uint32 Id = static_cast<uint32>(Index + 1);
		LocalPopulation.Add(MakeCandidate(Id, FVector(Depth, Right, Height)));
		ExpectedIds.Add(FGuLiSoldierId(Id));
	}
	// Known misses independently exercise all four rectangle edges, camera front,
	// ownership and life state; none may be pulled in to fill the second cohort.
	LocalPopulation.Add(MakeCandidate(101u, FVector(10000.0, -3500.0, 0.0)));
	LocalPopulation.Add(MakeCandidate(102u, FVector(10000.0, 3500.0, 0.0)));
	LocalPopulation.Add(MakeCandidate(103u, FVector(10000.0, 0.0, 2500.0)));
	LocalPopulation.Add(MakeCandidate(104u, FVector(10000.0, 0.0, -2500.0)));
	LocalPopulation.Add(MakeCandidate(105u, FVector(-3000.0, 0.0, 0.0)));
	LocalPopulation.Add(MakeCandidate(106u, FVector(10000.0, 0.0, 0.0), 1u, EGuLiTeam::Red, false));
	LocalPopulation.Add(MakeCandidate(107u, FVector(10000.0, 0.0, 0.0), 1u, EGuLiTeam::Blue));

	for (const FRotator Rotation : {FRotator::ZeroRotator, FRotator(-42.0, 73.0, 19.0)})
	{
		const FQuat CameraRotation = Rotation.Quaternion();
		const FVector CameraOrigin(17500.0, -22000.0, 31000.0);
		FGuLiSelectionRequest Request;
		Request.Kind = EGuLiSelectionKind::Box;
		Request.ClientRequestId = 1u;
		Request.RayOrigin = CameraOrigin;
		// Wire/API corner order is top-left, top-right, bottom-right, bottom-left.
		Request.BoxTopLeftRay = CameraRotation.RotateVector(FVector(1.0, -0.30, 0.20).GetSafeNormal());
		Request.BoxTopRightRay = CameraRotation.RotateVector(FVector(1.0, 0.30, 0.20).GetSafeNormal());
		Request.BoxBottomRightRay = CameraRotation.RotateVector(FVector(1.0, 0.30, -0.20).GetSafeNormal());
		Request.BoxBottomLeftRay = CameraRotation.RotateVector(FVector(1.0, -0.30, -0.20).GetSafeNormal());
		TArray<FCandidate> Population = LocalPopulation;
		for (FCandidate& Candidate : Population)
		{
			Candidate.Location = CameraOrigin + CameraRotation.RotateVector(Candidate.Location);
		}
		TArray<FGuLiSoldierId> Hits;
		TestTrue(TEXT("translated and rotated box request is accepted"),
			ResolveCandidates(Request, EGuLiTeam::Red, Population, Hits));
		Hits.Sort();
		TestTrue(TEXT("yaw/pitch/roll and sloped foot heights preserve exactly the same 26 IDs"), Hits == ExpectedIds);

		TArray<GuLiControlCohortBuilder::FCandidate> Seeds;
		for (const FCandidate& Candidate : Population)
		{
			if (Hits.Contains(Candidate.SoldierId))
			{
				Seeds.Add({Candidate.SoldierId, Candidate.Location});
			}
		}
		TArray<TArray<FGuLiSoldierId>> Cohorts;
		GuLiControlCohortBuilder::Build(Seeds, {}, 0.0f, Cohorts);
		TestEqual(TEXT("actual query hits produce two cohorts"), Cohorts.Num(), 2);
		if (Cohorts.Num() == 2)
		{
			TestEqual(TEXT("first cohort contains 25 actual hits"), Cohorts[0].Num(), 25);
			TestEqual(TEXT("last cohort remains one actual hit without outside fill"), Cohorts[1].Num(), 1);
		}
		TArray<FGuLiSoldierId> CohortIds;
		for (const TArray<FGuLiSoldierId>& Cohort : Cohorts)
		{
			CohortIds.Append(Cohort);
		}
		CohortIds.Sort();
		TestTrue(TEXT("query to cohort builder neither loses nor adds a soldier"), CohortIds == ExpectedIds);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderPointHintValidationTest,
	"GuLiStrike.Commander.Selection.PointHintValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderPointHintValidationTest::RunTest(const FString& Parameters)
{
	// Scale020 fixture: spatial values use final centimeters.

	(void)Parameters;
	using namespace GuLiCommanderSelectionTests;
	using namespace GuLiCommanderSelectionQuery;
	TArray<FCandidate> Population = {
		MakeCandidate(1u, FVector(5000.0, 0.0, 0.0)),
		MakeCandidate(2u, FVector::ZeroVector)
	};
	TArray<FGuLiSoldierId> Ids;
	FGuLiSelectionRequest Request = MakePointRequest(); Request.RayOrigin *= 0.2;
	TestFalse(TEXT("hint far from cursor is rejected"), ResolveCandidates(Request, EGuLiTeam::Red, Population, Ids));
	TestTrue(TEXT("rejected hint never selects a nearer neighbour"), Ids.IsEmpty());
	Population[0].Location = FVector::ZeroVector;
	Request.RayOrigin = FVector(-600.0, 0.0, 200.0);
	Request.RayDirection = FVector::ForwardVector;
	TestTrue(TEXT("stationary body top is clickable without velocity allowance"),
		ResolveCandidates(Request, EGuLiTeam::Red, Population, Ids));
	Request.RayOrigin = FVector(-600.0, 150.0, 100.0);
	TestTrue(TEXT("stationary body side at client pick radius is clickable"),
		ResolveCandidates(Request, EGuLiTeam::Red, Population, Ids));
	Request.RayOrigin = FVector(-600.0, 400.0, 100.0);
	TestFalse(TEXT("body allowance still rejects a ray far beside a stationary soldier"),
		ResolveCandidates(Request, EGuLiTeam::Red, Population, Ids));
	Request = MakePointRequest(); Request.RayOrigin *= 0.2;
	Population[0].Location = FVector(240.0, 0.0, 0.0);
	TestFalse(TEXT("stationary soldier outside body plus angular allowance is rejected"),
		ResolveCandidates(Request, EGuLiTeam::Red, Population, Ids));
	Population[0].Velocity = FVector(200.0, 0.0, 0.0);
	TestTrue(TEXT("bounded moving display error is tolerated"), ResolveCandidates(Request, EGuLiTeam::Red, Population, Ids));
	Population[0].Team = EGuLiTeam::Blue;
	TestFalse(TEXT("enemy seed rejected"), ResolveCandidates(Request, EGuLiTeam::Red, Population, Ids));
	Population[0].Team = EGuLiTeam::Red;
	Population[0].bAlive = false;
	TestFalse(TEXT("dead seed rejected"), ResolveCandidates(Request, EGuLiTeam::Red, Population, Ids));
	Request.SeedSoldierId = FGuLiSoldierId(999u);
	TestFalse(TEXT("unknown seed rejected"), ResolveCandidates(Request, EGuLiTeam::Red, Population, Ids));
	Request = MakePointRequest(); Request.RayOrigin *= 0.2;
	Request.RayDirection = FVector::ZeroVector;
	TestFalse(TEXT("zero direction is malformed"), Request.IsWellFormed());
	Request = MakePointRequest(); Request.RayOrigin *= 0.2;
	Request.PickHalfAngleRadians = 0.5f;
	TestFalse(TEXT("oversized pick aperture is malformed"), Request.IsWellFormed());
	Request = MakeBoxRequest();
	Swap(Request.BoxTopRightRay, Request.BoxBottomRightRay);
	TestFalse(TEXT("crossed box corner order is malformed"), Request.IsWellFormed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderSameTypeCapTest,
	"GuLiStrike.Commander.Selection.SameTypeScreenAnd500Meters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderSameTypeCapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderSelectionTests;
	using namespace GuLiCommanderSelectionQuery;
	for (const int32 EligibleCount : {999, 1000, 1001})
	{
		TArray<FCandidate> Population;
		// Reverse insertion order proves selection is based on distance rather than roster order.
		for (int32 Index = EligibleCount; Index >= 1; --Index)
		{
			Population.Add(MakeCandidate(static_cast<uint32>(Index), FVector((Index - 1) * 10.0, 0.0, 0.0), 7u));
		}
		Population.Add(MakeCandidate(2001u, FVector::ZeroVector, 8u));
		Population.Add(MakeCandidate(2002u, FVector::ZeroVector, 7u, EGuLiTeam::Blue));
		Population.Add(MakeCandidate(2003u, FVector::ZeroVector, 7u, EGuLiTeam::Red, false));
		// TArray rejects Add with a reference into its own storage, even before reallocation.
		const FCandidate DuplicateCandidate = Population.Last();
		Population.Add(DuplicateCandidate);
		TArray<FGuLiSoldierId> Ids;
		TestTrue(TEXT("same type request accepted"), ResolveCandidates(
			MakePointRequest(EGuLiSelectionKind::SameType), EGuLiTeam::Red, Population, Ids));
		TestEqual(TEXT("same-type selection uses the overall protocol capacity"), Ids.Num(), EligibleCount);
		TestTrue(TEXT("clicked seed remains first"), !Ids.IsEmpty() && Ids[0] == FGuLiSoldierId(1u));
		TestTrue(TEXT("visible nearby allies included"), Ids.Contains(FGuLiSoldierId(999u)));
		TestFalse(TEXT("other type excluded"), Ids.Contains(FGuLiSoldierId(2001u)));
		TestFalse(TEXT("enemy excluded"), Ids.Contains(FGuLiSoldierId(2002u)));
		TestFalse(TEXT("dead excluded"), Ids.Contains(FGuLiSoldierId(2003u)));
	}
	const TArray<FCandidate> EqualDistances = {
		MakeCandidate(1u, FVector::ZeroVector),
		MakeCandidate(3u, FVector(1000.0, 0.0, 0.0)),
		MakeCandidate(2u, FVector(-1000.0, 0.0, 0.0))
	};
	TArray<FGuLiSoldierId> Ids;
	ResolveCandidates(MakePointRequest(EGuLiSelectionKind::SameType), EGuLiTeam::Red, EqualDistances, Ids);
	TestTrue(TEXT("equal distance uses stable soldier ID order"),
		Ids == TArray<FGuLiSoldierId>{FGuLiSoldierId(1u), FGuLiSoldierId(2u), FGuLiSoldierId(3u)});
	const TArray<FCandidate> Boundary = { MakeCandidate(1,FVector::ZeroVector), MakeCandidate(2,FVector(50000,0,0)),
		MakeCandidate(3,FVector(50001,0,0)), MakeCandidate(4,FVector(0,40000,0)) };
	auto Request = MakePointRequest(EGuLiSelectionKind::SameType);
	ResolveCandidates(Request, EGuLiTeam::Red, Boundary, Ids);
	TestTrue(TEXT("500m boundary is inclusive"), Ids.Contains(FGuLiSoldierId(2)));
	TestFalse(TEXT("one centimetre beyond radius excluded"), Ids.Contains(FGuLiSoldierId(3)));
	Request.BoxTopLeftRay = FVector(-6,-3,-1).GetSafeNormal(); Request.BoxTopRightRay = FVector(6,-3,-1).GetSafeNormal();
	Request.BoxBottomRightRay = FVector(6,3,-1).GetSafeNormal(); Request.BoxBottomLeftRay = FVector(-6,3,-1).GetSafeNormal();
	ResolveCandidates(Request, EGuLiTeam::Red, Boundary, Ids);
	TestFalse(TEXT("off-screen unit excluded even inside radius"), Ids.Contains(FGuLiSoldierId(4)));
	Request.Center = FVector(20000,0,0);
	ResolveCandidates(Request, EGuLiTeam::Red, Boundary, Ids);
	TestTrue(TEXT("radius is measured from mouse ground point, not seed"), Ids.Contains(FGuLiSoldierId(3)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderAddMembershipTest,
	"GuLiStrike.Commander.Selection.AddMembershipAndCohortCapacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderAddMembershipTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderSelectionQuery;
	TArray<FGuLiSoldierId> Existing = {FGuLiSoldierId(1u), FGuLiSoldierId(2u)};
	const TArray<FGuLiSoldierId> Hits = {FGuLiSoldierId(2u), FGuLiSoldierId(3u), FGuLiSoldierId(3u)};
	TArray<FGuLiSoldierId> Combined;
	CombineMembership(Existing, Hits, EGuLiSelectionModifier::Add, Combined);
	TestTrue(TEXT("Shift adds without toggling existing members"),
		Combined == TArray<FGuLiSoldierId>{FGuLiSoldierId(1u), FGuLiSoldierId(2u), FGuLiSoldierId(3u)});
	Existing = Combined;
	CombineMembership(Existing, Hits, EGuLiSelectionModifier::Add, Combined);
	TestTrue(TEXT("repeated addition has identical membership for no-rebuild fast path"), Existing == Combined);
	for (uint32 Id = 4u; Id <= 1001u; ++Id)
	{
		Existing = MoveTemp(Combined);
		const FGuLiSoldierId OneId(Id);
		CombineMembership(Existing, MakeArrayView(&OneId, 1), EGuLiSelectionModifier::Add, Combined);
	}
	TestEqual(TEXT("Shift accumulated selection is not capped by Alt's 1000 limit"), Combined.Num(), 1001);
	TArray<GuLiControlCohortBuilder::FCandidate> Seeds;
	for (const FGuLiSoldierId Id : Combined)
	{
		Seeds.Add({Id, FVector(Id.Value * 100.0, 0.0, 0.0)});
	}
	TArray<TArray<FGuLiSoldierId>> Cohorts;
	GuLiControlCohortBuilder::Build(Seeds, {}, 0.0f, Cohorts);
	TestEqual(TEXT("1001 single additions use 41 groups, not 1001 groups"), Cohorts.Num(), 41);
	TestEqual(TEXT("last group stays exact, without fill"), Cohorts.Last().Num(), 1);

	Existing.Reset();
	for (uint32 Id = 2u; Id <= 10001u; ++Id)
	{
		Existing.Add(FGuLiSoldierId(Id));
	}
	const FGuLiSoldierId LowerNewId(1u);
	CombineMembership(Existing, MakeArrayView(&LowerNewId, 1), EGuLiSelectionModifier::Add, Combined);
	TestEqual(TEXT("overflow remains visible so authority can reject instead of silently truncating"), Combined.Num(), 10001);
	Existing = {FGuLiSoldierId(1),FGuLiSoldierId(2),FGuLiSoldierId(3)};
	const FGuLiSoldierId Toggle(2);
	CombineMembership(Existing, MakeArrayView(&Toggle,1), EGuLiSelectionModifier::Toggle, Combined);
	TestTrue(TEXT("Shift point toggles only the exact member"), Combined == TArray<FGuLiSoldierId>{FGuLiSoldierId(1),FGuLiSoldierId(3)});
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
