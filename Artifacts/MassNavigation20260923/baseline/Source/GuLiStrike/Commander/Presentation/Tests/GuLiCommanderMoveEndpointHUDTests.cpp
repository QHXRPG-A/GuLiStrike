// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Commander/Presentation/GuLiCommanderHUD.h"

#include "Misc/AutomationTest.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderMoveEndpointHUDPolicyTest,
	"GuLiStrike.Commander.Presentation.MoveEndpointHUDPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderMoveEndpointHUDPolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGuLiMoveEndpointItem Endpoint;
	Endpoint.SoldierId = FGuLiSoldierId(17u);
	Endpoint.ActiveOrderId = 41u;
	Endpoint.CommandStart = FVector(100.0, 200.0, 300.0);
	Endpoint.FinalDestination = FVector(400.0, 500.0, 600.0);
	Endpoint.Revision = 1u;

	FGuLiSoldierStateItem Soldier;
	Soldier.SoldierId = Endpoint.SoldierId;
	Soldier.Team = EGuLiTeam::Red;
	Soldier.Health = 100.0f;
	Soldier.MaxHealth = 100.0f;
	Soldier.LifeState = EGuLiSoldierLifeState::Alive;
	Soldier.ActiveOrderId = Endpoint.ActiveOrderId;

	TestTrue(TEXT("Selected live Soldier executing the endpoint order draws a route"),
		GuLiCommanderHUD::ShouldDrawMoveEndpoint(Endpoint, true, &Soldier));
	TestFalse(TEXT("Deselection hides the route immediately"),
		GuLiCommanderHUD::ShouldDrawMoveEndpoint(Endpoint, false, &Soldier));
	TestFalse(TEXT("A missing reliable Soldier state cannot leave a stale route"),
		GuLiCommanderHUD::ShouldDrawMoveEndpoint(Endpoint, true, nullptr));

	Soldier.ActiveOrderId = 0u;
	TestFalse(TEXT("Arrival or Blocked state clears ActiveOrderId and hides the route"),
		GuLiCommanderHUD::ShouldDrawMoveEndpoint(Endpoint, true, &Soldier));
	Soldier.ActiveOrderId = Endpoint.ActiveOrderId + 1u;
	TestFalse(TEXT("A replacement order hides the superseded route"),
		GuLiCommanderHUD::ShouldDrawMoveEndpoint(Endpoint, true, &Soldier));
	Soldier.ActiveOrderId = Endpoint.ActiveOrderId;
	Soldier.Health = 0.0f;
	TestFalse(TEXT("Death hides the route even if an old order id is still present"),
		GuLiCommanderHUD::ShouldDrawMoveEndpoint(Endpoint, true, &Soldier));
	Soldier.Health = 100.0f;
	Soldier.SoldierId = FGuLiSoldierId(18u);
	TestFalse(TEXT("A mismatched roster identity cannot authorize an endpoint"),
		GuLiCommanderHUD::ShouldDrawMoveEndpoint(Endpoint, true, &Soldier));
	Soldier.SoldierId = Endpoint.SoldierId;
	Endpoint.Revision = 0u;
	TestFalse(TEXT("An invalid or reset endpoint is never drawn"),
		GuLiCommanderHUD::ShouldDrawMoveEndpoint(Endpoint, true, &Soldier));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiCommanderMoveEndpointScreenClipTest,
	"GuLiStrike.Commander.Presentation.MoveEndpointScreenClip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiCommanderMoveEndpointScreenClipTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBox2D Bounds(FVector2D::ZeroVector, FVector2D(100.0, 100.0));

	FVector2D Start(-50.0, 50.0);
	FVector2D End(150.0, 50.0);
	TestTrue(TEXT("A line crossing the viewport is retained"),
		GuLiCommanderHUD::ClipScreenLineToBounds(Bounds, Start, End));
	TestTrue(TEXT("Crossing start is clipped to the left edge"),
		Start.Equals(FVector2D(0.0, 50.0), 0.001));
	TestTrue(TEXT("Crossing end is clipped to the right edge"),
		End.Equals(FVector2D(100.0, 50.0), 0.001));

	Start = FVector2D(-20.0, -20.0);
	End = FVector2D(120.0, 120.0);
	TestTrue(TEXT("A diagonal through two corners is retained"),
		GuLiCommanderHUD::ClipScreenLineToBounds(Bounds, Start, End));
	TestTrue(TEXT("Diagonal entry is clipped exactly"),
		Start.Equals(FVector2D::ZeroVector, 0.001));
	TestTrue(TEXT("Diagonal exit is clipped exactly"),
		End.Equals(FVector2D(100.0, 100.0), 0.001));

	Start = FVector2D(-50.0, -10.0);
	End = FVector2D(150.0, -10.0);
	TestFalse(TEXT("A parallel line wholly outside the viewport is culled"),
		GuLiCommanderHUD::ClipScreenLineToBounds(Bounds, Start, End));

	Start = FVector2D(10.0, 20.0);
	End = FVector2D(30.0, 40.0);
	TestTrue(TEXT("A line already inside the viewport is retained"),
		GuLiCommanderHUD::ClipScreenLineToBounds(Bounds, Start, End));
	TestTrue(TEXT("An inside line is not moved"),
		Start.Equals(FVector2D(10.0, 20.0), 0.001)
			&& End.Equals(FVector2D(30.0, 40.0), 0.001));

	Start = FVector2D(10.0, 20.0);
	End = FVector2D(std::numeric_limits<double>::quiet_NaN(), 40.0);
	TestFalse(TEXT("Non-finite projected coordinates are culled"),
		GuLiCommanderHUD::ClipScreenLineToBounds(Bounds, Start, End));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
