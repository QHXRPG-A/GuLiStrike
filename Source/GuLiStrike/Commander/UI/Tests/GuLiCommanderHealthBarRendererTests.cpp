// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Commander/UI/GuLiCommanderHealthBarRenderer.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderHealthBarVisibilityPolicyTest,
	"GuLiStrike.Commander.UI.HealthBar.VisibilityPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderHealthBarVisibilityPolicyTest::RunTest(const FString& Parameters)
{
	using FRenderer = AGuLiCommanderHealthBarRenderer;
	constexpr float MaximumDistance = 30000.0f;

	TestTrue(
		TEXT("A selected full-health living Soldier is visible"),
		FRenderer::TestOnly_ShouldDisplayHealthBar(
			true, 100u, 100u, true, 1000.0f, MaximumDistance));
	TestTrue(
		TEXT("An unselected damaged living Soldier is visible"),
		FRenderer::TestOnly_ShouldDisplayHealthBar(
			true, 75u, 100u, false, 1000.0f, MaximumDistance));
	TestFalse(
		TEXT("An unselected full-health Soldier is hidden"),
		FRenderer::TestOnly_ShouldDisplayHealthBar(
			true, 100u, 100u, false, 1000.0f, MaximumDistance));
	TestFalse(
		TEXT("A destroyed selected Soldier is hidden"),
		FRenderer::TestOnly_ShouldDisplayHealthBar(
			false, 15u, 100u, true, 1000.0f, MaximumDistance));
	TestTrue(
		TEXT("The maximum-distance boundary is inclusive"),
		FRenderer::TestOnly_ShouldDisplayHealthBar(
			true, 55u, 100u, false, MaximumDistance, MaximumDistance));
	TestFalse(
		TEXT("A Soldier beyond the maximum distance is hidden"),
		FRenderer::TestOnly_ShouldDisplayHealthBar(
			true, 55u, 100u, false, MaximumDistance + 1.0f, MaximumDistance));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderHealthBarSizingPolicyTest,
	"GuLiStrike.Commander.UI.HealthBar.SizingPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderHealthBarSizingPolicyTest::RunTest(const FString& Parameters)
{
	using FRenderer = AGuLiCommanderHealthBarRenderer;

	TestTrue(
		TEXT("100/100 produces a full health fraction"),
		FMath::IsNearlyEqual(FRenderer::TestOnly_CalculateHealthFraction(100u, 100u), 1.0f));
	TestTrue(
		TEXT("75/100 produces a 0.75 health fraction"),
		FMath::IsNearlyEqual(FRenderer::TestOnly_CalculateHealthFraction(75u, 100u), 0.75f));
	TestTrue(
		TEXT("55/100 produces a 0.55 health fraction"),
		FMath::IsNearlyEqual(FRenderer::TestOnly_CalculateHealthFraction(55u, 100u), 0.55f));
	TestTrue(
		TEXT("15/100 produces a critical 0.15 health fraction"),
		FMath::IsNearlyEqual(FRenderer::TestOnly_CalculateHealthFraction(15u, 100u), 0.15f));
	TestTrue(
		TEXT("Health is clamped to a 1.0 fraction"),
		FMath::IsNearlyEqual(FRenderer::TestOnly_CalculateHealthFraction(200u, 100u), 1.0f));
	TestTrue(
		TEXT("A zero maximum is handled safely"),
		FMath::IsNearlyZero(FRenderer::TestOnly_CalculateHealthFraction(0u, 0u)));

	const FVector2D SizeAtOneKilometer = FRenderer::TestOnly_CalculateWorldSizeCentimeters(
		1000.0f,
		90.0f,
		1920,
		1080);
	TestTrue(TEXT("84px width is converted from horizontal FOV"), FMath::IsNearlyEqual(
		SizeAtOneKilometer.X, 87.5f, 0.01f));
	TestTrue(TEXT("12px height is converted from horizontal FOV"), FMath::IsNearlyEqual(
		SizeAtOneKilometer.Y, 12.5f, 0.01f));
	TestTrue(TEXT("The requested 7:1 pixel aspect is retained"), FMath::IsNearlyEqual(
		SizeAtOneKilometer.X / SizeAtOneKilometer.Y, 7.0f, 0.001f));

	const FVector2D SizeAtTwoKilometers = FRenderer::TestOnly_CalculateWorldSizeCentimeters(
		2000.0f,
		90.0f,
		1920,
		1080);
	TestTrue(TEXT("World size grows linearly with distance"), SizeAtTwoKilometers.Equals(
		SizeAtOneKilometer * 2.0f, 0.01f));
	TestTrue(TEXT("An invalid viewport returns zero size"),
		FRenderer::TestOnly_CalculateWorldSizeCentimeters(
			1000.0f, 90.0f, 0, 1080).IsNearlyZero());

	return true;
}

#endif
