// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/GuLiControlCohortBuilder.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace GuLiControlCohortBuilderTests
{
	GuLiControlCohortBuilder::FCandidate MakeCandidate(
		const uint32 SoldierId,
		const float XCentimeters,
		const float YCentimeters = 0.0f)
	{
		return { FGuLiSoldierId(SoldierId), FVector(XCentimeters, YCentimeters, 0.0f) };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiSingleSeedBuildsDynamicTwentyFiveTest,
	"GuLiStrike.Commander.Cohorts.SingleSeedBuildsDynamicTwentyFive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiSingleSeedBuildsDynamicTwentyFiveTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiControlCohortBuilderTests;
	TArray<GuLiControlCohortBuilder::FCandidate> Seeds { MakeCandidate(1u, 0.0f) };
	TArray<GuLiControlCohortBuilder::FCandidate> Fill;
	for (uint32 Id = 2u; Id <= 30u; ++Id)
	{
		Fill.Add(MakeCandidate(Id, static_cast<float>(Id) * 100.0f));
	}
	TArray<TArray<FGuLiSoldierId>> Cohorts;
	GuLiControlCohortBuilder::Build(Seeds, Fill, 30000.0f, Cohorts);
	TestEqual(TEXT("one cohort"), Cohorts.Num(), 1);
	TestEqual(TEXT("control granularity is 25"), Cohorts[0].Num(), 25);
	TestTrue(TEXT("clicked seed remains a member"), Cohorts[0].Contains(FGuLiSoldierId(1u)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiTwentySixSeedsPartitionTest,
	"GuLiStrike.Commander.Cohorts.TwentySixSeedsPartition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiTwentySixSeedsPartitionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiControlCohortBuilderTests;
	TArray<GuLiControlCohortBuilder::FCandidate> Seeds;
	for (uint32 Id = 1u; Id <= 26u; ++Id)
	{
		Seeds.Add(MakeCandidate(Id, static_cast<float>(Id) * 100.0f));
	}
	TArray<TArray<FGuLiSoldierId>> Cohorts;
	GuLiControlCohortBuilder::Build(Seeds, {}, 30000.0f, Cohorts);
	TestEqual(TEXT("two cohorts"), Cohorts.Num(), 2);
	TestEqual(TEXT("first cohort full"), Cohorts[0].Num(), 25);
	TestEqual(TEXT("second cohort understrength"), Cohorts[1].Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiFillDistanceAndEmptySelectionTest,
	"GuLiStrike.Commander.Cohorts.FillDistanceAndEmptySelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiFillDistanceAndEmptySelectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiControlCohortBuilderTests;
	TArray<TArray<FGuLiSoldierId>> Cohorts;
	const TArray<GuLiControlCohortBuilder::FCandidate> Seeds { MakeCandidate(1u, 0.0f) };
	const TArray<GuLiControlCohortBuilder::FCandidate> TooFar {
		MakeCandidate(2u, 30001.0f),
		MakeCandidate(3u, 60000.0f)
	};
	GuLiControlCohortBuilder::Build(Seeds, TooFar, 30000.0f, Cohorts);
	TestEqual(TEXT("far allies do not fill"), Cohorts[0].Num(), 1);
	GuLiControlCohortBuilder::Build({}, TooFar, 30000.0f, Cohorts);
	TestTrue(TEXT("empty selection never pulls outside soldiers"), Cohorts.IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
