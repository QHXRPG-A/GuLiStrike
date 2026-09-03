// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/GuLiStrikeShip.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilitySystemComponent.h"
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
			return Test.TestTrue(TEXT("The session-level Ship loadout begins well formed"),
				PlayerState->GetShipAbilityLoadoutState().IsWellFormed());
		}

		AGuLiStrikeShip* SpawnAndPossessShip(FAutomationTestBase& Test, const TCHAR* Description)
		{
			UClass* ShipClass = LoadObject<UClass>(
				nullptr, TEXT("/Game/GuLiStrike/Ship/BP_GuLiStrikeShip.BP_GuLiStrikeShip_C"));
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

	int32 CountActiveAbilities(const UAbilitySystemComponent& AbilitySystem)
	{
		int32 Count = 0;
		for (const FGameplayAbilitySpec& Spec : AbilitySystem.GetActivatableAbilities())
		{
			Count += Spec.IsActive() ? 1 : 0;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiShipWorldRespawnAbilityLifecycleTest,
	"GuLiStrike.Ship.Abilities.WorldRespawnCreatesFreshASCAndRetainsStableLoadout",
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
	UGuLiShipAbilitySystemComponent* FirstASC = FirstShip->GetShipAbilitySystemComponent();
	if (!TestNotNull(TEXT("The first Ship owns its ASC"), FirstASC))
	{
		return false;
	}
	const FGuLiGroupAbilityConfigSnapshot FirstConfig = FirstShip->GetGroupAbilityConfig();
	const FGuLiShipAbilityLoadoutState FirstLoadout = FirstASC->GetAppliedLoadout();
	TestTrue(TEXT("The first Ship projects an active three-ability group config"),
		FirstConfig.IsUsableByLeaseOwner());
	TestEqual(TEXT("The first Ship grants exactly the three v1 stable abilities"),
		FirstASC->GetActivatableAbilities().Num(), 3);
	TestTrue(TEXT("The first Ship loadout matches the PlayerState session loadout"),
		FirstLoadout.HasSameSelection(Fixture.PlayerState->GetShipAbilityLoadoutState()));
	TestTrue(TEXT("The default formation/basic passives are active"), CountActiveAbilities(*FirstASC) >= 2);

	const FGuid CooldownReservation = FGuid::NewGuid();
	TestTrue(TEXT("The old Ship can own a temporary missile cooldown"),
		FirstASC->ServerTryReserveMissileCooldown(8.0f, CooldownReservation));
	TestTrue(TEXT("The old Ship cooldown is active before death/destruction"),
		FirstASC->IsMissileCooldownActive());

	Fixture.Controller->UnPossess();
	TestTrue(TEXT("Destroying the old Ship executes its EndPlay lifecycle"), FirstShip->Destroy());
	TestFalse(TEXT("The old Ship EndPlay cancels every active ability"),
		CountActiveAbilities(*FirstASC) > 0);
	TestEqual(TEXT("The old Ship EndPlay removes every granted ability spec"),
		FirstASC->GetActivatableAbilities().Num(), 0);
	TestNull(TEXT("The old Ship EndPlay releases its applied ability set"),
		FirstASC->GetAppliedAbilitySet());
	TestTrue(TEXT("The old Ship EndPlay clears its applied stable-id loadout"),
		FirstASC->GetAppliedLoadout().AbilityIds.IsEmpty());
	TestFalse(TEXT("The old Ship EndPlay clears its temporary missile cooldown"),
		FirstASC->IsMissileCooldownActive());
	TestFalse(TEXT("The old Ship publishes a non-activatable ability-config tombstone"),
		FirstShip->GetGroupAbilityConfig().IsUsableByLeaseOwner());

	AGuLiStrikeShip* SecondShip = Fixture.SpawnAndPossessShip(*this, TEXT("The replacement real Ship spawns"));
	if (!SecondShip)
	{
		return false;
	}
	UGuLiShipAbilitySystemComponent* SecondASC = SecondShip->GetShipAbilitySystemComponent();
	if (!TestNotNull(TEXT("The replacement Ship owns its ASC"), SecondASC))
	{
		return false;
	}
	const FGuLiGroupAbilityConfigSnapshot SecondConfig = SecondShip->GetGroupAbilityConfig();
	const FGuLiShipAbilityLoadoutState SecondLoadout = SecondASC->GetAppliedLoadout();

	TestTrue(TEXT("Respawn creates a distinct Pawn-owned ASC instance"), SecondASC != FirstASC);
	TestTrue(TEXT("The replacement ASC owns and avatars only the replacement Ship"),
		SecondASC->GetOwnerActor() == SecondShip && SecondASC->GetAvatarActor() == SecondShip);
	TestTrue(TEXT("The replacement Ship has a distinct stable instance identity"),
		SecondConfig.ShipInstanceId.IsValid()
			&& SecondConfig.ShipInstanceId != FirstConfig.ShipInstanceId);
	TestTrue(TEXT("The replacement Ship allocates new Ship and group generations"),
		SecondConfig.ShipGeneration != 0u && SecondConfig.GroupGeneration != 0u
			&& SecondConfig.ShipGeneration != FirstConfig.ShipGeneration
			&& SecondConfig.GroupGeneration != FirstConfig.GroupGeneration);
	TestEqual(TEXT("The replacement Ship re-grants exactly three abilities"),
		SecondASC->GetActivatableAbilities().Num(), 3);
	TestTrue(TEXT("Only stable AbilityIds survive the respawn"),
		SecondLoadout.HasSameSelection(FirstLoadout)
			&& SecondLoadout.HasSameSelection(Fixture.PlayerState->GetShipAbilityLoadoutState()));
	TestFalse(TEXT("Missile cooldown never migrates to the fresh ASC"),
		SecondASC->IsMissileCooldownActive());
	TestTrue(TEXT("The replacement formation/basic passives restart as fresh instances"),
		CountActiveAbilities(*SecondASC) >= 2);
	return true;
}

#endif
