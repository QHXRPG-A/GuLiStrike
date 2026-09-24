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
		TEXT("Escape never clears an admitted selection or task queue"),
		GuLiCommanderToolPolicy::ResolveCancelAction(EGuLiCommanderToolMode::Select)
			== GuLiCommanderToolPolicy::ECancelAction::CancelMove);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderMoveAckRoutingTest,
	"GuLiStrike.Commander.Framework.ToolMode.MoveAckRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderMoveAckRoutingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using GuLiCommanderToolPolicy::ResolveMoveAckRouting;

	const auto OldAckBeforeReplacement = ResolveMoveAckRouting(
		EGuLiCommandKind::Move,
		90u,
		92u,
		90u,
		0u);
	TestTrue(TEXT("The first ACK always resolves its own prediction"),
		OldAckBeforeReplacement.bResolvePrediction);
	TestFalse(TEXT("The first ACK cannot replace feedback for the latest click"),
		OldAckBeforeReplacement.bUpdateCommandLine);
	TestTrue(TEXT("Synchronous handling clears the first dispatched command"),
		OldAckBeforeReplacement.bClearDispatchedCommand);

	const auto OldAckAfterReplacement = ResolveMoveAckRouting(
		EGuLiCommandKind::Move,
		90u,
		92u,
		92u,
		0u);
	TestTrue(TEXT("FIFO fallback still resolves an older ACK by its own ID"),
		OldAckAfterReplacement.bResolvePrediction);
	TestFalse(TEXT("An older ACK cannot clear the newer dispatched command"),
		OldAckAfterReplacement.bClearDispatchedCommand);

	const auto LatestAck = ResolveMoveAckRouting(
		EGuLiCommandKind::Move,
		92u,
		92u,
		92u,
		90u);
	TestTrue(TEXT("The latest ACK resolves prediction"), LatestAck.bResolvePrediction);
	TestTrue(TEXT("The latest ACK updates command-line feedback"), LatestAck.bUpdateCommandLine);
	TestTrue(TEXT("The latest ACK clears its dispatched command"), LatestAck.bClearDispatchedCommand);

	const auto SelectionAck = ResolveMoveAckRouting(
		EGuLiCommandKind::Selection,
		92u,
		92u,
		92u,
		0u);
	TestFalse(TEXT("Selection ACKs never enter move prediction routing"),
		SelectionAck.bResolvePrediction);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
