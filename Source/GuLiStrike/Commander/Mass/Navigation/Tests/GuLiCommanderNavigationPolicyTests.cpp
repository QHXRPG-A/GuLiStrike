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

		const FVector TailDirection = CalculateSharedPathFollowDirection(
			LShapedPath,
			FVector(5000.0, 0.0, 0.0),
			750.0f);
		TestTrue(TEXT("A tail member before the bend follows the shared path toward the corner"),
			TailDirection.X > 0.99f && FMath::Abs(TailDirection.Y) < 0.01f);

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
		}
		Members[0].Location = FVector(9500.0, 0.0, 0.0);

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
		}
		TestTrue(TEXT("A short straight path completes after every active member enters the domain"),
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
}

#endif // WITH_DEV_AUTOMATION_TESTS
