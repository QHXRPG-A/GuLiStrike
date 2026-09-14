// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Resources/GuLiResourceActors.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "Misc/AutomationTest.h"

namespace GuLiResourceTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter;

	struct FTransientGameWorldFixture
	{
		UWorld* World = nullptr;
		bool bWorldContextRegistered = false;

		~FTransientGameWorldFixture()
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
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Engine exists for resource tests"), GEngine))
			{
				return false;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!Test.TestNotNull(TEXT("Transient resource test World exists"), World))
			{
				return false;
			}
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			bWorldContextRegistered = true;
			return true;
		}
	};

	TArray<EGuLiTeam> MakeInitialOwners()
	{
		TArray<EGuLiTeam> Owners;
		Owners.Init(EGuLiTeam::Unassigned, GULI_RESOURCE_TERRITORY_COUNT);
		Owners[GuLiResources::ToTerritoryIndex(1, 3)] = EGuLiTeam::Red;
		Owners[GuLiResources::ToTerritoryIndex(5, 3)] = EGuLiTeam::Blue;
		return Owners;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiResourceBoardContractTest,
	"GuLiStrike.Resources.Board.CanonicalContract",
	GuLiResourceTests::Flags)

bool FGuLiResourceBoardContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	int32 BlueTotal = 0;
	int32 RedTotal = 0;
	for (int32 Row = 1; Row <= GULI_RESOURCE_BOARD_DIMENSION; ++Row)
	{
		for (int32 Column = 1; Column <= GULI_RESOURCE_BOARD_DIMENSION; ++Column)
		{
			const int32 MirrorRow = GULI_RESOURCE_BOARD_DIMENSION + 1 - Row;
			const int32 MirrorColumn = GULI_RESOURCE_BOARD_DIMENSION + 1 - Column;
			const FVector Center = GuLiResources::GetTerritoryCenter(Row, Column);
			const FVector MirrorCenter = GuLiResources::GetTerritoryCenter(MirrorRow, MirrorColumn);
			TestTrue(TEXT("Territory center obeys 180-degree symmetry"),
				FVector2D(Center).Equals(-FVector2D(MirrorCenter), 0.1));
			TestEqual(TEXT("Blue budget obeys 180-degree symmetry"),
				GuLiResources::GetBlueClusterBudget(Row, Column),
				GuLiResources::GetBlueClusterBudget(MirrorRow, MirrorColumn));
			TestEqual(TEXT("Red budget obeys 180-degree symmetry"),
				GuLiResources::GetRedClusterBudget(Row, Column),
				GuLiResources::GetRedClusterBudget(MirrorRow, MirrorColumn));
			TestEqual(TEXT("Territory index is row-major"),
				GuLiResources::ToTerritoryIndex(Row, Column),
				(Row - 1) * GULI_RESOURCE_BOARD_DIMENSION + Column - 1);
			BlueTotal += GuLiResources::GetBlueClusterBudget(Row, Column);
			RedTotal += GuLiResources::GetRedClusterBudget(Row, Column);
		}
	}
	TestEqual(TEXT("Blue cluster budget totals 200"), BlueTotal, GULI_RESOURCE_BLUE_CLUSTER_COUNT);
	TestEqual(TEXT("Red cluster budget totals 40"), RedTotal, GULI_RESOURCE_RED_CLUSTER_COUNT);
	TestEqual(TEXT("R1C3 is the north home center"),
		GuLiResources::GetTerritoryCenter(1, 3), FVector(0.0, 224000.0, 0.0));
	TestEqual(TEXT("R5C3 is the south home center"),
		GuLiResources::GetTerritoryCenter(5, 3), FVector(0.0, -224000.0, 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiResourceEconomyContractTest,
	"GuLiStrike.Resources.Economy.DefaultsAndConservation",
	GuLiResourceTests::Flags)

bool FGuLiResourceEconomyContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UGuLiResourceEconomyConfig* Config = NewObject<UGuLiResourceEconomyConfig>();
	FString Error;
	TestTrue(TEXT("Native resource economy defaults validate"), Config->ValidateConfig(Error));
	TestEqual(TEXT("Ore renderer has 24 visual keys"), Config->OreVisuals.Num(), 24);
	TestEqual(TEXT("Economy references the common Soldiers miner row"), Config->MiningVehicleUnitTypeId, 3);
	TestEqual(TEXT("Factory speed triples to 15m/s"), Config->FactoryManeuverSpeedCentimetersPerSecond, 1500.0f);
	TestEqual(TEXT("Sentry costs 10 blue"),
		Config->GetBlueBuildingCost(EGuLiBuildingType::SentryTurret), 10);
	TestEqual(TEXT("Missile tower costs 20 blue"),
		Config->GetBlueBuildingCost(EGuLiBuildingType::MissileTurret), 20);
	TestEqual(TEXT("Outpost costs 40 blue"),
		Config->GetBlueBuildingCost(EGuLiBuildingType::Outpost), 40);

	FGuLiResourceAmounts Amounts;
	TestTrue(TEXT("Inventory accepts a positive deposit"), Amounts.Add(EGuLiResourceType::Blue, 40));
	TestFalse(TEXT("Inventory rejects overdraft"), Amounts.Remove(EGuLiResourceType::Blue, 41));
	TestTrue(TEXT("Inventory accepts an affordable reservation"),
		Amounts.Remove(EGuLiResourceType::Blue, 20));
	TestEqual(TEXT("Successful subtraction conserves inventory"), Amounts.Blue, 20);
	TestTrue(TEXT("Refund restores reserved inventory"), Amounts.Add(EGuLiResourceType::Blue, 20));
	TestEqual(TEXT("Deposit - reserve + refund conserves resources"), Amounts.Blue, 40);
	TestEqual(TEXT("Three successful mutations advance revision three times"), Amounts.Revision, 3u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiResourceBakedAssetContractTest,
	"GuLiStrike.Resources.Bake.CanonicalAsset",
	GuLiResourceTests::Flags)

bool FGuLiResourceBakedAssetContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UGuLiResourceMapDefinition* Definition = LoadObject<UGuLiResourceMapDefinition>(nullptr,
		TEXT("/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap.DA_CommanderResourceMap"));
	const UGuLiResourceEconomyConfig* Config = LoadObject<UGuLiResourceEconomyConfig>(nullptr,
		TEXT("/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy.DA_ResourceEconomy"));
	if (!TestNotNull(TEXT("Canonical baked map definition exists"), Definition)
		|| !TestNotNull(TEXT("Canonical economy definition exists"), Config))
	{
		return false;
	}
	FString Error;
	TestTrue(FString::Printf(TEXT("Canonical bake validates: %s"), *Error),
		Definition->ValidateDefinition(Error));
	TestTrue(TEXT("Source hash is populated"), !Definition->SourceHash.IsEmpty());
	TestEqual(TEXT("Layout hash is deterministic"),
		Definition->CalculateLayoutHash(), Definition->CalculateLayoutHash());
	TestEqual(TEXT("Layout hash matches the stored bake"),
		Definition->CalculateLayoutHash(), Definition->LayoutHash);
	TestEqual(TEXT("Canonical bake contains 25 territories"),
		Definition->Territories.Num(), GULI_RESOURCE_TERRITORY_COUNT);
	TestEqual(TEXT("Canonical bake contains 240 clusters"),
		Definition->Clusters.Num(), GULI_RESOURCE_CLUSTER_COUNT);
	TestEqual(TEXT("Canonical bake contains 6240 logical nodes"),
		Definition->Nodes.Num(), GULI_RESOURCE_NODE_COUNT);
	TestEqual(TEXT("Economy bake contains no more than 24 HISM visual keys"),
		Config->OreVisuals.Num(), 24);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiResourceFastArrayStateTest,
	"GuLiStrike.Resources.Network.PublicDeltaState",
	GuLiResourceTests::Flags)

bool FGuLiResourceFastArrayStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	GuLiResourceTests::FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	AGuLiResourceWorldState* State = Fixture.World->SpawnActor<AGuLiResourceWorldState>();
	if (!TestNotNull(TEXT("Authority resource state spawns"), State))
	{
		return false;
	}
	const TArray<EGuLiTeam> Owners = GuLiResourceTests::MakeInitialOwners();
	State->InitializeAuthority(TEXT("test-layout-hash"), Owners);
	TestEqual(TEXT("Only north home begins red"),
		State->GetTerritoryOwner(GuLiResources::ToTerritoryIndex(1, 3)), EGuLiTeam::Red);
	TestEqual(TEXT("Only south home begins blue"),
		State->GetTerritoryOwner(GuLiResources::ToTerritoryIndex(5, 3)), EGuLiTeam::Blue);
	TestEqual(TEXT("Neutral Territory can be reassigned authoritatively"),
		State->SetTerritoryOwnerAuthority(
			GuLiResources::ToTerritoryIndex(3, 3), EGuLiTeam::Red), true);
	TestTrue(TEXT("First ore decrement creates a delta"), State->SetNodeRemainingAuthority(1u, 2u));
	TestTrue(TEXT("Second ore decrement updates the same delta"), State->SetNodeRemainingAuthority(1u, 1u));
	TestEqual(TEXT("One changed logical node occupies one FastArray item"), State->GetOreDeltas().Num(), 1);
	TestEqual(TEXT("FastArray lookup exposes the latest remaining amount"),
		State->GetNodeRemainingOr(1u, 3u), static_cast<uint8>(1u));
	TestEqual(TEXT("Untouched nodes retain their baked amount"),
		State->GetNodeRemainingOr(2u, 3u), static_cast<uint8>(3u));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiResourceHISMSwapIndexTest,
	"GuLiStrike.Resources.Rendering.HISMSwapIndex",
	GuLiResourceTests::Flags)

bool FGuLiResourceHISMSwapIndexTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	GuLiResourceTests::FTransientGameWorldFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("Engine cube exists for HISM bookkeeping test"), Cube))
	{
		return false;
	}
	UGuLiResourceEconomyConfig* Config = NewObject<UGuLiResourceEconomyConfig>(Fixture.World);
	for (FGuLiOreVisualAsset& Visual : Config->OreVisuals)
	{
		Visual.Mesh = Cube;
	}
	UGuLiResourceMapDefinition* Definition = NewObject<UGuLiResourceMapDefinition>(Fixture.World);
	Definition->Nodes.SetNum(GULI_RESOURCE_NODE_COUNT);
	for (int32 Index = 0; Index < Definition->Nodes.Num(); ++Index)
	{
		FGuLiResourceNodeDefinition& Node = Definition->Nodes[Index];
		Node.NodeId = static_cast<uint32>(Index + 1);
		Node.ClusterId = static_cast<uint16>(Index / GULI_RESOURCE_NODES_PER_CLUSTER + 1);
		Node.ResourceType = Index % 2 == 0 ? EGuLiResourceType::Blue : EGuLiResourceType::Red;
		Node.FamilyIndex = static_cast<uint8>((Index / 2) % 4);
		Node.InitialAmount = static_cast<uint8>(Index % 3 + 1);
		Node.WorldTransform = FTransform(FRotator(0.0, Index % 360, 0.0),
			FVector(Index % 80 * 200.0, Index / 80 * 200.0, 0.0), FVector::OneVector);
	}
	AGuLiResourceWorldState* State = Fixture.World->SpawnActor<AGuLiResourceWorldState>();
	AGuLiOreFieldActor* Field = Fixture.World->SpawnActor<AGuLiOreFieldActor>();
	if (!TestNotNull(TEXT("HISM test state spawns"), State)
		|| !TestNotNull(TEXT("HISM test field spawns"), Field))
	{
		return false;
	}
	State->InitializeAuthority(TEXT("hism-test-layout"), GuLiResourceTests::MakeInitialOwners());
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("6240-node HISM field initializes: %s"), *Error),
		Field->InitializeField(*Definition, *Config, *State, Error)))
	{
		return false;
	}
	TestEqual(TEXT("All logical nodes begin as visible HISM instances"),
		Field->GetVisibleNodeCount(), GULI_RESOURCE_NODE_COUNT);
	TestTrue(TEXT("Initial HISM reverse indexes validate"), Field->ValidateInstanceIndexMap(&Error));
	Field->ApplyNodeAmount(1u, 0u);
	Field->ApplyNodeAmount(49u, 0u);
	Field->ApplyNodeAmount(2u, 3u);
	TestEqual(TEXT("Two removed nodes reduce the visible count by two"),
		Field->GetVisibleNodeCount(), GULI_RESOURCE_NODE_COUNT - 2);
	TestTrue(FString::Printf(TEXT("Swap-removal reverse indexes validate: %s"), *Error),
		Field->ValidateInstanceIndexMap(&Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGuLiMiningCommandContractTest,
	"GuLiStrike.Resources.Commands.ValidationAndRevision",
	GuLiResourceTests::Flags)

bool FGuLiMiningCommandContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FGuLiMiningCommand Command;
	TestFalse(TEXT("Zero request/revision is rejected"), Command.IsWellFormed());
	Command.RequestId = 7u;
	Command.SelectionRevision = 3u;
	Command.Type = EGuLiMiningOrderType::Move;
	Command.Target = FVector(100.0, 200.0, 0.0);
	TestTrue(TEXT("Move command with stable revisions validates"), Command.IsWellFormed());
	Command.Type = EGuLiMiningOrderType::MineCluster;
	TestFalse(TEXT("Mine command without ClusterId is rejected"), Command.IsWellFormed());
	Command.ClusterId = 15u;
	TestTrue(TEXT("Mine command with ClusterId validates"), Command.IsWellFormed());
	Command.Type = EGuLiMiningOrderType::ReturnToFactory;
	TestFalse(TEXT("Return command cannot smuggle a ClusterId"), Command.IsWellFormed());
	Command.ClusterId = 0u;
	TestTrue(TEXT("Return command with no ClusterId validates"), Command.IsWellFormed());
	return true;
}

#endif
