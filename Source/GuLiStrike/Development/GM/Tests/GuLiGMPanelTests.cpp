// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Development/GM/GuLiGMPanelModel.h"
#include "Gameplay/Skills/GuLiSkillGMUtilities.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiGMPanelPaginationFilterTest,
	"GuLiStrike.GM.Panel.PaginationAndFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiGMPanelPaginationFilterTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestEqual(TEXT("Empty lists still expose one stable page"), GuLiGMPanel::GetPageCount(0), 1);
	TestEqual(TEXT("Ten rows fit one page"), GuLiGMPanel::GetPageCount(10), 1);
	TestEqual(TEXT("Eleven rows require two pages"), GuLiGMPanel::GetPageCount(11), 2);
	TestEqual(TEXT("Twenty-three rows require three pages"), GuLiGMPanel::GetPageCount(23), 3);
	TestEqual(TEXT("Requested page clamps after a filter shrinks the result"), GuLiGMPanel::ClampPageIndex(2, 3), 0);
	TestEqual(TEXT("Negative page clamps to the first page"), GuLiGMPanel::ClampPageIndex(-9, 23), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiGMPanelRightLayoutTest,
	"GuLiStrike.GM.Panel.RightLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiGMPanelRightLayoutTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (const FVector2D Viewport : { FVector2D(1280.0f, 720.0f), FVector2D(1920.0f, 1080.0f), FVector2D(3440.0f, 1440.0f), FVector2D(240.0f, 160.0f) })
	{
		const GuLiGMPanel::FPanelLayout Layout = GuLiGMPanel::CalculatePanelLayout(Viewport);
		TestTrue(TEXT("Panel width is positive"), Layout.Width > 0.0f);
		TestTrue(TEXT("Panel height is positive"), Layout.Height > 0.0f);
		TestTrue(TEXT("Panel leaves horizontal safe space"), Layout.Width <= FMath::Max(1.0f, Viewport.X - 2.0f * Layout.RightMargin));
		TestTrue(TEXT("Panel leaves vertical safe space"), Layout.Height <= FMath::Max(1.0f, Viewport.Y - 2.0f * Layout.RightMargin));
		TestTrue(TEXT("Panel never becomes a full-screen visual surface"), Layout.Width < Viewport.X && Layout.Height < Viewport.Y);
	}
	const GuLiGMPanel::FPanelLayout FullHd = GuLiGMPanel::CalculatePanelLayout(FVector2D(1920.0f, 1080.0f));
	TestTrue(TEXT("Default width remains close to 42 percent"), FMath::IsNearlyEqual(FullHd.Width / 1920.0f, 0.42f, 0.01f));
	TestTrue(TEXT("Default height remains close to 84 percent"), FMath::IsNearlyEqual(FullHd.Height / 1080.0f, 0.84f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiGMPanelPermissionMatrixTest,
	"GuLiStrike.GM.Panel.PermissionMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiGMPanelPermissionMatrixTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const GuLiGMPanel::FAccessPolicy Standalone = GuLiGMPanel::ResolveAccessPolicy(NM_Standalone);
	TestTrue(TEXT("Standalone can mutate authority state"), Standalone.bCanMutateAuthorityState);
	TestTrue(TEXT("Standalone can read registries and diagnostics"), Standalone.bCanReadRuntimeRegistry && Standalone.bCanReadSkillSources && Standalone.bCanReadAuthorityDiagnostics);

	const GuLiGMPanel::FAccessPolicy Listen = GuLiGMPanel::ResolveAccessPolicy(NM_ListenServer);
	TestTrue(TEXT("Listen host can mutate authority state"), Listen.bCanMutateAuthorityState);

	const GuLiGMPanel::FAccessPolicy Client = GuLiGMPanel::ResolveAccessPolicy(NM_Client);
	TestFalse(TEXT("Client never receives a new authority mutation path"), Client.bCanMutateAuthorityState);
	TestFalse(TEXT("Client cannot read unreplicated registry/source/navigation state"), Client.bCanReadRuntimeRegistry || Client.bCanReadSkillSources || Client.bCanReadAuthorityDiagnostics);
	TestTrue(TEXT("Client can read replicated committed profiles"), Client.bCanReadSkillProfiles);
	TestTrue(TEXT("Client keeps explicitly local tools"), Client.bCanRunLocalBenchmark && Client.bCanToggleLocalCameraDebug);
	TestTrue(TEXT("Client denial is explicit"), Client.AuthorityUnavailableReason.Contains(TEXT("不发送 GM RPC")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiGMPanelConfirmationTest,
	"GuLiStrike.GM.Panel.Confirmation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiGMPanelConfirmationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	GuLiGMPanel::FConfirmationGate Gate;
	TestFalse(TEXT("First destructive click arms instead of executing"), Gate.ConsumeOrArm(GuLiGMPanel::EConfirmationAction::ResetAllRuntime, 10.0));
	TestTrue(TEXT("The same action confirms within five seconds"), Gate.ConsumeOrArm(GuLiGMPanel::EConfirmationAction::ResetAllRuntime, 14.9));
	TestFalse(TEXT("Confirmed action consumes its arm"), Gate.IsArmed(GuLiGMPanel::EConfirmationAction::ResetAllRuntime, 15.0));
	TestFalse(TEXT("A different destructive action replaces the arm"), Gate.ConsumeOrArm(GuLiGMPanel::EConfirmationAction::BenchmarkTenThousand, 20.0));
	TestFalse(TEXT("Expired arm never confirms"), Gate.ConsumeOrArm(GuLiGMPanel::EConfirmationAction::BenchmarkTenThousand, 25.1));
	Gate.Cancel();
	TestFalse(TEXT("Parameter or page changes cancel confirmation"), Gate.IsArmed(GuLiGMPanel::EConfirmationAction::BenchmarkTenThousand, 25.2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiGMPanelInputLifecycleTest,
	"GuLiStrike.GM.Panel.InputLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiGMPanelInputLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestEqual(TEXT("A valid local closed panel opens"), GuLiGMPanel::ResolveToggleAction(true, true, false), GuLiGMPanel::EToggleAction::Open);
	TestEqual(TEXT("A second toggle closes"), GuLiGMPanel::ResolveToggleAction(true, true, true), GuLiGMPanel::EToggleAction::Close);
	TestEqual(TEXT("An open panel can close while its World is tearing down"), GuLiGMPanel::ResolveToggleAction(false, false, true), GuLiGMPanel::EToggleAction::Close);
	TestEqual(TEXT("A remote controller cannot open a panel"), GuLiGMPanel::ResolveToggleAction(false, true, false), GuLiGMPanel::EToggleAction::None);

	const GuLiGMPanel::FInputRestorePolicy Commander = GuLiGMPanel::ResolveInputRestorePolicy(true);
	TestTrue(TEXT("Commander returns to Game+UI and cursor interaction"), Commander.bGameAndUI && Commander.bShowCursor && Commander.bEnableClickEvents && Commander.bEnableMouseOverEvents);
	const GuLiGMPanel::FInputRestorePolicy Pawn = GuLiGMPanel::ResolveInputRestorePolicy(false);
	TestFalse(TEXT("Ground and air roles return to GameOnly without cursor interaction"), Pawn.bGameAndUI || Pawn.bShowCursor || Pawn.bEnableClickEvents || Pawn.bEnableMouseOverEvents);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiGMPanelSourceIdAndInvalidInputTest,
	"GuLiStrike.GM.Panel.SourceIdAndInvalidInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiGMPanelSourceIdAndInvalidInputTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FGuid RedA = GuLiSkillGM::MakeSourceId(EGuLiTeam::Red, TEXT("PanelSource"));
	const FGuid RedB = GuLiSkillGM::MakeSourceId(EGuLiTeam::Red, TEXT("PanelSource"));
	const FGuid Blue = GuLiSkillGM::MakeSourceId(EGuLiTeam::Blue, TEXT("PanelSource"));
	const FGuid OtherLabel = GuLiSkillGM::MakeSourceId(EGuLiTeam::Red, TEXT("OtherSource"));
	TestTrue(TEXT("Team+Label SourceId is stable"), RedA == RedB);
	TestTrue(TEXT("Team is part of SourceId"), RedA != Blue);
	TestTrue(TEXT("Label is part of SourceId"), RedA != OtherLabel);

	GuLiGMPanel::FModel Model;
	GuLiGMPanel::FSkillNumericRequest Invalid;
	Invalid.Team = EGuLiTeam::Red;
	Invalid.UnitTypeId = TEXT("1");
	Invalid.SlotId = TEXT("");
	Invalid.Value = TEXT("1");
	TestFalse(TEXT("Empty slot is rejected before dispatch"), Model.SetSkillNumericOverride(Invalid).bSuccess);
	Invalid.SlotId = TEXT("BasicAttack");
	Invalid.Value = TEXT("nan");
	TestFalse(TEXT("NaN is rejected before dispatch"), Model.SetSkillNumericOverride(Invalid).bSuccess);
	Invalid.Value = TEXT("1");
	Invalid.Team = EGuLiTeam::Unassigned;
	TestFalse(TEXT("Invalid team is rejected before dispatch"), Model.SetSkillNumericOverride(Invalid).bSuccess);
	return true;
}

#endif
