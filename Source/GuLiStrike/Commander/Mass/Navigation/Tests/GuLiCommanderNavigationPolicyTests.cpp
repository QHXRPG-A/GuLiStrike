// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Mass/Navigation/GuLiCommanderNavigationPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NavigationSystem.h"

#include <limits>

namespace GuLiCommanderNavigationPolicyTests
{
	using namespace GuLiCommanderNavigationPolicy;

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderNamedNavDataContractTest,
		"GuLiStrike.Commander.Mass.Navigation.NamedNavDataContract",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderNamedNavDataContractTest::RunTest(const FString& Parameters)
	{
		const UNavigationSystemV1* NavigationSystemDefaults = GetDefault<UNavigationSystemV1>();
		TestNotNull(TEXT("NavigationSystemV1 defaults exist"), NavigationSystemDefaults);
		if (!NavigationSystemDefaults)
		{
			return false;
		}

		const TArray<FNavDataConfig>& SupportedAgents =
			NavigationSystemDefaults->GetSupportedAgents();
		const FNavDataConfig* CommanderConfig = FindRequiredAgentConfig(SupportedAgents);
		TestNotNull(TEXT("DefaultEngine declares the exact CommanderSoldier SupportedAgent"), CommanderConfig);
		if (CommanderConfig)
		{
			TestEqual(TEXT("Commander agent name is exact"), CommanderConfig->Name, GetRequiredAgentName());
			TestEqual(
				TEXT("Commander clearance radius remains 750cm"),
				CommanderConfig->AgentRadius,
				RequiredAgentRadiusCentimeters);
		}

		TArray<FNavDataConfig> DefaultOnly;
		for (const FNavDataConfig& Config : SupportedAgents)
		{
			if (Config.Name == FName(TEXT("Default")))
			{
				DefaultOnly.Add(Config);
				break;
			}
		}
		TestEqual(TEXT("The project still has one separate Default agent"), DefaultOnly.Num(), 1);
		TestNull(
			TEXT("A Default-only configuration cannot satisfy Commander navigation"),
			FindRequiredAgentConfig(DefaultOnly));

		if (CommanderConfig)
		{
			TArray<FNavDataConfig> WrongClearance;
			WrongClearance.Add(*CommanderConfig);
			WrongClearance[0].AgentRadius = 34.0f;
			TestNull(
				TEXT("A name match cannot hide a Default-sized clearance radius"),
				FindRequiredAgentConfig(WrongClearance));
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderSharedTargetProjectionTest,
		"GuLiStrike.Commander.Mass.Navigation.SharedTargetProjection",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderSharedTargetProjectionTest::RunTest(const FString& Parameters)
	{
		const FVector RequestedTarget(1000.0, 2000.0, 50.0);
		TestTrue(
			TEXT("An exact CommanderSoldier projection is accepted"),
			IsProjectedTargetAcceptable(
				RequestedTarget,
				RequestedTarget,
				RequiredAgentRadiusCentimeters));
		TestTrue(
			TEXT("Vertical NavMesh correction does not consume horizontal tolerance"),
			IsProjectedTargetAcceptable(
				RequestedTarget,
				RequestedTarget + FVector(750.0, 0.0, 5000.0),
				RequiredAgentRadiusCentimeters));
		TestFalse(
			TEXT("A projection beyond one Soldier radius is InvalidTarget"),
			IsProjectedTargetAcceptable(
				RequestedTarget,
				RequestedTarget + FVector(751.0, 0.0, 0.0),
				RequiredAgentRadiusCentimeters));
		TestFalse(
			TEXT("Non-finite targets are never accepted"),
			IsProjectedTargetAcceptable(
				RequestedTarget,
				FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0),
				RequiredAgentRadiusCentimeters));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderLooseArrivalRadiusTest,
		"GuLiStrike.Commander.Mass.Navigation.LooseArrivalRadius",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderLooseArrivalRadiusTest::RunTest(const FString& Parameters)
	{
		TestEqual(
			TEXT("A 25-Soldier accepted batch gets a 50m arrival domain"),
			CalculateArrivalDomainRadiusCentimeters(25, RequiredAgentRadiusCentimeters),
			5000.0f);
		TestEqual(
			TEXT("Two cohorts share a 65m batch arrival domain"),
			CalculateArrivalDomainRadiusCentimeters(50, RequiredAgentRadiusCentimeters),
			6500.0f);
		TestEqual(
			TEXT("Four cohorts share a 95m batch arrival domain"),
			CalculateArrivalDomainRadiusCentimeters(100, RequiredAgentRadiusCentimeters),
			9500.0f);
		TestEqual(
			TEXT("An empty batch cannot create an arrival domain"),
			CalculateArrivalDomainRadiusCentimeters(0, RequiredAgentRadiusCentimeters),
			0.0f);
		TestEqual(
			TEXT("The 50m domain releases members at 45m and keeps a 5m recovery band"),
			CalculateLooseArrivalHoldRadiusCentimeters(5000.0f),
			4500.0f);
		TestEqual(
			TEXT("A one-member domain retains the minimum 5m hold radius"),
			CalculateLooseArrivalHoldRadiusCentimeters(500.0f),
			500.0f);
		TestEqual(
			TEXT("A 36m/s Soldier lane retains one agent radius of discrete capture margin"),
			CalculateLooseArrivalMaximumLaneOffsetCentimeters(
				5000.0f, 500.0f, RequiredAgentRadiusCentimeters, 3600.0f, 1.0f / 30.0f),
			3750.0f);
		TestEqual(
			TEXT("A one-member domain falls back to its center lane"),
			CalculateLooseArrivalMaximumLaneOffsetCentimeters(
				500.0f, 500.0f, RequiredAgentRadiusCentimeters, 3600.0f, 1.0f / 30.0f),
			0.0f);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderSurfaceMoveAcceptanceTest,
		"GuLiStrike.Commander.Mass.Navigation.SurfaceMoveAcceptance",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderSurfaceMoveAcceptanceTest::RunTest(const FString& Parameters)
	{
		const FVector Previous(1000.0, 2000.0, 300.0);
		TestFalse(
			TEXT("A failed FindMoveAlongSurface result is rejected even when it is flat"),
			IsSurfaceMoveResultAcceptable(false, Previous, Previous));
		TestTrue(
			TEXT("A successful flat surface step is accepted"),
			IsSurfaceMoveResultAcceptable(
				true, Previous, Previous + FVector(120.0, 0.0, 0.0)));
		TestTrue(
			TEXT("The positive 250cm vertical boundary is accepted"),
			IsSurfaceMoveResultAcceptable(
				true, Previous, Previous + FVector(0.0, 0.0, 250.0)));
		TestTrue(
			TEXT("The negative 250cm vertical boundary is accepted"),
			IsSurfaceMoveResultAcceptable(
				true, Previous, Previous + FVector(0.0, 0.0, -250.0)));
		TestFalse(
			TEXT("A successful query cannot cross more than 250cm vertically"),
			IsSurfaceMoveResultAcceptable(
				true, Previous, Previous + FVector(0.0, 0.0, 250.01)));
		TestFalse(
			TEXT("A non-finite surface candidate is rejected"),
			IsSurfaceMoveResultAcceptable(
				true,
				Previous,
				FVector(std::numeric_limits<double>::infinity(), 0.0, 0.0)));
		TestFalse(
			TEXT("A negative vertical policy limit is rejected"),
			IsSurfaceMoveResultAcceptable(true, Previous, Previous, -1.0f));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderMeaningfulProgressTest,
		"GuLiStrike.Commander.Mass.Navigation.MeaningfulProgress",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderMeaningfulProgressTest::RunTest(const FString& Parameters)
	{
		TestTrue(
			TEXT("Advancing the monotonic path cursor is progress even if distance resets"),
			HasMeaningfulNavigationProgress(2, 3, 100.0f, 1000.0f));
		TestFalse(
			TEXT("A 29.99cm waypoint improvement is below the progress threshold"),
			HasMeaningfulNavigationProgress(2, 2, 1000.0f, 970.01f));
		TestTrue(
			TEXT("Exactly 30cm of waypoint improvement is progress"),
			HasMeaningfulNavigationProgress(2, 2, 1000.0f, 970.0f));
		TestFalse(
			TEXT("Moving farther from the same waypoint is not progress"),
			HasMeaningfulNavigationProgress(2, 2, 1000.0f, 1100.0f));
		TestFalse(
			TEXT("Non-finite distances cannot report progress without cursor advancement"),
			HasMeaningfulNavigationProgress(
				2,
				2,
				std::numeric_limits<float>::infinity(),
				0.0f));
		TestFalse(
			TEXT("A non-positive improvement policy cannot make distance-only progress"),
			HasMeaningfulNavigationProgress(2, 2, 100.0f, 50.0f, 0.0f));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderLooseArrivalTerminalPhaseTest,
		"GuLiStrike.Commander.Mass.Navigation.LooseArrivalTerminalPhase",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderLooseArrivalTerminalPhaseTest::RunTest(const FString& Parameters)
	{
		const FVector Target(100000.0, 0.0, 0.0);
		constexpr float ArrivalRadius = 5000.0f;
		constexpr float ApproachPadding = 1800.0f;
		TestFalse(
			TEXT("A direct two-point path retains transit columns while far from its target"),
			HasEnteredLooseArrivalTerminalPhase(
				1, 2, FVector::ZeroVector, Target, ArrivalRadius, ApproachPadding));
		TestTrue(
			TEXT("A direct two-point path releases slots inside the loose-arrival approach"),
			HasEnteredLooseArrivalTerminalPhase(
				1, 2, FVector(94000.0, 0.0, 0.0), Target, ArrivalRadius, ApproachPadding));
		TestFalse(
			TEXT("A multi-bend path cannot enter terminal movement before its final path point"),
			HasEnteredLooseArrivalTerminalPhase(
				2, 4, FVector(94000.0, 0.0, 0.0), Target, ArrivalRadius, ApproachPadding));
		TestTrue(
			TEXT("A multi-bend path releases slots after reaching its final near-target segment"),
			HasEnteredLooseArrivalTerminalPhase(
				3, 4, FVector(94000.0, 0.0, 0.0), Target, ArrivalRadius, ApproachPadding));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderPerMemberLooseArrivalStateTest,
		"GuLiStrike.Commander.Mass.Navigation.PerMemberLooseArrivalState",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderPerMemberLooseArrivalStateTest::RunTest(const FString& Parameters)
	{
		constexpr float ArrivalRadius = 5000.0f;
		FLooseArrivalMemberState State;
		State = UpdateLooseArrivalMemberState(
			State, true, false, 4000.0f, ArrivalRadius);
		TestTrue(TEXT("Tail clearance latches before the final corridor"), State.bTailCleared);
		TestFalse(TEXT("Tail clearance alone cannot release a member"), State.bHasReachedArrival);

		State = UpdateLooseArrivalMemberState(
			State, false, true, 4600.0f, ArrivalRadius);
		TestTrue(TEXT("Tail clearance is monotonic"), State.bTailCleared);
		TestFalse(TEXT("A member outside the 45m hold radius retains its lane"),
			State.bHasReachedArrival);

		State = UpdateLooseArrivalMemberState(
			State, false, true, 4500.0f, ArrivalRadius);
		TestTrue(TEXT("Entering the inner radius latches arrival"), State.bHasReachedArrival);
		TestFalse(TEXT("A newly arrived member holds position"), State.bRecovering);

		State = UpdateLooseArrivalMemberState(
			State, false, true, 5100.0f, ArrivalRadius);
		TestTrue(TEXT("Crossing the outer radius starts recovery without clearing arrival"),
			State.bHasReachedArrival && State.bRecovering);
		State = UpdateLooseArrivalMemberState(
			State, false, true, 4800.0f, ArrivalRadius);
		TestTrue(TEXT("Recovery persists through the 5m hysteresis band"), State.bRecovering);
		State = UpdateLooseArrivalMemberState(
			State, false, true, 4500.0f, ArrivalRadius);
		TestFalse(TEXT("Recovery ends only after returning to the inner radius"),
			State.bRecovering);

		const float MaximumLaneOffset =
			CalculateLooseArrivalMaximumLaneOffsetCentimeters(
				ArrivalRadius,
				500.0f,
				RequiredAgentRadiusCentimeters,
				3600.0f,
				1.0f / 30.0f);
		FLooseArrivalMemberState DiscreteCrossingState;
		for (float LongitudinalOffset = -6000.0f;
			LongitudinalOffset <= 6000.0f;
			LongitudinalOffset += 120.0f)
		{
			const float Distance = FMath::Sqrt(
				FMath::Square(LongitudinalOffset)
				+ FMath::Square(MaximumLaneOffset));
			DiscreteCrossingState = UpdateLooseArrivalMemberState(
				DiscreteCrossingState,
				true,
				true,
				Distance,
				ArrivalRadius);
		}
		TestTrue(TEXT("A 36m/s discrete lane crossing cannot skip the inner arrival domain"),
			DiscreteCrossingState.bHasReachedArrival);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderTransitColumnPolicyTest,
		"GuLiStrike.Commander.Mass.Navigation.TransitColumnPolicy",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderTransitColumnPolicyTest::RunTest(const FString& Parameters)
	{
		const TArray<TArray<uint8>> CorridorSequence = {
			{1u, 1u, 1u, 1u, 1u},
			{1u, 1u, 1u, 0u, 0u},
			{1u, 0u, 0u, 0u, 0u},
			{1u, 1u, 1u, 0u, 0u},
			{1u, 1u, 1u, 1u, 1u}
		};
		const TArray<int32> ExpectedColumns = {5, 3, 1, 3, 5};
		for (int32 Step = 0; Step < CorridorSequence.Num(); ++Step)
		{
			TestEqual(
				*FString::Printf(TEXT("Transit step %d shrinks/recovers deterministically"), Step),
				SelectTransitColumnCount(CorridorSequence[Step]),
				ExpectedColumns[Step]);
		}

		const TArray<uint8> NoCrossSectionFits = {0u, 0u, 0u, 0u, 0u};
		TestEqual(
			TEXT("A failed transit cross-section remains a one-column movement fallback"),
			SelectTransitColumnCount(NoCrossSectionFits),
			1);

		constexpr float SpacingCentimeters = 1800.0f;
		for (int32 ColumnCount = 1; ColumnCount <= MaximumFormationColumns; ++ColumnCount)
		{
			TSet<FIntPoint> QuantizedSlots;
			float MaximumLateralOffset = 0.0f;
			for (int32 SlotIndex = 0; SlotIndex < FormationMemberCapacity; ++SlotIndex)
			{
				const FVector Offset = MakeFormationSlotOffset(
					SlotIndex,
					SpacingCentimeters,
					ColumnCount);
				QuantizedSlots.Add(FIntPoint(
					FMath::RoundToInt(Offset.X),
					FMath::RoundToInt(Offset.Y)));
				MaximumLateralOffset = FMath::Max(MaximumLateralOffset, FMath::Abs(Offset.Y));
			}
			TestEqual(
				*FString::Printf(TEXT("%d-column layout retains 25 unique slots"), ColumnCount),
				QuantizedSlots.Num(),
				FormationMemberCapacity);
			TestTrue(
				*FString::Printf(TEXT("%d-column layout stays inside its lateral width"), ColumnCount),
				MaximumLateralOffset
					<= static_cast<float>(ColumnCount - 1) * SpacingCentimeters * 0.5f
						+ UE_KINDA_SMALL_NUMBER);
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderFinalPathFrameUsesTerminalSegmentTest,
		"GuLiStrike.Commander.Mass.Navigation.FinalPathFrameUsesTerminalSegment",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderFinalPathFrameUsesTerminalSegmentTest::RunTest(const FString& Parameters)
	{
		const TArray<FVector> LShapedPath = {
			FVector(0.0, 0.0, 0.0),
			FVector(10000.0, 0.0, 0.0),
			FVector(10000.0, 10000.0, 0.0),
			FVector(10000.0, 10000.0, 0.0)
		};
		const FFinalPathFrame LFrame = ResolveFinalPathFrame(
			LShapedPath,
			LShapedPath[0],
			LShapedPath.Last());
		TestTrue(TEXT("Repeated terminal points still resolve a usable final direction"),
			LFrame.bHasUsableDirection);
		TestTrue(TEXT("The final formation yaw follows the vertical terminal segment, not the 45-degree chord"),
			FMath::IsNearlyEqual(LFrame.YawDegrees, 90.0f, 0.01f));
		TestTrue(TEXT("An L-shaped path requires the tail to clear its last turn"),
			LFrame.bRequiresTailClear);
		TestTrue(TEXT("The last turn begins 100m along the shared path"),
			FMath::IsNearlyEqual(LFrame.TailClearPathDistanceCentimeters, 10000.0, 0.01));
		TestEqual(TEXT("The L-shaped path caches its final-turn path point"),
			LFrame.TailClearPathPointIndex, 1);

		int32 MemberPathPointIndex = AdvanceMemberPathPointIndex(
			LShapedPath,
			1,
			FVector(5000.0, 0.0, 0.0),
			RequiredAgentRadiusCentimeters,
			4350.0f);
		TestEqual(TEXT("A tail member before the bend retains the corner as its next point"),
			MemberPathPointIndex, 1);
		TestFalse(TEXT("The cached cursor prevents an early L-bend tail-clear latch"),
			HasClearedFinalTurn(LFrame, MemberPathPointIndex));
		MemberPathPointIndex = AdvanceMemberPathPointIndex(
			LShapedPath,
			MemberPathPointIndex,
			FVector(10000.0, 500.0, 0.0),
			RequiredAgentRadiusCentimeters,
			4350.0f);
		TestEqual(TEXT("Crossing the corner advances the member onto the final segment"),
			MemberPathPointIndex, 2);
		TestTrue(TEXT("The final-turn latch becomes true after sequential path progress"),
			HasClearedFinalTurn(LFrame, MemberPathPointIndex));

		const TArray<FVector> HairpinPath = {
			FVector(0.0, 0.0, 0.0),
			FVector(10000.0, 0.0, 0.0),
			FVector(0.0, 0.0, 0.0)
		};
		const FFinalPathFrame HairpinFrame = ResolveFinalPathFrame(
			HairpinPath, HairpinPath[0], HairpinPath.Last());
		const int32 HairpinCursor = AdvanceMemberPathPointIndex(
			HairpinPath, 1, FVector(5000.0, 0.0, 0.0), 100.0f, 4350.0f);
		TestFalse(TEXT("A member on an overlapping hairpin ray cannot skip the actual turn"),
			HasClearedFinalTurn(HairpinFrame, HairpinCursor));
		const int32 OffCorridorCursor = AdvanceMemberPathPointIndex(
			LShapedPath,
			1,
			FVector(10100.0, 5000.0, 0.0),
			RequiredAgentRadiusCentimeters,
			4350.0f);
		TestEqual(TEXT("Crossing a waypoint plane far outside its corridor cannot skip the turn"),
			OffCorridorCursor, 1);
		const int32 WideFormationCursor = AdvanceMemberPathPointIndex(
			LShapedPath,
			1,
			FVector(10001.0, 3600.0, 0.0),
			RequiredAgentRadiusCentimeters,
			4350.0f);
		TestEqual(TEXT("A five-column outer lane can clear the turn without converging on its center"),
			WideFormationCursor, 2);
		const int32 SingleColumnCursor = AdvanceMemberPathPointIndex(
			LShapedPath,
			1,
			FVector(10001.0, 3600.0, 0.0),
			RequiredAgentRadiusCentimeters,
			RequiredAgentRadiusCentimeters);
		TestEqual(TEXT("The same offset cannot bypass a one-column corridor"),
			SingleColumnCursor, 1);
		const TArray<FVector> CloseZigzagPath = {
			FVector::ZeroVector,
			FVector(100.0, 0.0, 0.0),
			FVector(100.0, 100.0, 0.0),
			FVector(200.0, 100.0, 0.0)
		};
		const int32 CloseZigzagCursor = AdvanceMemberPathPointIndex(
			CloseZigzagPath,
			1,
			FVector(100.0, 100.0, 0.0),
			RequiredAgentRadiusCentimeters,
			4350.0f);
		TestEqual(TEXT("Closely spaced physical turns advance at most one waypoint per fixed step"),
			CloseZigzagCursor, 2);
		const FVector ShiftedIncomingWaypoint = CalculatePathLaneWaypoint(
			LShapedPath,
			1,
			3600.0f);
		TestTrue(TEXT("An outer transit lane approaches its own offset corner instead of the center"),
			FMath::IsNearlyEqual(ShiftedIncomingWaypoint.X, 10000.0f, 0.01f)
				&& FMath::IsNearlyEqual(ShiftedIncomingWaypoint.Y, 3600.0f, 0.01f));

		const TArray<FVector> DegeneratePath = {
			FVector(10.0, 20.0, 0.0),
			FVector(10.0, 20.0, 0.0)
		};
		const FFinalPathFrame FallbackFrame = ResolveFinalPathFrame(
			DegeneratePath,
			FVector(0.0, 0.0, 0.0),
			FVector(0.0, -1000.0, 0.0));
		TestTrue(TEXT("An entirely degenerate path uses the deterministic fallback direction"),
			FallbackFrame.bHasUsableDirection
				&& FMath::IsNearlyEqual(FallbackFrame.YawDegrees, -90.0f, 0.01f));
		const FFinalPathFrame UnusableFrame = ResolveFinalPathFrame(
			DegeneratePath,
			DegeneratePath[0],
			DegeneratePath[0]);
		TestFalse(TEXT("A coincident shared path is explicitly unusable instead of becoming a permanent order"),
			UnusableFrame.bHasUsableDirection);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderTransitColumnHysteresisTest,
		"GuLiStrike.Commander.Mass.Navigation.TransitColumnHysteresis",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderTransitColumnHysteresisTest::RunTest(const FString& Parameters)
	{
		FTransitColumnHysteresisState State;
		State = UpdateTransitColumnHysteresis(State, 3, false, 0.0);
		TestEqual(TEXT("A blocked corridor narrows from five to three columns immediately"),
			State.ColumnCount, 3);
		TestEqual(TEXT("Narrowing resets the expansion streak"),
			State.ConsecutiveExpansionSuccessSteps, 0);
		TestEqual(TEXT("Narrowing records its rearrangement time"),
			State.LastRearrangementTimeSeconds, 0.0);

		for (int32 Step = 1; Step < RequiredTransitExpansionSuccessSteps; ++Step)
		{
			State = UpdateTransitColumnHysteresis(
				State,
				MaximumFormationColumns,
				true,
				static_cast<double>(Step) / 30.0);
		}
		TestEqual(TEXT("Fourteen successful steps do not widen the layout"),
			State.ColumnCount, 3);
		TestEqual(TEXT("Fourteen successful steps remain queued"),
			State.ConsecutiveExpansionSuccessSteps, 14);

		State = UpdateTransitColumnHysteresis(
			State,
			MaximumFormationColumns,
			true,
			0.5);
		TestEqual(TEXT("The fifteenth success at 0.5 seconds widens the layout"),
			State.ColumnCount, MaximumFormationColumns);
		TestEqual(TEXT("A completed expansion clears the streak"),
			State.ConsecutiveExpansionSuccessSteps, 0);

		State = UpdateTransitColumnHysteresis(State, 2, true, 0.51);
		TestEqual(TEXT("A second narrowing ignores the rearrangement throttle"),
			State.ColumnCount, 2);
		for (int32 Step = 0; Step < RequiredTransitExpansionSuccessSteps - 1; ++Step)
		{
			State = UpdateTransitColumnHysteresis(State, 4, true, 0.52 + Step * 0.01);
		}
		State = UpdateTransitColumnHysteresis(State, 4, false, 0.70);
		TestEqual(TEXT("A failed movement step resets a pending expansion"),
			State.ConsecutiveExpansionSuccessSteps, 0);

		State.LastRearrangementTimeSeconds = 10.0;
		for (int32 Step = 0; Step < RequiredTransitExpansionSuccessSteps; ++Step)
		{
			State = UpdateTransitColumnHysteresis(State, 4, true, 10.1 + Step * 0.01);
		}
		TestEqual(TEXT("Fifteen successes cannot bypass the 0.5 second throttle"),
			State.ColumnCount, 2);
		TestEqual(TEXT("A throttled expansion retains its completed streak"),
			State.ConsecutiveExpansionSuccessSteps,
			RequiredTransitExpansionSuccessSteps);
		State = UpdateTransitColumnHysteresis(State, 4, true, 10.5);
		TestEqual(TEXT("The pending layout widens when the throttle expires"),
			State.ColumnCount, 4);

		State = UpdateTransitColumnHysteresis(State, 0, false, 10.51);
		TestEqual(TEXT("Invalid corridor widths clamp to the one-column fallback"),
			State.ColumnCount, 1);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderFinalCorridorLaneTest,
		"GuLiStrike.Commander.Mass.Navigation.FinalCorridorLane",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderFinalCorridorLaneTest::RunTest(const FString& Parameters)
	{
		const TArray<FVector> StraightPath = {
			FVector::ZeroVector,
			FVector(10000.0, 0.0, 0.0)
		};
		const FFinalPathFrame Frame = ResolveFinalPathFrame(
			StraightPath, StraightPath[0], StraightPath.Last());
		constexpr float Spacing = 1800.0f;
		const FVector CornerSlot = MakeFormationSlotOffset(0, Spacing, 5);
		TestTrue(TEXT("The legacy 5x5 corner lies outside the 25-member 50m domain"),
			CornerSlot.Size2D() > 5000.0f);
		const FVector CornerMember(2000.0, CornerSlot.Y, 0.0);
		const FVector CornerLaneTarget = CalculateFinalCorridorLaneTarget(
			Frame, StraightPath.Last(), CornerMember, CornerSlot.Y, 1800.0f);
		TestTrue(TEXT("A corner member keeps its lateral lane and advances longitudinally"),
			FMath::IsNearlyEqual(CornerLaneTarget.Y, CornerMember.Y, 0.01f)
				&& CornerLaneTarget.X > CornerMember.X);

		const FVector OneColumnRearSlot = MakeFormationSlotOffset(24, Spacing, 1);
		TestTrue(TEXT("A one-column rear transit slot can be more than 200m behind its guide"),
			FMath::Abs(OneColumnRearSlot.X) > 20000.0f);
		const FVector RearMember(-21600.0, 0.0, 0.0);
		const FVector RearLaneTarget = CalculateFinalCorridorLaneTarget(
			Frame, StraightPath.Last(), RearMember, 0.0f, 1800.0f);
		TestTrue(TEXT("Final-corridor steering discards the one-column longitudinal slot"),
			RearLaneTarget.X > RearMember.X
				&& FMath::IsNearlyEqual(RearLaneTarget.Y, RearMember.Y, 0.01f));
		const FVector OvershotMember(12000.0, 0.0, 0.0);
		const FVector OvershotLaneTarget = CalculateFinalCorridorLaneTarget(
			Frame, StraightPath.Last(), OvershotMember, 0.0f, 1800.0f);
		TestTrue(TEXT("A member that overshoots the arrival disk turns back along its lane"),
			OvershotLaneTarget.X < OvershotMember.X);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderLooseArrivalWaitsForTailTest,
		"GuLiStrike.Commander.Mass.Navigation.LooseArrivalWaitsForTail",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderLooseArrivalWaitsForTailTest::RunTest(const FString& Parameters)
	{
		const TArray<FVector> LShapedPath = {
			FVector(0.0, 0.0, 0.0),
			FVector(10000.0, 0.0, 0.0),
			FVector(10000.0, 10000.0, 0.0)
		};
		const FFinalPathFrame LFrame = ResolveFinalPathFrame(
			LShapedPath,
			LShapedPath[0],
			LShapedPath.Last());
		TArray<FFormationMemberProgressSample> Members;
		Members.SetNum(FormationMemberCapacity);
		for (int32 MemberIndex = 0; MemberIndex < Members.Num(); ++MemberIndex)
		{
			Members[MemberIndex].Location = FVector(10000.0, 1000.0 + MemberIndex * 10.0, 0.0);
			Members[MemberIndex].bAlive = true;
			Members[MemberIndex].bFollowsOrder = true;
			Members[MemberIndex].bTailCleared = true;
			Members[MemberIndex].bHasReachedArrival = true;
		}
		Members[0].Location = FVector(9500.0, 0.0, 0.0);
		Members[0].bTailCleared = false;
		Members[0].bHasReachedArrival = false;

		TestFalse(TEXT("Even an Euclidean-near tail cannot complete through the wall before the L bend"),
			ShouldCompleteOrder(
				true, LFrame, LShapedPath, LShapedPath.Last(), Members, 11000.0f, 100.0f));

		Members[0].bAlive = false;
		TestTrue(TEXT("A dead tail member no longer blocks the surviving order formation"),
			ShouldCompleteOrder(
				true, LFrame, LShapedPath, LShapedPath.Last(), Members, 11000.0f, 100.0f));

		Members[0].bAlive = true;
		Members[0].bFollowsOrder = false;
		TestTrue(TEXT("A tail member superseded by a newer order no longer blocks the old formation"),
			ShouldCompleteOrder(
				true, LFrame, LShapedPath, LShapedPath.Last(), Members, 11000.0f, 100.0f));

		Members[0].bFollowsOrder = true;
		Members[0].Location = FVector(10000.0, 500.0, 0.0);
		Members[0].bTailCleared = true;
		Members[0].bHasReachedArrival = true;
		TestFalse(TEXT("Member readiness cannot bypass leader arrival"),
			ShouldCompleteOrder(
				false, LFrame, LShapedPath, LShapedPath.Last(), Members, 11000.0f, 100.0f));

		const TArray<FVector> ShortStraightPath = {
			FVector(0.0, 0.0, 0.0),
			FVector(1000.0, 0.0, 0.0),
			FVector(1000.0, 0.0, 0.0)
		};
		const FFinalPathFrame StraightFrame = ResolveFinalPathFrame(
			ShortStraightPath,
			ShortStraightPath[0],
			ShortStraightPath.Last());
		for (FFormationMemberProgressSample& Member : Members)
		{
			Member.Location = FVector(-1000.0, 0.0, 0.0);
			Member.bAlive = true;
			Member.bFollowsOrder = true;
			Member.bTailCleared = true;
			Member.bHasReachedArrival = false;
		}
		TestFalse(TEXT("A short straight path still waits for a distant tail to enter the arrival domain"),
			ShouldCompleteOrder(
				true,
				StraightFrame,
				ShortStraightPath,
				ShortStraightPath.Last(),
				Members,
				500.0f,
				100.0f));
		for (FFormationMemberProgressSample& Member : Members)
		{
			Member.Location = FVector(750.0, 0.0, 0.0);
			Member.bHasReachedArrival = true;
		}
		TestTrue(TEXT("A latched arrival remains complete after avoidance moves members outside the domain"),
			ShouldCompleteOrder(
				true,
				StraightFrame,
				ShortStraightPath,
				ShortStraightPath.Last(),
				Members,
				500.0f,
				100.0f));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderBatchCompletionGateTest,
		"GuLiStrike.Commander.Mass.Navigation.BatchCompletionGate",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderBatchCompletionGateTest::RunTest(const FString& Parameters)
	{
		TArray<FBatchOrderFormationCompletionSample> Formations;
		Formations.SetNum(2);
		Formations[0].bHasActiveMembers = true;
		Formations[0].bPathValid = true;
		Formations[0].bGuideAndMembersReady = true;
		Formations[1].bHasActiveMembers = true;
		Formations[1].bPathValid = true;
		Formations[1].bGuideAndMembersReady = false;
		TestFalse(
			TEXT("A leading cohort cannot complete the shared batch before a trailing cohort"),
			ShouldCompleteBatchOrder(Formations));

		Formations[1].bGuideAndMembersReady = true;
		TestTrue(
			TEXT("The batch completes after every active formation is ready"),
			ShouldCompleteBatchOrder(Formations));

		Formations[1].bHasActiveMembers = false;
		Formations[1].bPathValid = false;
		Formations[1].bGuideAndMembersReady = false;
		TestTrue(
			TEXT("A depleted or fully superseded formation does not block the active batch"),
			ShouldCompleteBatchOrder(Formations));

		Formations[0].bPathValid = false;
		TestFalse(
			TEXT("An active formation with an invalid path cannot report batch completion"),
			ShouldCompleteBatchOrder(Formations));

		Formations[0].bHasActiveMembers = false;
		TestFalse(
			TEXT("An empty batch is removal-ready but not falsely reported as completed"),
			ShouldCompleteBatchOrder(Formations));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderCommonActiveOrderTest,
		"GuLiStrike.Commander.Mass.Navigation.CommonActiveOrder",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderCommonActiveOrderTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		TestEqual(TEXT("terminal members are ignored"),
			ResolveCommonActiveOrderId(TArray<uint32>{0u, 19u, 0u, 19u}), 19u);
		TestEqual(TEXT("no executing member reports zero"),
			ResolveCommonActiveOrderId(TArray<uint32>{0u, 0u}), 0u);
		TestEqual(TEXT("mixed executing orders report zero"),
			ResolveCommonActiveOrderId(TArray<uint32>{19u, 0u, 20u}), 0u);
		TestEqual(TEXT("enumeration order cannot change the summary"),
			ResolveCommonActiveOrderId(TArray<uint32>{19u, 19u, 0u}),
			ResolveCommonActiveOrderId(TArray<uint32>{0u, 19u, 19u}));
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCommanderRecoveryThresholdsTest,
		"GuLiStrike.Commander.Mass.Navigation.RecoveryThresholds",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCommanderRecoveryThresholdsTest::RunTest(const FString& Parameters)
	{
		(void)Parameters;
		TestFalse(TEXT("one surface failure stays Normal"),
			ShouldEnterCenterlineRecovery(1, 0.99f));
		TestTrue(TEXT("two consecutive surface failures enter centerline recovery"),
			ShouldEnterCenterlineRecovery(2, 0.0f));
		TestTrue(TEXT("one second without progress enters centerline recovery"),
			ShouldEnterCenterlineRecovery(0, 1.0f));
		TestFalse(TEXT("five total failures stay in centerline recovery"),
			ShouldEnterPersonalPathRecovery(5, 0.99f));
		TestTrue(TEXT("six total failures enter personal path recovery"),
			ShouldEnterPersonalPathRecovery(6, 0.0f));
		TestTrue(TEXT("another second without progress enters personal path recovery"),
			ShouldEnterPersonalPathRecovery(0, 1.0f));
		TestFalse(TEXT("the first personal path failure cannot block"),
			ShouldBlockPersonalPathRecovery(1, 2.0f));
		TestFalse(TEXT("two queries wait for the second no-progress window"),
			ShouldBlockPersonalPathRecovery(2, 1.99f));
		TestTrue(TEXT("one retry plus another two seconds enters Blocked"),
			ShouldBlockPersonalPathRecovery(2, 2.0f));
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
