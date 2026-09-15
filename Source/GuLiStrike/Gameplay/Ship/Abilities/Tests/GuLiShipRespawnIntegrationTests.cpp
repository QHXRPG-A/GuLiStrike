// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/GuLiStrikeShip.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/Ship/Build/GuLiShipBuildComponent.h"
#include "Gameplay/Ship/Build/GuLiShipAssemblyComponent.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Ship/Capabilities/GuLiShipHangarCapabilityComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

namespace GuLiShipRespawnIntegrationTests
{
	struct FWorldFixture
	{
		UWorld* World = nullptr;
		APlayerController* Controller = nullptr;
		AGuLiBattlePlayerState* PlayerState = nullptr;
		bool bWorldContextRegistered = false;

		~FWorldFixture()
		{
			if (!World)
			{
				return;
			}
			World->DestroyWorld(false);
			if (bWorldContextRegistered && GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			World = nullptr;
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("An engine exists for the respawn fixture"), GEngine))
			{
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("An isolated authority game World exists"), World))
			{
				return false;
			}
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bWorldContextRegistered = true;
			// Enable Actor user callbacks for this isolated game World. Manually
			// dispatching BeginPlay on one Actor is not sufficient in UE 5.7:
			// UWorld::DestroyActor skips Destroyed/RouteEndPlay while the World's
			// global actor callback gate remains disabled.
			World->InitializeActorsForPlay(FURL());

			auto* GameState = World->SpawnActor<AGuLiBattleGameState>();
			if (!Test.TestNotNull(TEXT("The respawn fixture has a BattleGameState"), GameState))
			{
				return false;
			}
			World->SetGameState(GameState);
			GameState->InitializeServerMatchState();

			Controller = World->SpawnActor<APlayerController>();
			PlayerState = World->SpawnActor<AGuLiBattlePlayerState>();
			if (!Test.TestNotNull(TEXT("The respawn fixture has a PlayerController"), Controller)
				|| !Test.TestNotNull(TEXT("The respawn fixture has a BattlePlayerState"), PlayerState))
			{
				return false;
			}
			Controller->SetPlayerState(PlayerState);
			PlayerState->EnsureServerPlayerGuid();
			PlayerState->SetServerRoleAssignment(EGuLiTeam::Red, EGuLiCommanderRole::Air, 1);
			return Test.TestTrue(TEXT("A new PlayerState begins with no committed Ship choices"),
				PlayerState->GetShipBuild()->GetBuildState().ChosenNodeIds.IsEmpty());
		}

		AGuLiStrikeShip* SpawnAndPossessShip(FAutomationTestBase& Test, const TCHAR* Description)
		{
			UClass* ShipClass = LoadObject<UClass>(
				nullptr, TEXT("/Game/GuLiStrike/Ship/BP_CombatAvatarFly01.BP_CombatAvatarFly01_C"));
			if (!Test.TestNotNull(TEXT("The concrete Ship Blueprint class loads"), ShipClass)
				|| !Test.TestTrue(TEXT("The loaded class derives from AGuLiStrikeShip"),
					ShipClass->IsChildOf(AGuLiStrikeShip::StaticClass())))
			{
				return nullptr;
			}

			FActorSpawnParameters Parameters;
			Parameters.ObjectFlags |= RF_Transient;
			auto* Ship = World->SpawnActor<AGuLiStrikeShip>(ShipClass, FTransform::Identity, Parameters);
			if (!Test.TestNotNull(Description, Ship))
			{
				return nullptr;
			}
			if (!Ship->HasActorBegunPlay())
			{
				Ship->DispatchBeginPlay();
			}
			Controller->Possess(Ship);
			if (!Test.TestTrue(TEXT("The authority controller possesses the spawned Ship"),
				Controller->GetPawn() == Ship))
			{
				return nullptr;
			}
			return Ship;
		}
	};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiShipWorldRespawnAbilityLifecycleTest,
	"GuLiStrike.Ship.Abilities.WorldRespawnRestoresCommittedBuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiShipWorldRespawnAbilityLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GuLiShipRespawnIntegrationTests;

	FWorldFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AGuLiStrikeShip* FirstShip = Fixture.SpawnAndPossessShip(*this, TEXT("The first real Ship spawns"));
	if (!FirstShip)
	{
		return false;
	}
	TestNull(TEXT("Ship starts without an automatic Wingman grant"), FirstShip->GetHangarCapability());
	auto* Build = Fixture.PlayerState->GetShipBuild();
	const FGuid RequestId = FGuid::NewGuid();
	const auto Before = Build->GetBuildState();
	const auto Choice = Build->CommitConfirmedChoice(RequestId, TEXT("08"), Before.MatchEpoch, Before.BuildRevision);
	if (!TestTrue(*Choice.Reason, Choice.bCommitted)) return false;
	const auto Duplicate = Build->CommitConfirmedChoice(RequestId, TEXT("08"), Before.MatchEpoch, Before.BuildRevision);
	TestTrue(TEXT("Retry returns the original committed result"), Duplicate.bCommitted && Duplicate.BuildRevision == Choice.BuildRevision);
	auto* FirstHangar = FirstShip->GetHangarCapability();
	if (!TestNotNull(TEXT("Committed hangar grants exactly one capability"), FirstHangar)) return false;
	TestTrue(TEXT("Both authored hangar mounts exist"), FirstShip->GetPartAt(TEXT("wingman_bay_1")) && FirstShip->GetPartAt(TEXT("wingman_bay_2")));
	auto* Assembly = FirstShip->GetShipAssembly();
	auto* OriginalCatalog = Assembly->Catalog.Get();
	auto* BrokenCatalog = DuplicateObject<UGuLiShipBuildCatalog>(OriginalCatalog, FirstShip);
	++BrokenCatalog->Revision;
	for (auto& Group : BrokenCatalog->Groups) if (Group.GroupId == FName("Hangar"))
		Group.Mounts[1].PartClass = TSoftClassPtr<UGuLiStrikeShipPartComponent>(FSoftObjectPath("/Game/GuLiStrike/Ship/Parts/Missing_QA.Missing_QA_C"));
	Assembly->Catalog = BrokenCatalog;
	FString PrepareError;
	AddExpectedError(TEXT("Missing_QA"), EAutomationExpectedErrorFlags::Contains, 0);
	TestFalse(TEXT("Missing part resources reject preparation"), Assembly->PrepareBuild(Build->GetBuildState(), PrepareError));
	Assembly->Catalog = OriginalCatalog;
	TestTrue(TEXT("Failed preparation preserves the old capability and both mounts"), FirstShip->GetHangarCapability() == FirstHangar
		&& FirstShip->GetPartAt(TEXT("wingman_bay_1")) && FirstShip->GetPartAt(TEXT("wingman_bay_2")));
	TestEqual(TEXT("Failed preparation does not append a choice"), Build->GetBuildState().ChosenNodeIds.Num(), 1);
	const auto Stale = Build->CommitConfirmedChoice(FGuid::NewGuid(), TEXT("08"), Before.MatchEpoch, Before.BuildRevision);
	TestFalse(TEXT("A stale build revision cannot commit"), Stale.bCommitted);
	const auto FirstConfig = FirstShip->GetGroupAbilityConfig();
	TestTrue(TEXT("Hangar publishes a usable group"), FirstConfig.IsUsableByLeaseOwner());
	const auto Loadout = FirstHangar->GetAppliedLoadout();
	TestTrue(TEXT("Temporary cooldown starts"), FirstHangar->ServerTryReserveMissileCooldown(8.f, FGuid::NewGuid()));
	Fixture.Controller->UnPossess();
	TestTrue(TEXT("Losing control preserves persistent Wingman behavior"), FirstHangar->IsCapabilityEnabled());
	TestFalse(TEXT("Losing control closes active input"), FirstHangar->IsActiveAbilityInputEnabled());
	TestTrue(TEXT("Old Ship executes destruction lifecycle"), FirstShip->Destroy());
	TestNull(TEXT("Old runtime releases its data references"), FirstHangar->GetAppliedAbilitySet());
	TestFalse(TEXT("Old runtime releases temporary cooldowns"), FirstHangar->IsMissileCooldownActive());
	TestFalse(TEXT("Old Ship publishes a group tombstone"), FirstShip->GetGroupAbilityConfig().IsUsableByLeaseOwner());
	auto* SecondShip = Fixture.SpawnAndPossessShip(*this, TEXT("Replacement Ship spawns"));
	if (!SecondShip) return false;
	auto* SecondHangar = SecondShip->GetHangarCapability();
	if (!TestNotNull(TEXT("Match choices restore the hangar"), SecondHangar)) return false;
	TestTrue(TEXT("Respawn creates new runtime components"), FirstHangar != SecondHangar && SecondHangar->GetOwner() == SecondShip);
	TestTrue(TEXT("Immutable action selection survives respawn"), SecondHangar->GetAppliedLoadout().HasSameSelection(Loadout));
	TestFalse(TEXT("Temporary cooldown does not survive respawn"), SecondHangar->IsMissileCooldownActive());
	const auto SecondConfig = SecondShip->GetGroupAbilityConfig();
	TestTrue(TEXT("Respawn allocates fresh network identity"), SecondConfig.ShipInstanceId != FirstConfig.ShipInstanceId
		&& SecondConfig.ShipGeneration != FirstConfig.ShipGeneration && SecondConfig.GroupGeneration != FirstConfig.GroupGeneration);
	const auto Locked = Build->CommitConfirmedChoice(FGuid::NewGuid(), TEXT("11"), Before.MatchEpoch, Build->GetBuildState().BuildRevision);
	TestFalse(TEXT("Respawn retains historical route exclusion"), Locked.bCommitted);
	return true;
}

#endif
