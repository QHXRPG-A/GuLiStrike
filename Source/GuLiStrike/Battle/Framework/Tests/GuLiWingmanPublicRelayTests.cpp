// Copyright Epic Games, Inc. All Rights Reserved.

#include "Battle/Framework/GuLiBattleGameState.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Battle/Network/Relay/GuLiWingmanRelayComponent.h"
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
		Config.MatchEpoch = 9u;
		Config.Team = EGuLiTeam::Red;
		Config.OwnerPlayerGuid = FGuid(1u, 2u, 3u, 4u);
		Config.WingmanTypeId = TEXT("PublicRelayTestWingman");
		Config.ShipGeneration = Group.ShipGeneration;
		Config.GroupGeneration = Group.GroupGeneration;
		Config.AbilitySetRevision = 1u;
		Config.LoadoutRevision = 1u;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuLiWingmanPublicPoseBeforeGameStateTest,
	"GuLiStrike.Wingman.Relay.PublicTransport.PoseFrameBeforeGameState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGuLiWingmanPublicPoseBeforeGameStateTest::RunTest(const FString& Parameters)
{
	using namespace GuLiWingmanPublicRelayTests;
	// Exercise the actual RPC thunk with a valid, nonempty frame: an empty or
	// malformed batch would not reach the original null GameState dereference.
	struct FPublicPoseFrameParams
	{
		TArray<FGuLiWingmanAcceptedBatch> AcceptedBatches;
	} Frame;
	FGuLiWingmanAcceptedBatch& Batch = Frame.AcceptedBatches.AddDefaulted_GetRef();
	Batch.Group = MakeGroup();
	Batch.StateRef.MatchEpoch = 1u;
	Batch.StateRef.GroupGeneration = Batch.Group.GroupGeneration;
	Batch.StateRef.AcceptedSequence = 1u;
	Batch.StateRef.ClientSimTick = 1u;
	Batch.CarrierSource.CanonicalEpoch = 1u;
	Batch.CarrierSource.MoveRevision = 1u;
	Batch.ConnectionGeneration = 1u;
	Batch.RosterRevision = 1u;
	Batch.FlightIndex = 0u;
	Batch.FrameSequence = 1u;
	Batch.AbilitySetRevision = 1u;
	Batch.FormationCommandRevision = 1u;
	Batch.FormationDefinitionChecksum = 1u;
	FGuLiWingmanCandidateSample& Sample = Batch.Samples.AddDefaulted_GetRef();
	Sample.Wingman.Flight.Group = Batch.Group;
	Sample.Wingman.Flight.FlightIndex = 0u;
	Sample.Wingman.MemberIndex = 0u;
	Sample.Wingman.EntityGeneration = 1u;
	Batch.RefreshHash();
	if (!TestTrue(TEXT("The public pose frame contains a valid batch"), Batch.IsWellFormed()))
	{
		return false;
	}

	UGuLiWingmanRelayComponent* DetachedRelay = NewObject<UGuLiWingmanRelayComponent>();
	UFunction* ReceiveFrame = DetachedRelay->FindFunctionChecked(TEXT("ClientReceivePublicPoseFrame"));
	TestNull(TEXT("The detached receiver has no World"), DetachedRelay->GetWorld());
	DetachedRelay->ProcessEvent(ReceiveFrame, &Frame);

	FWorldFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	AActor* Owner = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("The receiver owner exists"), Owner))
	{
		return false;
	}
	UGuLiWingmanRelayComponent* Relay = NewObject<UGuLiWingmanRelayComponent>(Owner);
	TestEqual(TEXT("The receiver has the joining World"), Relay->GetWorld(), Fixture.World);
	TestNull(TEXT("GameState has not arrived yet"), Fixture.World->GetGameState());
	Relay->ProcessEvent(ReceiveFrame, &Frame);
	TestNull(TEXT("An early frame does not create a substitute GameState"), Fixture.World->GetGameState());

	AGuLiBattleGameState* GameState = Fixture.World->SpawnActor<AGuLiBattleGameState>();
	if (!TestNotNull(TEXT("GameState can arrive after the early frame"), GameState))
	{
		return false;
	}
	Fixture.World->SetGameState(GameState);
	Relay->ProcessEvent(ReceiveFrame, &Frame);
	TestEqual(TEXT("The next frame uses the now available GameState"),
		Fixture.World->GetGameState<AGuLiBattleGameState>(), GameState);
	return true;
}
#endif
