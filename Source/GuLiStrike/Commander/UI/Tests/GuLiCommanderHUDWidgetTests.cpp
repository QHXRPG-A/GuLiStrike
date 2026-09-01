// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Commander/UI/GuLiCommanderUnitTypeSummary.h"
#include "Commander/UI/GuLiCommanderHUDWidget.h"
#include "Misc/AutomationTest.h"

#include <limits>

namespace GuLiCommanderHUDWidgetTests
{
	FGuLiSoldierStateItem MakeSoldier(
		const uint32 Id,
		const float Health = 100.0f,
		const float MaxHealth = 100.0f)
	{
		FGuLiSoldierStateItem Soldier;
		Soldier.SoldierId = FGuLiSoldierId(Id);
		Soldier.Team = EGuLiTeam::Blue;
		Soldier.Health = Health;
		Soldier.MaxHealth = MaxHealth;
		return Soldier;
	}

	FGuLiControlCohortDescriptor MakeCohort(
		const uint32 Id,
		const TArray<FGuLiSoldierId>& MemberIds)
	{
		FGuLiControlCohortDescriptor Cohort;
		Cohort.CohortId = FGuLiControlCohortId(Id);
		Cohort.MemberIds = MemberIds;
		// Intentionally stale: the HUD must use reliable individual life/health facts.
		Cohort.AliveCount = 0u;
		return Cohort;
	}

	FGuLiCohortCommandAck MakeCohortAck(const uint32 Id, const EGuLiCommandAckResult Result)
	{
		FGuLiCohortCommandAck Ack;
		Ack.CohortId = FGuLiControlCohortId(Id);
		Ack.Result = Result;
		Ack.MemberCount = 1u;
		Ack.EligibleMemberMask = 1u;
		Ack.AcceptedMemberMask = (Result == EGuLiCommandAckResult::Accepted
			|| Result == EGuLiCommandAckResult::PartiallyAccepted) ? 1u : 0u;
		return Ack;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderHUDSurvivorHealthTest,
	"GuLiStrike.Commander.UI.HUD.UnitType.SurvivorHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderHUDSurvivorHealthTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderHUDWidgetTests;
	FGuLiCommanderSelectionState Selection;
	FGuLiControlCohortDescriptor Cohort;
	Cohort.CohortId = FGuLiControlCohortId(147u);
	Cohort.AliveCount = 25u;
	TArray<FGuLiSoldierStateItem> States;
	for (uint32 Id = 1u; Id <= 25u; ++Id)
	{
		Cohort.MemberIds.Add(FGuLiSoldierId(Id));
		States.Add(MakeSoldier(Id));
	}
	Selection.Cohorts.Add(Cohort);

	FGuLiCommanderUnitTypeSummary Summary = BuildGuLiCommanderUnitTypeSummary(
		Selection, FGuLiCommandAck(), States, true);
	TestEqual(TEXT("Initial selected population"), Summary.AliveCount, 25);
	TestTrue(TEXT("Initial health is full"), FMath::IsNearlyEqual(Summary.GetHealthFraction(), 1.0f));
	for (int32 Index = 0; Index < 5; ++Index)
	{
		States[Index].LifeState = EGuLiSoldierLifeState::Destroyed;
		States[Index].Health = 0u;
	}
	Summary = BuildGuLiCommanderUnitTypeSummary(Selection, FGuLiCommandAck(), States, true);
	TestEqual(TEXT("Five deaths subtract from the count despite stale cohort count"), Summary.AliveCount, 20);
	TestEqual(TEXT("Only living soldiers contribute current health"), Summary.TotalHealth, 2000.0f);
	TestEqual(TEXT("Only living soldiers contribute maximum health"), Summary.TotalMaxHealth, 2000.0f);
	TestTrue(TEXT("Survivors stay at 100 percent after five deaths"), FMath::IsNearlyEqual(Summary.GetHealthFraction(), 1.0f));

	States[5].Health = 50u;
	Summary = BuildGuLiCommanderUnitTypeSummary(Selection, FGuLiCommandAck(), States, true);
	TestEqual(TEXT("An injury does not remove a living soldier"), Summary.AliveCount, 20);
	TestEqual(TEXT("Survivor injury changes health"), Summary.TotalHealth, 1950.0f);
	TestTrue(TEXT("Bar reflects real damage"), FMath::IsNearlyEqual(Summary.GetHealthFraction(), 0.975f));

	// Life-state and zero-health exclusions both matter even if the other field is delayed.
	States[0].Health = 100u;
	States[5].Health = 0u;
	Summary = BuildGuLiCommanderUnitTypeSummary(Selection, FGuLiCommandAck(), States, true);
	TestEqual(TEXT("Destroyed and zero-health states are both excluded"), Summary.AliveCount, 19);
	TestEqual(TEXT("Dead maximum health never dilutes the survivor bar"), Summary.TotalMaxHealth, 1900.0f);
	TestTrue(TEXT("Remaining full-health survivors still have a full bar"), FMath::IsNearlyEqual(Summary.GetHealthFraction(), 1.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderHUDUnitTypeDeduplicationTest,
	"GuLiStrike.Commander.UI.HUD.UnitType.DeduplicationAndWeightedHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderHUDUnitTypeDeduplicationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderHUDWidgetTests;
	FGuLiCommanderSelectionState Selection;
	Selection.Cohorts.Add(MakeCohort(147u, {FGuLiSoldierId(1u), FGuLiSoldierId(1u)}));
	Selection.Cohorts.Add(MakeCohort(148u, {FGuLiSoldierId(1u), FGuLiSoldierId(2u), FGuLiSoldierId()}));
	TArray<FGuLiSoldierStateItem> States = {
		MakeSoldier(1u, 50u, 100u),
		MakeSoldier(2u, 200u, 200u),
		MakeSoldier(3u, 1u, 200u), // An unselected soldier must not affect the summary.
		MakeSoldier(1u, 50u, 100u) // Repeated identity must not inflate the population.
	};
	FGuLiCommanderUnitTypeSummary Summary = BuildGuLiCommanderUnitTypeSummary(
		Selection, FGuLiCommandAck(), States, true);
	TestTrue(TEXT("Many control cohorts produce one visible unit-type summary"), Summary.IsVisible());
	TestEqual(TEXT("Selection IDs are unique across and within cohorts"), Summary.SelectedCount, 2);
	TestEqual(TEXT("Reliable snapshot duplicate is not counted twice"), Summary.AliveCount, 2);
	TestEqual(TEXT("Current health sum"), Summary.TotalHealth, 250.0f);
	TestEqual(TEXT("Maximum health sum"), Summary.TotalMaxHealth, 300.0f);
	TestTrue(TEXT("Different maxima are weighted by total health, not mean percentages"),
		FMath::IsNearlyEqual(Summary.GetHealthFraction(), 250.0f / 300.0f));

	for (uint32 Id = 4u; Id <= 10u; ++Id)
	{
		Selection.Cohorts.Add(MakeCohort(150u + Id, {FGuLiSoldierId(Id)}));
		States.Add(MakeSoldier(Id));
	}
	Summary = BuildGuLiCommanderUnitTypeSummary(Selection, FGuLiCommandAck(), States, true);
	TestEqual(TEXT("Selection beyond the former six-card limit is not truncated"), Summary.AliveCount, 9);
	TestEqual(TEXT("All groups share the same health aggregate"), Summary.TotalHealth, 950.0f);
	TestEqual(TEXT("All groups share the same maximum-health aggregate"), Summary.TotalMaxHealth, 1000.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderHUDUnitTypeAvailabilityTest,
	"GuLiStrike.Commander.UI.HUD.UnitType.AvailabilityAndReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderHUDUnitTypeAvailabilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderHUDWidgetTests;
	FGuLiCommanderSelectionState Selection;
	Selection.Cohorts.Add(MakeCohort(1u, {FGuLiSoldierId(1u), FGuLiSoldierId(2u)}));
	TArray<FGuLiSoldierStateItem> States = {MakeSoldier(1u)};
	FGuLiCommanderUnitTypeSummary Summary = BuildGuLiCommanderUnitTypeSummary(
		Selection, FGuLiCommandAck(), States, true);
	TestTrue(TEXT("Missing selected state keeps a syncing card visible"), Summary.IsVisible() && Summary.bSyncing);
	TestEqual(TEXT("Missing data is explained"), Summary.CommandStatus.ToString(), FString(TEXT("同步中")));
	TestTrue(TEXT("Partial full-health data must not paint a full bar"), FMath::IsNearlyZero(Summary.GetHealthFraction()));

	States.Add(MakeSoldier(2u));
	Summary = BuildGuLiCommanderUnitTypeSummary(Selection, FGuLiCommandAck(), States, false);
	TestTrue(TEXT("Bootstrap or match mismatch suppresses existing facts"), Summary.bSyncing);
	TestEqual(TEXT("Unready snapshot has no displayed population facts"), Summary.AliveCount, 0);
	TestEqual(TEXT("Unready snapshot has no stale health sum"), Summary.TotalHealth, 0.0f);
	TestTrue(TEXT("Unready snapshot has no false full bar"), FMath::IsNearlyZero(Summary.GetHealthFraction()));

	Summary = BuildGuLiCommanderUnitTypeSummary(Selection, FGuLiCommandAck(), States, true);
	TestFalse(TEXT("Completion of reliable data exits syncing"), Summary.bSyncing);
	TestEqual(TEXT("Recovered population"), Summary.AliveCount, 2);
	States[0].Health = 0u;
	States[1].LifeState = EGuLiSoldierLifeState::Destroyed;
	Summary = BuildGuLiCommanderUnitTypeSummary(Selection, FGuLiCommandAck(), States, true);
	TestFalse(TEXT("All dead selection collapses its panel"), Summary.IsVisible());
	TestEqual(TEXT("All dead population is empty"), Summary.AliveCount, 0);
	TestEqual(TEXT("All dead maximum health is cleared"), Summary.TotalMaxHealth, 0.0f);
	TestTrue(TEXT("All dead status does not linger"), Summary.CommandStatus.IsEmpty());

	Summary = BuildGuLiCommanderUnitTypeSummary(FGuLiCommanderSelectionState(), FGuLiCommandAck(), States, false);
	TestFalse(TEXT("Cleared selection stays collapsed even while syncing"), Summary.IsVisible());
	TestFalse(TEXT("Empty selection has no spurious syncing state"), Summary.bSyncing);
	TestEqual(TEXT("Clear removes selected IDs"), Summary.SelectedCount, 0);

	Selection.Cohorts = {MakeCohort(2u, {FGuLiSoldierId(3u)})};
	States.Add(MakeSoldier(3u, 30u, 150u));
	Summary = BuildGuLiCommanderUnitTypeSummary(Selection, FGuLiCommandAck(), States, true);
	TestTrue(TEXT("Reselection restores the panel"), Summary.IsVisible());
	TestEqual(TEXT("Reselection only includes new members"), Summary.AliveCount, 1);
	TestTrue(TEXT("Reselection uses fresh health, without the earlier total"),
		FMath::IsNearlyEqual(Summary.GetHealthFraction(), 0.2f));

	States.Last().MaxHealth = 0u;
	Summary = BuildGuLiCommanderUnitTypeSummary(Selection, FGuLiCommandAck(), States, true);
	TestTrue(TEXT("Incomplete maximum health is not invented"), Summary.bSyncing);
	TestTrue(TEXT("Missing maximum cannot create an invalid fill"), FMath::IsNearlyZero(Summary.GetHealthFraction()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderHUDUnitTypeCommandFeedbackTest,
	"GuLiStrike.Commander.UI.HUD.UnitType.CommandFeedback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderHUDUnitTypeCommandFeedbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderHUDWidgetTests;
	FGuLiCommanderSelectionState Selection;
	Selection.SelectionRevision = 7u;
	Selection.Cohorts = {
		MakeCohort(10u, {FGuLiSoldierId(1u), FGuLiSoldierId(2u)}),
		MakeCohort(20u, {FGuLiSoldierId(3u), FGuLiSoldierId(4u)}),
		MakeCohort(30u, {FGuLiSoldierId(5u)})
	};
	TArray<FGuLiSoldierStateItem> States = {
		MakeSoldier(1u), MakeSoldier(2u, 0u), MakeSoldier(3u), MakeSoldier(4u, 0u), MakeSoldier(5u)
	};
	States[0].ActiveOrderId = 51u;
	FGuLiCommandAck Ack;
	Ack.CommandKind = EGuLiCommandKind::Move;
	Ack.ServerSelectionRevision = Selection.SelectionRevision;
	Ack.Result = EGuLiCommandAckResult::PartiallyAccepted;
	Ack.CohortResults = {
		MakeCohortAck(10u, EGuLiCommandAckResult::Accepted),
		MakeCohortAck(20u, EGuLiCommandAckResult::PathFailed),
		MakeCohortAck(30u, EGuLiCommandAckResult::Accepted)
	};
	FGuLiCommanderUnitTypeSummary Summary = BuildGuLiCommanderUnitTypeSummary(Selection, Ack, States, true);
	TestEqual(TEXT("Partial acceptance retains success and the failure reason for living soldiers"),
		Summary.CommandStatus.ToString(), FString(TEXT("执行中 1 · 已接收 1 · 寻路失败 1")));
	TestEqual(TEXT("Command status does not restore dead members"), Summary.AliveCount, 3);

	Ack.CohortResults.RemoveAt(2);
	Summary = BuildGuLiCommanderUnitTypeSummary(Selection, Ack, States, true);
	TestEqual(TEXT("Partial ACK without a cohort detail must not become unconditional success"),
		Summary.CommandStatus.ToString(), FString(TEXT("执行中 1 · 部分接收 1 · 寻路失败 1")));

	Ack.ServerSelectionRevision = Selection.SelectionRevision - 1u;
	Summary = BuildGuLiCommanderUnitTypeSummary(Selection, Ack, States, true);
	TestEqual(TEXT("Stale ACK is ignored while mixed current execution stays visible"),
		Summary.CommandStatus.ToString(), FString(TEXT("执行中 1 · 待命 2")));
	Ack.ServerSelectionRevision = Selection.SelectionRevision;
	Ack.CommandKind = EGuLiCommandKind::Selection;
	Summary = BuildGuLiCommanderUnitTypeSummary(Selection, Ack, States, true);
	TestEqual(TEXT("Selection ACK must not overwrite move feedback with a failure"),
		Summary.CommandStatus.ToString(), FString(TEXT("执行中 1 · 待命 2")));

	Ack.CommandKind = EGuLiCommandKind::Move;
	Selection.Cohorts = {MakeCohort(20u, {FGuLiSoldierId(3u), FGuLiSoldierId(4u)})};
	Summary = BuildGuLiCommanderUnitTypeSummary(Selection, Ack, States, true);
	TestEqual(TEXT("Uniform failure retains the specific reason"),
		Summary.CommandStatus.ToString(), FString(TEXT("寻路失败")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderShortcutTooltipPlacementTest,
	"GuLiStrike.Commander.UI.HUD.Shortcuts.ViewportPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderShortcutTooltipPlacementTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// Exercise both viewport aspect ratios and logical coordinate sizes caused by DPI.
	const FVector2D Viewports[] = {
		FVector2D(1280, 720), FVector2D(1920, 1080), FVector2D(2560, 1080),
		FVector2D(1280 / 1.5, 720 / 1.5), FVector2D(1920 / 0.666, 1080 / 0.666)
	};
	for (const FVector2D& View : Viewports)
	{
		const FVector2D Anchors[] = {
			FVector2D(2, 2), FVector2D(View.X - 46, 2),
			FVector2D(2, View.Y - 46), FVector2D(View.X - 46, View.Y - 46),
			FVector2D(View.X * 0.75, View.Y - 230)
		};
		for (const FVector2D& Anchor : Anchors)
		{
			const FBox2D Bounds = GuLiCommanderHUDLayout::PlaceTooltip(
				FBox2D(Anchor, Anchor + FVector2D(44, 44)), FVector2D(368, 192), View);
			TestTrue(TEXT("Tooltip remains fully inside the game viewport at every edge and DPI"),
				Bounds.Min.X >= 0 && Bounds.Min.Y >= 0 && Bounds.Max.X <= View.X && Bounds.Max.Y <= View.Y);
			TestEqual(TEXT("A normal game viewport retains readable tooltip size"), Bounds.GetSize(), FVector2D(368, 192));
		}
	}
	const FBox2D SmallBounds = GuLiCommanderHUDLayout::PlaceTooltip(
		FBox2D(FVector2D(50, 50), FVector2D(94, 94)), FVector2D(368, 192), FVector2D(160, 90));
	TestTrue(TEXT("An exceptionally small viewport clips the tooltip area safely"),
		SmallBounds.Min.X >= 0 && SmallBounds.Min.Y >= 0 && SmallBounds.Max.X <= 160 && SmallBounds.Max.Y <= 90);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderHUDFractionalHealthTest,
	"GuLiStrike.Commander.UI.HUD.UnitType.FractionalHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderHUDFractionalHealthTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace GuLiCommanderHUDWidgetTests;
	FGuLiCommanderSelectionState Selection;
	Selection.Cohorts = {MakeCohort(1u, {FGuLiSoldierId(1u), FGuLiSoldierId(2u), FGuLiSoldierId(3u)})};
	TArray<FGuLiSoldierStateItem> States = {
		MakeSoldier(1u, 125.125f, 300.5f),
		MakeSoldier(2u, 0.25f, 0.5f),
		MakeSoldier(3u, 0.0f, 900.75f)
	};
	FGuLiCommanderUnitTypeSummary Summary = BuildGuLiCommanderUnitTypeSummary(
		Selection, FGuLiCommandAck(), States, true);
	TestEqual(TEXT("Fractional living health is not rounded in the aggregate"), Summary.TotalHealth, 125.375f);
	TestEqual(TEXT("Only surviving maxima contribute, including maxima above 255"), Summary.TotalMaxHealth, 301.0f);
	TestEqual(TEXT("A fractional survivor is still counted"), Summary.AliveCount, 2);
	TestTrue(TEXT("The aggregate uses health weighting rather than averaging per-unit fractions"),
		FMath::IsNearlyEqual(Summary.GetHealthFraction(), 125.375f / 301.0f));

	States[0].MaxHealth = std::numeric_limits<float>::quiet_NaN();
	Summary = BuildGuLiCommanderUnitTypeSummary(Selection, FGuLiCommandAck(), States, true);
	TestTrue(TEXT("Malformed health facts show syncing rather than a misleading aggregate"), Summary.bSyncing);
	TestEqual(TEXT("NaN never reaches the rendered fill"), Summary.GetHealthFraction(), 0.0f);
	TestTrue(TEXT("NaN never contaminates the display sum"), FMath::IsFinite(Summary.TotalMaxHealth));
	return true;
}

#endif
