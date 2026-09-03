// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Combat/GuLiMissileVisualSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

namespace GuLiMissileVisualTests
{
	FGuLiTargetHandle MakeTarget(const uint32 Id)
	{
		FGuLiTargetHandle Target;
		Target.Kind = EGuLiTargetKind::Ship;
		Target.AuthorityId = FGuid(0u, 0u, 0u, Id);
		Target.Generation = 1u;
		return Target;
	}

	FGuLiWingmanHandle MakeEmitter()
	{
		FGuLiWingmanHandle Emitter;
		Emitter.Flight.Group.ShipInstanceId = FGuid(1u, 2u, 3u, 4u);
		Emitter.Flight.Group.ShipGeneration = 1u;
		Emitter.Flight.Group.GroupGeneration = 1u;
		Emitter.Flight.FlightIndex = 0u;
		Emitter.MemberIndex = 0u;
		Emitter.EntityGeneration = 1u;
		return Emitter;
	}

	struct FFixture
	{
		UWorld* World = nullptr;
		UGuLiMissileVisualSubsystem* Visuals = nullptr;
		bool bWorldContextRegistered = false;

		~FFixture()
		{
			if (!World) return;
			World->DestroyWorld(false);
			if (bWorldContextRegistered && GEngine) GEngine->DestroyWorldContext(World);
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Engine exists for missile visual test"), GEngine)) return false;
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("Client-capable game World exists"), World)) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bWorldContextRegistered = true;
			Visuals = World->GetSubsystem<UGuLiMissileVisualSubsystem>();
			return Test.TestNotNull(TEXT("Non-dedicated World creates visual state service"), Visuals);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiMissileVisualEventSequenceTest,
	"GuLiStrike.Combat.Missile.Visual.LaunchCorrectionTerminalSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiMissileVisualEventSequenceTest::RunTest(const FString& Parameters)
{
	using namespace GuLiMissileVisualTests;
	FFixture Fixture;
	if (!Fixture.Initialize(*this)) return false;
	TestFalse(TEXT("Visual reconstruction service is not a missile Actor"),
		UGuLiMissileVisualSubsystem::StaticClass()->IsChildOf(AActor::StaticClass()));
	Fixture.Visuals->BeginEpoch(77u);

	int32 LaunchEvents = 0;
	int32 CorrectionEvents = 0;
	int32 TerminalEvents = 0;
	Fixture.Visuals->OnVisualLaunchNative.AddLambda(
		[&LaunchEvents](const FGuLiMissileVisualLaunchDTO&) { ++LaunchEvents; });
	Fixture.Visuals->OnVisualCorrectionNative.AddLambda(
		[&CorrectionEvents](const FGuLiMissileVisualCorrectionDTO&) { ++CorrectionEvents; });
	Fixture.Visuals->OnVisualTerminalNative.AddLambda(
		[&TerminalEvents](const FGuLiMissileVisualTerminalDTO&) { ++TerminalEvents; });

	FGuLiMissileVisualLaunchDTO Launch;
	Launch.MatchEpoch = 77u;
	Launch.MissileId = FGuid(10u, 20u, 30u, 40u);
	Launch.Emitter = MakeEmitter();
	Launch.Target = MakeTarget(55u);
	Launch.Position = FVector(100.0, 200.0, 300.0);
	Launch.Velocity = FVector(1000.0, 0.0, 0.0);
	Launch.ServerWorldTimeSeconds = 1.0f;
	TestTrue(TEXT("Reliable launch creates one local visual record"), Fixture.Visuals->ApplyLaunch(Launch));
	TestFalse(TEXT("A duplicate launch is idempotently ignored"), Fixture.Visuals->ApplyLaunch(Launch));
	TestEqual(TEXT("Launch observers run once"), LaunchEvents, 1);

	FGuLiMissileVisualCorrectionDTO Correction;
	Correction.MatchEpoch = 77u;
	Correction.MissileId = Launch.MissileId;
	Correction.SimulationSequence = 6u;
	Correction.Position = FVector(200.0, 200.0, 300.0);
	Correction.Velocity = FVector(1000.0, 5.0, 0.0);
	Correction.ServerWorldTimeSeconds = 1.2f;
	TestTrue(TEXT("A newer unreliable correction advances visual state"),
		Fixture.Visuals->ApplyCorrection(Correction));
	FGuLiMissileVisualCorrectionDTO OlderCorrection = Correction;
	OlderCorrection.SimulationSequence = 5u;
	TestFalse(TEXT("A reordered older correction is ignored"),
		Fixture.Visuals->ApplyCorrection(OlderCorrection));
	TestEqual(TEXT("Correction observers run only for the monotonic sample"), CorrectionEvents, 1);

	FGuLiMissileVisualTerminalDTO Terminal;
	Terminal.MatchEpoch = 77u;
	Terminal.MissileId = Launch.MissileId;
	Terminal.SimulationSequence = 7u;
	Terminal.Reason = EGuLiLogicalMissileTerminalReason::Impact;
	Terminal.Location = FVector(300.0, 200.0, 300.0);
	Terminal.ServerWorldTimeSeconds = 1.3f;
	TestTrue(TEXT("Reliable terminal removes the local visual record"),
		Fixture.Visuals->ApplyTerminal(Terminal));
	TestEqual(TEXT("No active visual remains after terminal"), Fixture.Visuals->GetActiveVisualCount(), 0);
	Correction.SimulationSequence = 8u;
	TestFalse(TEXT("A late unreliable correction cannot resurrect a terminal missile"),
		Fixture.Visuals->ApplyCorrection(Correction));
	TestFalse(TEXT("A duplicate terminal is idempotently ignored"), Fixture.Visuals->ApplyTerminal(Terminal));
	TestEqual(TEXT("Terminal observers run once"), TerminalEvents, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiMissileVisualRpcContractTest,
	"GuLiStrike.Combat.Missile.Visual.GameStateRpcReliabilityContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiMissileVisualRpcContractTest::RunTest(const FString& Parameters)
{
	const UFunction* Launch = AGuLiBattleGameState::StaticClass()->FindFunctionByName(
		TEXT("MulticastReceiveMissileLaunch"));
	const UFunction* Correction = AGuLiBattleGameState::StaticClass()->FindFunctionByName(
		TEXT("MulticastReceiveMissileCorrection"));
	const UFunction* Terminal = AGuLiBattleGameState::StaticClass()->FindFunctionByName(
		TEXT("MulticastReceiveMissileTerminal"));
	TestNotNull(TEXT("Launch multicast exists"), Launch);
	TestNotNull(TEXT("Correction multicast exists"), Correction);
	TestNotNull(TEXT("Terminal multicast exists"), Terminal);
	if (!Launch || !Correction || !Terminal) return false;
	TestTrue(TEXT("Launch is a reliable multicast"),
		Launch->HasAnyFunctionFlags(FUNC_NetMulticast | FUNC_NetReliable)
		&& Launch->HasAllFunctionFlags(FUNC_NetMulticast | FUNC_NetReliable));
	TestTrue(TEXT("Correction is an unreliable multicast"),
		Correction->HasAnyFunctionFlags(FUNC_NetMulticast)
		&& !Correction->HasAnyFunctionFlags(FUNC_NetReliable));
	TestTrue(TEXT("Terminal is a reliable multicast"),
		Terminal->HasAllFunctionFlags(FUNC_NetMulticast | FUNC_NetReliable));
	return true;
}
#endif

