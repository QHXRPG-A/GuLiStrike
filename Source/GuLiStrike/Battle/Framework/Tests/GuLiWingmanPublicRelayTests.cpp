// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Framework/GuLiBattleGameState.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Battle/Relay/GuLiWingmanRelayServer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Gameplay/Ship/Abilities/GuLiShipAbilityTags.h"
#include "Misc/AutomationTest.h"

namespace GuLiWingmanPublicRelayTests
{
	struct FWorldFixture
	{
		UWorld* World = nullptr;
		bool bWorldContextRegistered = false;

		~FWorldFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (bWorldContextRegistered && GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
			}
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Engine exists for public Relay test"), GEngine))
			{
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("Isolated game World exists"), World))
			{
				return false;
			}
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bWorldContextRegistered = true;
			return true;
		}
	};

	FGuLiWingmanGroupHandle MakeGroup()
	{
		FGuLiWingmanGroupHandle Group;
		Group.ShipInstanceId = FGuid(0x31323334u, 0x41424344u, 0x51525354u, 0x61626364u);
		Group.ShipGeneration = 2u;
		Group.GroupGeneration = 3u;
		return Group;
	}

	FGuLiGroupAbilityConfigSnapshot MakeConfig(const FGuLiWingmanGroupHandle& Group)
	{
		FGuLiGroupAbilityConfigSnapshot Config;
		Config.ShipInstanceId = Group.ShipInstanceId;
		Config.ShipGeneration = Group.ShipGeneration;
		Config.GroupGeneration = Group.GroupGeneration;
		Config.AbilitySetRevision = 1u;
		Config.SnapshotRevision = 1u;
		Config.bGroupAbilitiesValid = true;
		Config.FormationAbilityId = TAG_GuLi_ShipAbility_Formation_DoubleRing;
		Config.BasicWeaponAbilityId = TAG_GuLi_ShipAbility_Weapon_Basic_Auto;
		Config.MissileAbilityId = TAG_GuLi_ShipAbility_Weapon_Missile_Salvo;
		Config.FormationDefinitionRevision = 1u;
		Config.FormationDefinitionChecksum = 11u;
		Config.BasicWeaponDefinitionRevision = 1u;
		Config.BasicWeaponDefinitionChecksum = 12u;
		Config.MissileDefinitionRevision = 1u;
		Config.MissileDefinitionChecksum = 13u;
		Config.FormationCommandRevision = 1u;
		Config.EffectiveClientSimTick = 1u;
		Config.RefreshHash();
		return Config;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanPublicRelayRetentionTest,
	"GuLiStrike.Wingman.Relay.PublicTransport.RetainedBootstrapAndRevocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanPublicRelayRetentionTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanPublicRelayTests;
	FWorldFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	FGuLiWingmanRelayServer Relay;
	const FGuLiWingmanGroupHandle Group = MakeGroup();
	const FGuid Owner(1u, 2u, 3u, 4u);
	const FGuid Backup(5u, 6u, 7u, 8u);
	if (!TestTrue(TEXT("Relay initializes a public test group"),
		Relay.InitializeGroup(9u, Group, Owner, Backup, MakeConfig(Group), 0.0)))
	{
		return false;
	}
	FGuLiWingmanBootstrapBundle Bootstrap;
	if (!TestTrue(TEXT("Relay produces one well-formed six-scope cut"),
		Relay.BuildBootstrap(Bootstrap)))
	{
		return false;
	}

	AGuLiBattleGameState* GameState = Fixture.World->SpawnActor<AGuLiBattleGameState>();
	if (!TestNotNull(TEXT("Authority GameState exists"), GameState))
	{
		return false;
	}
	TestTrue(TEXT("Authority publishes the complete Bootstrap"),
		GameState->ServerPublishWingmanBootstrap(
			Bootstrap, EGuLiWingmanGroupLifecycle::Initializing));
	TestEqual(TEXT("LateJoin retained set has exactly one live group"),
		GameState->GetPublicWingmanBootstraps().Num(), 1);
	const uint32 FirstRevision = GameState->GetPublicWingmanBootstraps()[0].PublicationRevision;
	TestTrue(TEXT("Retained public state remains a complete atomic cut"),
		GameState->GetPublicWingmanBootstraps()[0].IsWellFormed());

	TestTrue(TEXT("An identical publication is accepted idempotently"),
		GameState->ServerPublishWingmanBootstrap(
			Bootstrap, EGuLiWingmanGroupLifecycle::Initializing));
	TestEqual(TEXT("Identical cuts do not consume reliable publication bandwidth"),
		GameState->GetPublicWingmanBootstraps()[0].PublicationRevision, FirstRevision);

	GameState->ServerRevokeWingmanGroup(Group);
	TestEqual(TEXT("Revocation removes the group from the LateJoin retained set"),
		GameState->GetPublicWingmanBootstraps().Num(), 0);
	return true;
}
#endif
