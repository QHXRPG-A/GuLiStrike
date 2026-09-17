// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Presentation/GuLiCommanderMiniMapTransform.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace GuLiCommanderMiniMapTransformTests
{
	GuLiCommanderMiniMap::FHeadingUpTransform MakeTransform(const float CameraYawDegrees)
	{
		GuLiCommanderMiniMap::FHeadingUpTransform Transform;
		Transform.WorldBounds = FBox2D(FVector2D(-100.0, -100.0), FVector2D(100.0, 100.0));
		Transform.ScreenBounds = FBox2D(FVector2D::ZeroVector, FVector2D(200.0, 200.0));
		Transform.CameraYawDegrees = CameraYawDegrees;
		return Transform;
	}

	bool IsNearlyEqual(
		const FVector2D& Actual,
		const FVector2D& Expected,
		const double Tolerance = 0.001)
	{
		return Actual.Equals(Expected, Tolerance);
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderMiniMapCardinalHeadingsTest,
		"GuLiStrike.Commander.Presentation.MiniMap.CardinalHeadings",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderMiniMapCardinalHeadingsTest::RunTest(const FString& Parameters)
	{
		struct FCardinalCase
		{
			float YawDegrees;
			FVector2D CameraForwardWorldPoint;
			FVector2D CameraRightWorldPoint;
		};
		const FCardinalCase Cases[] = {
			{0.0f, FVector2D(100.0, 0.0), FVector2D(0.0, 100.0)},
			{90.0f, FVector2D(0.0, 100.0), FVector2D(-100.0, 0.0)},
			{180.0f, FVector2D(-100.0, 0.0), FVector2D(0.0, -100.0)},
			{270.0f, FVector2D(0.0, -100.0), FVector2D(100.0, 0.0)}
		};

		for (const FCardinalCase& Case : Cases)
		{
			const GuLiCommanderMiniMap::FHeadingUpTransform Transform =
				MakeTransform(Case.YawDegrees);
			FVector2D ForwardScreen;
			FVector2D RightScreen;
			TestTrue(
				*FString::Printf(TEXT("Yaw %.0f forward remains visible"), Case.YawDegrees),
				Transform.TryWorldToScreen(Case.CameraForwardWorldPoint, ForwardScreen));
			TestTrue(
				*FString::Printf(TEXT("Yaw %.0f right remains visible"), Case.YawDegrees),
				Transform.TryWorldToScreen(Case.CameraRightWorldPoint, RightScreen));
			TestTrue(
				*FString::Printf(TEXT("Yaw %.0f maps camera forward to map top"), Case.YawDegrees),
				IsNearlyEqual(ForwardScreen, FVector2D(100.0, 0.0)));
			TestTrue(
				*FString::Printf(TEXT("Yaw %.0f maps camera right to map right"), Case.YawDegrees),
				IsNearlyEqual(RightScreen, FVector2D(200.0, 100.0)));
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderMiniMapRoundTripTest,
		"GuLiStrike.Commander.Presentation.MiniMap.WorldScreenRoundTrip",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderMiniMapRoundTripTest::RunTest(const FString& Parameters)
	{
		const float Headings[] = {0.0f, 37.0f, 90.0f, 173.0f, -145.0f};
		const FVector2D WorldPoint(21.5, -18.25);
		for (const float Heading : Headings)
		{
			const GuLiCommanderMiniMap::FHeadingUpTransform Transform = MakeTransform(Heading);
			FVector2D ScreenPoint;
			FVector2D RecoveredWorldPoint;
			TestTrue(
				*FString::Printf(TEXT("Yaw %.0f projects the interior point"), Heading),
				Transform.TryWorldToScreen(WorldPoint, ScreenPoint));
			TestTrue(
				*FString::Printf(TEXT("Yaw %.0f accepts the projected point"), Heading),
				Transform.TryScreenToWorld(ScreenPoint, RecoveredWorldPoint));
			TestTrue(
				*FString::Printf(TEXT("Yaw %.0f round-trips world and screen"), Heading),
				IsNearlyEqual(RecoveredWorldPoint, WorldPoint));
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderMiniMapRotatedCornerRejectionTest,
		"GuLiStrike.Commander.Presentation.MiniMap.RotatedCornerRejection",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderMiniMapRotatedCornerRejectionTest::RunTest(const FString& Parameters)
	{
		const GuLiCommanderMiniMap::FHeadingUpTransform Transform = MakeTransform(45.0f);
		FVector2D WorldPoint;
		TestFalse(
			TEXT("A blank fixed-panel corner outside the rotated battlefield is rejected"),
			Transform.TryScreenToWorld(FVector2D(0.0, 0.0), WorldPoint));
		TestTrue(
			TEXT("The panel center remains a valid battlefield point"),
			Transform.TryScreenToWorld(FVector2D(100.0, 100.0), WorldPoint));
		TestTrue(
			TEXT("The panel center still targets the world-bounds center"),
			IsNearlyEqual(WorldPoint, FVector2D::ZeroVector));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderMiniMapStaticPanelAndClippingTest,
		"GuLiStrike.Commander.Presentation.MiniMap.StaticPanelAndClipping",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderMiniMapStaticPanelAndClippingTest::RunTest(const FString& Parameters)
	{
		const FBox2D ExpectedPanel(FVector2D::ZeroVector, FVector2D(200.0, 200.0));
		for (const float Heading : {0.0f, 90.0f, 180.0f, 270.0f})
		{
			const GuLiCommanderMiniMap::FHeadingUpTransform Transform = MakeTransform(Heading);
			TestTrue(
				*FString::Printf(TEXT("Yaw %.0f does not rotate the panel bounds"), Heading),
				Transform.ScreenBounds.Min.Equals(ExpectedPanel.Min)
					&& Transform.ScreenBounds.Max.Equals(ExpectedPanel.Max));
			TestTrue(
				*FString::Printf(TEXT("Yaw %.0f keeps world center at fixed panel center"), Heading),
				IsNearlyEqual(
					Transform.WorldToScreenUnchecked(FVector2D::ZeroVector),
					ExpectedPanel.GetCenter()));
		}

		FVector2D Start(-50.0, 100.0);
		FVector2D End(250.0, 100.0);
		TestTrue(
			TEXT("A crossing dynamic line is clipped to the fixed panel"),
			GuLiCommanderMiniMap::ClipLineToScreenBounds(ExpectedPanel, Start, End));
		TestTrue(TEXT("The clipped start lies on the left edge"), IsNearlyEqual(Start, FVector2D(0.0, 100.0)));
		TestTrue(TEXT("The clipped end lies on the right edge"), IsNearlyEqual(End, FVector2D(200.0, 100.0)));

		FVector2D LastCellMinimum(190.0, 190.0);
		FVector2D LastCellSize(10.5, 10.5);
		TestTrue(
			TEXT("The seam-covering final terrain cell is clipped to the panel"),
			GuLiCommanderMiniMap::ClipRectToScreenBounds(
				ExpectedPanel,
				LastCellMinimum,
				LastCellSize));
		TestTrue(
			TEXT("The final terrain cell keeps its in-panel origin"),
			IsNearlyEqual(LastCellMinimum, FVector2D(190.0, 190.0)));
		TestTrue(
			TEXT("The final terrain cell cannot exceed the panel's right or bottom edge"),
			IsNearlyEqual(LastCellSize, FVector2D(10.0, 10.0)));
		return true;
	}
}

#endif
