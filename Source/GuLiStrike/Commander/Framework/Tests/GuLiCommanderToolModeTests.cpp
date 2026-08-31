// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderToolDefaultAndArmTest,
	"GuLiStrike.Commander.Framework.ToolMode.DefaultAndArm",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderToolDefaultAndArmTest::RunTest(const FString& Parameters)
{
	EGuLiCommanderToolMode ToolMode = GuLiCommanderToolPolicy::DefaultToolMode;
	TestTrue(
		TEXT("Commander starts in Select mode"),
		ToolMode == EGuLiCommanderToolMode::Select);

	const bool bArmedWithoutSelection = GuLiCommanderToolPolicy::CanArmMove(
		true,
		true,
		false);
	if (bArmedWithoutSelection)
	{
		ToolMode = EGuLiCommanderToolMode::Move;
	}
	TestFalse(TEXT("Move cannot be armed without a confirmed selection"), bArmedWithoutSelection);
	TestTrue(
		TEXT("A rejected arm request leaves Select active"),
		ToolMode == EGuLiCommanderToolMode::Select);

	TestFalse(
		TEXT("Move cannot be armed before commander synchronization"),
		GuLiCommanderToolPolicy::CanArmMove(false, true, true));
	TestFalse(
		TEXT("Move cannot be armed without the network component"),
		GuLiCommanderToolPolicy::CanArmMove(true, false, true));
	TestTrue(
		TEXT("Move can be armed only when every prerequisite is ready"),
		GuLiCommanderToolPolicy::CanArmMove(true, true, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderSelectionRadiusStepTest,
	"GuLiStrike.Commander.Framework.ToolMode.SelectionRadiusStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderSelectionRadiusStepTest::RunTest(const FString& Parameters)
{
	EGuLiSelectionRadiusPreset Preset = EGuLiSelectionRadiusPreset::Small;
	Preset = GuLiCommanderToolPolicy::ResolveRadiusStep(
		EGuLiCommanderToolMode::Select,
		Preset);
	TestTrue(TEXT("Select: Small advances to Medium"), Preset == EGuLiSelectionRadiusPreset::Medium);

	Preset = GuLiCommanderToolPolicy::ResolveRadiusStep(
		EGuLiCommanderToolMode::Select,
		Preset);
	TestTrue(TEXT("Select: Medium advances to Large"), Preset == EGuLiSelectionRadiusPreset::Large);

	Preset = GuLiCommanderToolPolicy::ResolveRadiusStep(
		EGuLiCommanderToolMode::Select,
		Preset);
	TestTrue(TEXT("Select: Large wraps to Small"), Preset == EGuLiSelectionRadiusPreset::Small);

	const EGuLiSelectionRadiusPreset MovePreset = GuLiCommanderToolPolicy::ResolveRadiusStep(
		EGuLiCommanderToolMode::Move,
		EGuLiSelectionRadiusPreset::Medium);
	TestTrue(
		TEXT("Move mode ignores selection-radius step input"),
		MovePreset == EGuLiSelectionRadiusPreset::Medium);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderCancelAndMoveCompletionTest,
	"GuLiStrike.Commander.Framework.ToolMode.CancelAndMoveCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderCancelAndMoveCompletionTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("Escape cancels Move before touching the selection"),
		GuLiCommanderToolPolicy::ResolveCancelAction(EGuLiCommanderToolMode::Move)
			== GuLiCommanderToolPolicy::ECancelAction::CancelMove);
	TestTrue(
		TEXT("Escape clears selection when Select is already active"),
		GuLiCommanderToolPolicy::ResolveCancelAction(EGuLiCommanderToolMode::Select)
			== GuLiCommanderToolPolicy::ECancelAction::ClearSelection);

	TestTrue(
		TEXT("A submitted one-shot move returns to Select"),
		GuLiCommanderToolPolicy::ResolveModeAfterMoveAttempt(
			EGuLiCommanderToolMode::Move,
			true) == EGuLiCommanderToolMode::Select);
	TestTrue(
		TEXT("An invalid move click keeps Move armed"),
		GuLiCommanderToolPolicy::ResolveModeAfterMoveAttempt(
			EGuLiCommanderToolMode::Move,
			false) == EGuLiCommanderToolMode::Move);
	TestTrue(
		TEXT("Move remains armed while its confirmed selection is still available"),
		GuLiCommanderToolPolicy::ResolveModeForSelectionAvailability(
			EGuLiCommanderToolMode::Move,
			true) == EGuLiCommanderToolMode::Move);
	TestTrue(
		TEXT("Losing the confirmed selection cancels Move back to Select"),
		GuLiCommanderToolPolicy::ResolveModeForSelectionAvailability(
			EGuLiCommanderToolMode::Move,
			false) == EGuLiCommanderToolMode::Select);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderWorldIntentBlockingTest,
	"GuLiStrike.Commander.Framework.ToolMode.WorldIntentBlocking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderWorldIntentBlockingTest::RunTest(const FString& Parameters)
{
	TestFalse(
		TEXT("HUD geometry blocks world selection and command submission"),
		GuLiCommanderToolPolicy::AllowsWorldIntent(true));
	TestTrue(
		TEXT("World intent may continue outside HUD geometry"),
		GuLiCommanderToolPolicy::AllowsWorldIntent(false));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
